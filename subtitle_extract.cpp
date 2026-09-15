// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WinterStatic

#include "subtitle_extract.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace winterstatic::subtitle {
namespace {

namespace fs = std::filesystem;
using native_string = fs::path::string_type;

struct ProcessResult {
    bool launched = false;
    int exit_code = -1;
    std::string output;
    std::string launch_error;
};

struct FormatChoice {
    std::string extension;   // without dot
    std::string codec_arg;   // FFmpeg -c:s value
    std::string format_arg;  // FFmpeg -f value
    bool lossless_copy = false;
};

// Always drain the child pipe, but keep only the most recent diagnostics.
// FFmpeg can be extremely verbose on broken media; diagnostics must not become
// an accidental unbounded-memory buffer.
static void append_process_output(std::string& output, const char* data, std::size_t size) {
    constexpr std::size_t max_capture = 1024 * 1024;
    if (size >= max_capture) {
        output.assign(data + (size - max_capture), max_capture);
        return;
    }
    if (output.size() + size > max_capture) {
        output.erase(0, output.size() + size - max_capture);
    }
    output.append(data, size);
}

static native_string native_arg(const char* s) {
    return fs::path(s).native();
}

static native_string native_arg(const std::string& s) {
    return fs::path(s).native();
}

#ifdef _WIN32
static std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";

    bool needs_quotes = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\v' || c == L'\"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return arg;

    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'\"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}
#endif

static ProcessResult run_process(const fs::path& executable,
                                 const std::vector<native_string>& args) {
    ProcessResult result;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        result.launch_error = "CreatePipe failed.";
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        result.launch_error = "SetHandleInformation failed.";
        return result;
    }

    std::wstring command = quote_windows_arg(executable.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        command += quote_windows_arg(arg);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    HANDLE null_input = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (null_input == INVALID_HANDLE_VALUE) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        result.launch_error = "Could not open NUL for child stdin.";
        return result;
    }

    si.hStdInput = null_input;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> command_buffer(command.begin(), command.end());
    command_buffer.push_back(L'\0');

    const BOOL ok = CreateProcessW(
        nullptr,
        command_buffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(write_pipe);
    write_pipe = nullptr;
    CloseHandle(null_input);

    if (!ok) {
        const DWORD code = GetLastError();
        CloseHandle(read_pipe);
        result.launch_error = "CreateProcessW failed with error " + std::to_string(code) + ".";
        return result;
    }

    result.launched = true;

    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_BROKEN_PIPE) {
                append_process_output(result.output, "\n[process pipe read failed]", std::strlen("\n[process pipe read failed]"));
            }
            break;
        }
        if (got == 0) break;
        append_process_output(result.output, buffer.data(), static_cast<std::size_t>(got));
    }

    CloseHandle(read_pipe);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;

#else
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.launch_error = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.launch_error = std::string("fork failed: ") + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<std::string> storage;
        storage.reserve(args.size() + 1);
        storage.push_back(executable.string());
        for (const auto& arg : args) storage.push_back(arg);

        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (auto& item : storage) argv.push_back(item.data());
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    result.launched = true;

    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t got = read(pipefd[0], buffer.data(), buffer.size());
        if (got > 0) {
            append_process_output(result.output, buffer.data(), static_cast<std::size_t>(got));
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        break;
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) >= 0) {
        if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
#endif
}

static std::string trim_cr(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

static std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool parse_int_exact(const std::string& s, int& value) {
    if (s.empty()) return false;
    std::size_t used = 0;
    try {
        const long long v = std::stoll(s, &used, 10);
        if (used != s.size() || v < 0 || v > 2147483647LL) return false;
        value = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

static std::vector<SubtitleTrack> parse_ffprobe_streams(const std::string& text) {
    std::vector<SubtitleTrack> tracks;
    SubtitleTrack current;
    bool in_stream = false;

    auto finish = [&]() {
        if (!in_stream) return;
        if (current.stream_index >= 0 && !current.codec.empty()) {
            tracks.push_back(current);
        }
        current = SubtitleTrack{};
        in_stream = false;
    };

    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim_cr(std::move(line));
        if (line == "[STREAM]") {
            finish();
            in_stream = true;
            current = SubtitleTrack{};
            continue;
        }
        if (line == "[/STREAM]") {
            finish();
            continue;
        }
        if (!in_stream) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "index") {
            int index = -1;
            if (parse_int_exact(value, index)) current.stream_index = index;
        } else if (key == "codec_name") {
            current.codec = value;
        } else if (key == "TAG:language") {
            current.language = value;
        } else if (key == "TAG:title") {
            current.title = value;
        }
    }
    finish();
    return tracks;
}

static fs::path temp_path_for(const fs::path& output) {
    fs::path p = output;
    p += ".subextract.tmp";
    return p;
}

// Older builds used a suffix ending in .srt for every temporary file. Keep cleaning
// that legacy name so interrupted work does not accumulate.
static fs::path legacy_temp_path_for(const fs::path& output) {
    fs::path p = output;
    p += ".subextract.tmp.srt";
    return p;
}

static fs::path backup_path_for(const fs::path& output) {
    fs::path p = output;
    p += ".subextract.bak";
    return p;
}

static void remove_stale_temp_if_present(const fs::path& temp) {
    std::error_code ec;
    const bool exists = fs::exists(temp, ec);
    if (ec) throw std::runtime_error("Could not inspect temporary output: " + ec.message());
    if (!exists) return;

    fs::remove(temp, ec);
    if (ec) throw std::runtime_error("Could not remove stale temporary output: " + ec.message());
}

static void recover_commit_state(const fs::path& output) {
    const fs::path temp = temp_path_for(output);
    const fs::path legacy_temp = legacy_temp_path_for(output);
    const fs::path backup = backup_path_for(output);
    std::error_code ec;

    const bool output_exists = fs::exists(output, ec);
    if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
    ec.clear();
    const bool backup_exists = fs::exists(backup, ec);
    if (ec) throw std::runtime_error("Could not inspect backup path: " + ec.message());

    if (!output_exists && backup_exists) {
        fs::rename(backup, output, ec);
        if (ec) throw std::runtime_error("Could not restore interrupted output backup: " + ec.message());
    } else if (output_exists && backup_exists) {
        fs::remove(backup, ec);
        if (ec) throw std::runtime_error("Could not remove stale output backup: " + ec.message());
    }

    remove_stale_temp_if_present(temp);
    if (legacy_temp != temp) remove_stale_temp_if_present(legacy_temp);
}

static FormatChoice choose_text_format(const std::string& source_codec) {
    const std::string c = lower_ascii(source_codec);

    if (c == "ass")
        return {"ass", "copy", "ass", true};

    // FFmpeg uses the ASS muxer for both ASS and SSA standalone output.
    if (c == "ssa")
        return {"ass", "copy", "ass", true};

    if (c == "subrip")
        return {"srt", "copy", "srt", true};

    if (c == "webvtt")
        return {"vtt", "copy", "webvtt", true};

    // mov_text and other text codecs without a useful standalone native
    // representation fall back to the proven SRT conversion path.
    return {"srt", "srt", "srt", false};
}

static bool basic_text_subtitle_valid(const fs::path& path,
                                      const std::string& extension) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;
    if (!fs::is_regular_file(path, ec) || ec) return false;
    if (fs::file_size(path, ec) == 0 || ec) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    // This is intentionally only a postcondition bumper, not a full parser.
    // Read enough of the beginning to recognize the standalone format.
    std::array<char, 8192> buffer{};
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::string head(buffer.data(), static_cast<std::size_t>(in.gcount()));

    const std::string ext = lower_ascii(extension);
    if (ext == "ass") {
        return head.find("[Script Info]") != std::string::npos ||
               head.find("[Events]") != std::string::npos ||
               head.find("Dialogue:") != std::string::npos;
    }
    if (ext == "vtt")
        return head.find("WEBVTT") != std::string::npos;

    return head.find("-->") != std::string::npos;
}

static bool basic_srt_valid(const fs::path& path) {
    return basic_text_subtitle_valid(path, "srt");
}

static void commit_temp(const fs::path& temp, const fs::path& output, bool overwrite) {
    const fs::path backup = backup_path_for(output);
    std::error_code ec;
    const bool exists = fs::exists(output, ec);
    if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
    if (exists && !overwrite) throw std::runtime_error("Output already exists (use --overwrite to replace it)." );

    if (!exists) {
        fs::rename(temp, output, ec);
        if (ec) throw std::runtime_error("Could not commit extracted subtitle: " + ec.message());
        return;
    }

    fs::rename(output, backup, ec);
    if (ec) throw std::runtime_error("Could not protect existing output: " + ec.message());

    ec.clear();
    fs::rename(temp, output, ec);
    if (ec) {
        const std::string install_error = ec.message();
        std::error_code restore_ec;
        fs::rename(backup, output, restore_ec);
        throw std::runtime_error(
            "Could not commit extracted subtitle: " + install_error +
            (restore_ec ? "; backup restore also failed: " + restore_ec.message() : "")
        );
    }

    ec.clear();
    fs::remove(backup, ec);
    if (ec) {
        // The new output is already valid and installed. A leftover backup is
        // harmless and can be cleaned on the next run.
    }
}

static std::string summarize_process_failure(const char* tool,
                                             const ProcessResult& process) {
    if (!process.launched) {
        return std::string("Could not start ") + tool + ": " + process.launch_error;
    }
    if (process.exit_code == 127 && process.output.empty()) {
        return std::string("Could not execute ") + tool + " (not found or not executable).";
    }
    std::ostringstream out;
    out << tool << " failed with exit code " << process.exit_code;
    if (!process.output.empty()) out << ":\n" << process.output;
    return out.str();
}

} // namespace

std::vector<SubtitleTrack> probe_subtitle_tracks(const fs::path& media,
                                                 const ToolPaths& tools) {
    if (media.empty()) throw std::runtime_error("Media path is empty.");

    std::vector<native_string> args = {
        native_arg("-v"), native_arg("error"),
        native_arg("-select_streams"), native_arg("s"),
        native_arg("-show_entries"), native_arg("stream=index,codec_name:stream_tags=language,title"),
        native_arg("-of"), native_arg("default=noprint_wrappers=0:nokey=0"),
        media.native()
    };

    const ProcessResult process = run_process(tools.ffprobe, args);
    if (!process.launched || process.exit_code != 0) {
        throw std::runtime_error(summarize_process_failure("ffprobe", process));
    }

    return parse_ffprobe_streams(process.output);
}

bool is_known_bitmap_codec(const std::string& codec) {
    const std::string c = lower_ascii(codec);
    return c == "hdmv_pgs_subtitle" ||
           c == "dvd_subtitle" ||
           c == "dvb_subtitle" ||
           c == "xsub";
}

ExtractResult extract_subtitle_text(const fs::path& media,
                                    int stream_index,
                                    const std::string& source_codec,
                                    const fs::path& output_without_extension,
                                    const ExtractOptions& options) {
    ExtractResult result;
    fs::path temp;

    try {
        if (media.empty()) throw std::runtime_error("Media path is empty.");
        if (output_without_extension.empty()) throw std::runtime_error("Output path is empty.");
        if (stream_index < 0) throw std::runtime_error("Stream index cannot be negative.");
        if (is_known_bitmap_codec(source_codec)) {
            throw std::runtime_error(
                "Selected stream is a bitmap/image subtitle and cannot be extracted through the text subtitle path."
            );
        }

        const FormatChoice format = choose_text_format(source_codec);

        // Append instead of replace_extension(): callers may deliberately use
        // dotted base names such as "clip.embedded-subtitle".
        fs::path output = output_without_extension;
        output += fs::path("." + format.extension);

        // Precondition bumper: replacing the source media is not recoverable
        // through the normal subtitle-output transaction.
        std::error_code ec;
        const bool media_exists = fs::exists(media, ec);
        if (ec) throw std::runtime_error("Could not inspect media path: " + ec.message());
        ec.clear();
        const bool output_preexists = fs::exists(output, ec);
        if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
        if (media_exists && output_preexists) {
            ec.clear();
            const bool same_file = fs::equivalent(media, output, ec);
            if (ec) throw std::runtime_error("Could not compare input and output paths: " + ec.message());
            if (same_file) throw std::runtime_error("Output path must not be the media input file.");
        }

        recover_commit_state(output);

        ec.clear();
        const bool output_exists = fs::exists(output, ec);
        if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
        if (output_exists && !options.overwrite) {
            throw std::runtime_error("Output already exists (use --overwrite to replace it)." );
        }

        temp = temp_path_for(output);
        const std::string map_value = "0:" + std::to_string(stream_index);

        std::vector<native_string> args = {
            native_arg("-nostdin"),
            native_arg("-hide_banner"),
            native_arg("-loglevel"), native_arg("error"),
            native_arg("-y"),
            native_arg("-i"), media.native(),
            native_arg("-map"), native_arg(map_value),
            native_arg("-c:s"), native_arg(format.codec_arg),
            native_arg("-f"), native_arg(format.format_arg),
            temp.native()
        };

        const ProcessResult process = run_process(options.tools.ffmpeg, args);
        if (!process.launched || process.exit_code != 0) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
            result.message = summarize_process_failure("ffmpeg", process);
            return result;
        }

        if (!basic_text_subtitle_valid(temp, format.extension)) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
            result.message =
                "FFmpeg completed, but the extracted file did not contain a recognizable ." +
                format.extension + " subtitle structure.";
            return result;
        }

        commit_temp(temp, output, options.overwrite);
        result.success = true;
        result.output_path = output;
        result.message = format.lossless_copy
            ? "Subtitle extracted successfully without text-format conversion."
            : "Subtitle extracted successfully with conversion to SRT.";
        return result;

    } catch (const std::exception& e) {
        if (!temp.empty()) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
        }
        result.message = e.what();
        return result;
    }
}

ExtractResult extract_subtitle_srt(const fs::path& media,
                                   int stream_index,
                                   const fs::path& output,
                                   const ExtractOptions& options) {
    ExtractResult result;
    fs::path temp;

    try {
        if (media.empty()) throw std::runtime_error("Media path is empty.");
        if (output.empty()) throw std::runtime_error("Output path is empty.");
        if (stream_index < 0) throw std::runtime_error("Stream index cannot be negative.");

        // This is deliberately a precondition instead of a post-failure bumper:
        // replacing the media file with an SRT would destroy the source itself.
        std::error_code ec;
        const bool media_exists = fs::exists(media, ec);
        if (ec) throw std::runtime_error("Could not inspect media path: " + ec.message());
        ec.clear();
        const bool output_preexists = fs::exists(output, ec);
        if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
        if (media_exists && output_preexists) {
            ec.clear();
            const bool same_file = fs::equivalent(media, output, ec);
            if (ec) throw std::runtime_error("Could not compare input and output paths: " + ec.message());
            if (same_file) throw std::runtime_error("Output path must not be the media input file.");
        }

        recover_commit_state(output);

        ec.clear();
        const bool output_exists = fs::exists(output, ec);
        if (ec) throw std::runtime_error("Could not inspect output path: " + ec.message());
        if (output_exists && !options.overwrite) {
            throw std::runtime_error("Output already exists (use --overwrite to replace it)." );
        }

        temp = temp_path_for(output);
        const std::string map_value = "0:" + std::to_string(stream_index);

        std::vector<native_string> args = {
            native_arg("-nostdin"),
            native_arg("-hide_banner"),
            native_arg("-loglevel"), native_arg("error"),
            native_arg("-y"),
            native_arg("-i"), media.native(),
            native_arg("-map"), native_arg(map_value),
            native_arg("-c:s"), native_arg("srt"),
            native_arg("-f"), native_arg("srt"),
            temp.native()
        };

        const ProcessResult process = run_process(options.tools.ffmpeg, args);
        if (!process.launched || process.exit_code != 0) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
            result.message = summarize_process_failure("ffmpeg", process);
            return result;
        }

        if (!basic_srt_valid(temp)) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
            result.message = "FFmpeg completed, but the extracted file did not contain a usable SRT timing marker.";
            return result;
        }

        commit_temp(temp, output, options.overwrite);
        result.success = true;
        result.output_path = output;
        result.message = "Subtitle extracted successfully.";
        return result;

    } catch (const std::exception& e) {
        if (!temp.empty()) {
            std::error_code cleanup_ec;
            fs::remove(temp, cleanup_ec);
        }
        result.message = e.what();
        return result;
    }
}

} // namespace winterstatic::subtitle
