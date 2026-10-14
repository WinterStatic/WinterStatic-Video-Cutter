#include "vobsub_sync.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace winterstatic::subtitle {
namespace {

namespace fs = std::filesystem;
constexpr long double kPtsHz = 90000.0L;
constexpr std::uint64_t kPtsMask = (std::uint64_t{1} << 33) - 1;
constexpr std::uint64_t kControlTickPts = 1024;
constexpr std::size_t kMpegSector = 0x800;

struct Cue {
    std::size_t line_start = 0;
    std::size_t line_end = 0;
    std::size_t timestamp_start = 0;
    std::size_t timestamp_end = 0;
    std::int64_t start_ms = 0;
    std::uint64_t filepos = 0;
};

struct SpuView {
    std::vector<std::uint8_t> logical;
    std::vector<std::size_t> file_positions;
    std::size_t pes_pts_filepos = std::numeric_limits<std::size_t>::max();
    std::uint8_t substream_id = 0;
};

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::uint8_t> read_binary(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open input file: " + p.string());
    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.eof() && !in) throw std::runtime_error("Could not read input file: " + p.string());
    return data;
}

std::string read_text_binary(const fs::path& p) {
    const auto bytes = read_binary(p);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void write_binary(const fs::path& p, const std::vector<std::uint8_t>& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not open output file: " + p.string());
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!out) throw std::runtime_error("Could not write output file: " + p.string());
}

void write_text_binary(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not open output file: " + p.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Could not write output file: " + p.string());
}

std::int64_t parse_timestamp_ms(const std::string& s) {
    // VobSub form: HH:MM:SS:mmm
    if (s.size() != 12 || s[2] != ':' || s[5] != ':' || s[8] != ':')
        throw std::runtime_error("Unsupported VobSub timestamp format: " + s);
    auto number = [&](std::size_t pos, std::size_t n) -> int {
        int v = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const char c = s[pos + i];
            if (c < '0' || c > '9') throw std::runtime_error("Invalid VobSub timestamp: " + s);
            v = v * 10 + (c - '0');
        }
        return v;
    };
    const int h = number(0, 2);
    const int m = number(3, 2);
    const int sec = number(6, 2);
    const int ms = number(9, 3);
    if (m >= 60 || sec >= 60) throw std::runtime_error("Invalid VobSub timestamp: " + s);
    return (((static_cast<std::int64_t>(h) * 60 + m) * 60 + sec) * 1000 + ms);
}

std::string format_timestamp_ms(std::int64_t ms) {
    if (ms < 0) ms = 0;
    const std::int64_t hours = ms / 3600000;
    if (hours > 99) throw std::runtime_error("VobSub IDX timestamp exceeds two-digit hour range.");
    ms %= 3600000;
    const auto minutes = ms / 60000; ms %= 60000;
    const auto seconds = ms / 1000; const auto millis = ms % 1000;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setw(2) << minutes << ':' << std::setw(2) << seconds << ':'
        << std::setw(3) << millis;
    return oss.str();
}

std::uint64_t parse_hex(const std::string& s) {
    if (s.empty()) throw std::runtime_error("Empty VobSub filepos.");
    std::uint64_t v = 0;
    for (char c : s) {
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(10 + c - 'A');
        else throw std::runtime_error("Invalid hexadecimal VobSub filepos: " + s);
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 16)
            throw std::runtime_error("VobSub filepos overflow.");
        v = v * 16 + d;
    }
    return v;
}

std::vector<Cue> parse_idx(const std::string& text) {
    std::vector<Cue> cues;
    std::size_t pos = 0;
    std::size_t language_sections = 0;
    while (pos < text.size()) {
        std::size_t line_end = text.find_first_of("\r\n", pos);
        if (line_end == std::string::npos) line_end = text.size();
        std::string line = text.substr(pos, line_end - pos);
        const std::string low = lower_ascii(line);
        std::size_t first_nonspace = 0;
        while (first_nonspace < low.size() && std::isspace(static_cast<unsigned char>(low[first_nonspace]))) ++first_nonspace;
        const std::string trimmed = low.substr(first_nonspace);
        if (trimmed.rfind("id:", 0) == 0) ++language_sections;
        if (trimmed.rfind("time offset:", 0) == 0) {
            std::string value = line.substr(first_nonspace + 12);
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c){ return !std::isspace(c); }));
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
            if (!(value == "0" || value == "+0" || value == "-0" || value == "00:00:00:000"))
                throw std::runtime_error("VobSub v0.1 does not support a non-zero IDX 'time offset'; normalize it first.");
        }
        const std::size_t key = low.find("timestamp:");
        if (key != std::string::npos) {
            std::size_t t0 = key + 10;
            while (t0 < line.size() && std::isspace(static_cast<unsigned char>(line[t0]))) ++t0;
            const std::size_t comma = line.find(',', t0);
            if (comma == std::string::npos) throw std::runtime_error("Malformed VobSub timestamp line.");
            std::size_t t1 = comma;
            while (t1 > t0 && std::isspace(static_cast<unsigned char>(line[t1 - 1]))) --t1;
            const std::string ts = line.substr(t0, t1 - t0);

            const std::string rest_low = lower_ascii(line.substr(comma + 1));
            const std::size_t fp_rel = rest_low.find("filepos:");
            if (fp_rel == std::string::npos) throw std::runtime_error("VobSub timestamp line is missing filepos.");
            std::size_t fp = comma + 1 + fp_rel + 8;
            while (fp < line.size() && std::isspace(static_cast<unsigned char>(line[fp]))) ++fp;
            std::size_t fp_end = fp;
            while (fp_end < line.size() && std::isxdigit(static_cast<unsigned char>(line[fp_end]))) ++fp_end;
            if (fp_end == fp) throw std::runtime_error("VobSub timestamp line has an empty filepos.");

            Cue cue;
            cue.line_start = pos;
            cue.line_end = line_end;
            cue.timestamp_start = pos + t0;
            cue.timestamp_end = pos + t1;
            cue.start_ms = parse_timestamp_ms(ts);
            cue.filepos = parse_hex(line.substr(fp, fp_end - fp));
            cues.push_back(cue);
        }
        if (line_end == text.size()) break;
        if (text[line_end] == '\r' && line_end + 1 < text.size() && text[line_end + 1] == '\n') pos = line_end + 2;
        else pos = line_end + 1;
    }
    if (cues.empty()) throw std::runtime_error("No VobSub timestamp entries were found in the IDX file.");
    if (language_sections > 1)
        throw std::runtime_error("VobSub v0.1 supports a single language stream per IDX/SUB pair; multi-stream IDX files must be split first.");
    for (std::size_t i = 1; i < cues.size(); ++i) {
        if (cues[i].filepos <= cues[i - 1].filepos)
            throw std::runtime_error("VobSub filepos entries are not strictly increasing; multi-stream/interleaved IDX is not supported by v0.1.");
    }
    return cues;
}

double raw_transform_ms(double old_ms, const VobSubSyncSpec& spec) {
    double corrected = old_ms;
    if (spec.drift_enabled) {
        double v1 = spec.video_a_seconds * 1000.0;
        double v2 = spec.video_b_seconds * 1000.0;
        double o1 = static_cast<double>(spec.offset_a_ms);
        double o2 = static_cast<double>(spec.offset_b_ms);
        if (v2 < v1) { std::swap(v1, v2); std::swap(o1, o2); }
        const double s1 = v1 - o1;
        const double s2 = v2 - o2;
        const double corrected_span = v2 - v1;
        const double source_span = s2 - s1;
        if (corrected_span <= 1.0 || source_span <= 1.0) {
            corrected += static_cast<double>(spec.constant_offset_ms);
        } else if (old_ms <= s1) {
            corrected += o1;
        } else if (old_ms >= s2) {
            corrected += o2;
        } else {
            corrected = v1 + (old_ms - s1) * (corrected_span / source_span);
        }
    } else {
        corrected += static_cast<double>(spec.constant_offset_ms);
    }
    corrected -= static_cast<double>(spec.timeline_shift_ms);
    return corrected;
}

double transform_ms(double old_ms, const VobSubSyncSpec& spec, std::size_t& clamp_counter) {
    const double raw = raw_transform_ms(old_ms, spec);
    if (raw < 0.0) ++clamp_counter;
    return std::max(0.0, raw);
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("Truncated VobSub SPU field.");
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[p]) << 8) | b[p + 1]);
}

void write_be16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t v) {
    b[p] = static_cast<std::uint8_t>((v >> 8) & 0xff);
    b[p + 1] = static_cast<std::uint8_t>(v & 0xff);
}

std::uint64_t decode_mpeg_pts(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 5 > b.size()) throw std::runtime_error("Truncated MPEG PTS.");
    return ((static_cast<std::uint64_t>((b[p] >> 1) & 0x07) << 30) |
            (static_cast<std::uint64_t>(b[p + 1]) << 22) |
            (static_cast<std::uint64_t>((b[p + 2] >> 1) & 0x7f) << 15) |
            (static_cast<std::uint64_t>(b[p + 3]) << 7) |
            static_cast<std::uint64_t>((b[p + 4] >> 1) & 0x7f)) & kPtsMask;
}

void encode_mpeg_pts(std::vector<std::uint8_t>& b, std::size_t p, std::uint64_t pts) {
    pts &= kPtsMask;
    const std::uint8_t prefix = static_cast<std::uint8_t>(b[p] & 0xf0u);
    b[p]     = static_cast<std::uint8_t>(prefix | (((pts >> 30) & 0x07u) << 1) | 1u);
    b[p + 1] = static_cast<std::uint8_t>((pts >> 22) & 0xffu);
    b[p + 2] = static_cast<std::uint8_t>((((pts >> 15) & 0x7fu) << 1) | 1u);
    b[p + 3] = static_cast<std::uint8_t>((pts >> 7) & 0xffu);
    b[p + 4] = static_cast<std::uint8_t>(((pts & 0x7fu) << 1) | 1u);
}

std::size_t find_start_code(const std::vector<std::uint8_t>& data, std::size_t from, std::size_t end, std::uint8_t id) {
    for (std::size_t p = from; p + 4 <= end; ++p) {
        if (data[p] == 0x00 && data[p + 1] == 0x00 && data[p + 2] == 0x01 && data[p + 3] == id) return p;
    }
    return std::numeric_limits<std::size_t>::max();
}

SpuView collect_spu(const std::vector<std::uint8_t>& data, std::size_t cue_start, std::size_t cue_end) {
    if (cue_start >= cue_end || cue_end > data.size()) throw std::runtime_error("Invalid VobSub cue file range.");
    SpuView view;
    std::size_t search = cue_start;
    std::uint16_t declared_size = 0;

    while (search + 6 <= cue_end && (declared_size == 0 || view.logical.size() < declared_size)) {
        const std::size_t pes = find_start_code(data, search, cue_end, 0xbd);
        if (pes == std::numeric_limits<std::size_t>::max()) break;
        if (pes + 6 > cue_end) throw std::runtime_error("Truncated VobSub private-stream packet header.");
        const std::uint16_t packet_len = static_cast<std::uint16_t>((data[pes + 4] << 8) | data[pes + 5]);
        const std::size_t packet_end = pes + 6 + packet_len;
        if (packet_end > cue_end || packet_end > data.size()) throw std::runtime_error("Truncated VobSub private-stream packet.");

        std::size_t p = pes + 6;

        // VobSub files in the wild use both MPEG-1 and MPEG-2 PES header forms.
        // FFmpeg commonly emits MPEG-1 style packets; BDSup2Sub emits MPEG-2
        // style private_stream_1 packets. Support both without touching payload.
        if (p + 3 <= packet_end && (data[p] & 0xc0u) == 0x80u) {
            // MPEG-2 PES: flags1, flags2, header_data_length, optional fields.
            const std::uint8_t flags2 = data[p + 1];
            const std::size_t header_len = data[p + 2];
            const std::size_t optional = p + 3;
            if (optional + header_len > packet_end)
                throw std::runtime_error("Truncated MPEG-2 VobSub PES optional header.");
            if ((flags2 & 0x80u) != 0) {
                if (header_len < 5) throw std::runtime_error("MPEG-2 VobSub PES advertises PTS but header is too short.");
                if (view.pes_pts_filepos == std::numeric_limits<std::size_t>::max())
                    view.pes_pts_filepos = optional;
            }
            p = optional + header_len;
        } else {
            // MPEG-1 PES.
            while (p < packet_end && data[p] == 0xff) ++p; // stuffing
            if (p + 2 <= packet_end && (data[p] & 0xc0u) == 0x40u) p += 2; // STD buffer field
            if (p >= packet_end) throw std::runtime_error("Malformed VobSub PES header.");

            const std::uint8_t marker = static_cast<std::uint8_t>(data[p] & 0xf0u);
            if (marker == 0x20u) {
                if (p + 5 > packet_end) throw std::runtime_error("Truncated VobSub PES PTS.");
                if (view.pes_pts_filepos == std::numeric_limits<std::size_t>::max()) view.pes_pts_filepos = p;
                p += 5;
            } else if (marker == 0x30u) {
                if (p + 10 > packet_end) throw std::runtime_error("Truncated VobSub PES PTS/DTS.");
                if (view.pes_pts_filepos == std::numeric_limits<std::size_t>::max()) view.pes_pts_filepos = p;
                p += 10;
            } else if (data[p] == 0x0f) {
                ++p;
            } else {
                throw std::runtime_error("Unsupported VobSub MPEG PES header form.");
            }
        }

        if (p >= packet_end) throw std::runtime_error("VobSub private-stream packet lacks a substream id.");
        const std::uint8_t sid = data[p++];
        if (sid < 0x20 || sid > 0x3f) {
            search = packet_end;
            continue;
        }
        if (view.logical.empty()) view.substream_id = sid;
        if (sid != view.substream_id) {
            search = packet_end;
            continue;
        }

        for (; p < packet_end && (declared_size == 0 || view.logical.size() < declared_size); ++p) {
            view.logical.push_back(data[p]);
            view.file_positions.push_back(p);
            if (view.logical.size() == 2) {
                declared_size = static_cast<std::uint16_t>((view.logical[0] << 8) | view.logical[1]);
                if (declared_size < 4) throw std::runtime_error("Invalid VobSub SPU declared size.");
            }
        }
        search = packet_end;
    }

    if (view.logical.size() < 4 || declared_size == 0) throw std::runtime_error("Could not reconstruct VobSub SPU packet.");
    if (view.logical.size() != declared_size) throw std::runtime_error("Truncated VobSub SPU packet across MPEG packets.");
    if (view.pes_pts_filepos == std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("First VobSub PES packet did not contain a PTS.");
    return view;
}

void patch_spu_control_delays(
    std::vector<std::uint8_t>& sub_data,
    const SpuView& view,
    double old_start_ms,
    double new_start_ms,
    const VobSubSyncSpec& spec,
    VobSubSyncResult& result) {

    auto logical = view.logical;
    const std::uint16_t ctrl_rel = read_be16(logical, 2);
    const std::size_t ctrl_start = 2u + ctrl_rel;
    if (ctrl_start + 2 > logical.size()) throw std::runtime_error("Invalid VobSub SPU control offset.");

    // The first control block is immediate and begins with a pointer to the next
    // timed sequence. Timed sequences then contain: delay, next pointer, commands.
    std::size_t seq = read_be16(logical, ctrl_start);
    if (seq < ctrl_start || seq + 4 > logical.size()) throw std::runtime_error("Invalid VobSub control-sequence pointer.");

    std::size_t guard = 0;
    while (true) {
        if (++guard > 64) throw std::runtime_error("VobSub control-sequence chain is cyclic or unreasonably long.");
        const std::uint16_t old_delay_word = read_be16(logical, seq);
        const std::uint16_t next = read_be16(logical, seq + 2);

        const long double old_rel_ms = static_cast<long double>(old_delay_word) *
            static_cast<long double>(kControlTickPts) * 1000.0L / kPtsHz;
        std::size_t dummy_clamp = 0;
        const double new_abs_ms = transform_ms(old_start_ms + static_cast<double>(old_rel_ms), spec, dummy_clamp);
        const double new_rel_ms = std::max(0.0, new_abs_ms - new_start_ms);
        const long double new_word_ld = static_cast<long double>(new_rel_ms) * kPtsHz /
            (1000.0L * static_cast<long double>(kControlTickPts));
        if (new_word_ld > 65535.0L) throw std::runtime_error("Corrected VobSub display/control delay exceeds format range.");
        const std::uint16_t new_delay_word = static_cast<std::uint16_t>(std::llround(new_word_ld));

        if (new_delay_word != old_delay_word) {
            write_be16(logical, seq, new_delay_word);
            const std::size_t fp0 = view.file_positions.at(seq);
            const std::size_t fp1 = view.file_positions.at(seq + 1);
            sub_data[fp0] = logical[seq];
            sub_data[fp1] = logical[seq + 1];
            ++result.control_delays_changed;
        }

        if (next == seq) break;
        if (next < ctrl_start || static_cast<std::size_t>(next) + 4 > logical.size())
            throw std::runtime_error("Invalid VobSub next control-sequence pointer.");
        seq = next;
    }
}

void commit_pair(const fs::path& out_idx, const fs::path& out_sub,
                 const fs::path& tmp_idx, const fs::path& tmp_sub, bool overwrite) {
    std::error_code ec;
    const bool idx_exists = fs::exists(out_idx, ec) && !ec;
    ec.clear();
    const bool sub_exists = fs::exists(out_sub, ec) && !ec;
    if ((idx_exists || sub_exists) && !overwrite)
        throw std::runtime_error("Output VobSub pair already exists (use overwrite to replace it).");

    fs::path idx_bak = out_idx; idx_bak += ".vobsync.bak";
    fs::path sub_bak = out_sub; sub_bak += ".vobsync.bak";
    fs::remove(idx_bak, ec); ec.clear(); fs::remove(sub_bak, ec);

    bool moved_idx_old = false, moved_sub_old = false;
    bool moved_idx_new = false, moved_sub_new = false;
    try {
        if (idx_exists) { fs::rename(out_idx, idx_bak); moved_idx_old = true; }
        if (sub_exists) { fs::rename(out_sub, sub_bak); moved_sub_old = true; }
        fs::rename(tmp_sub, out_sub); // binary payload first
        moved_sub_new = true;
        fs::rename(tmp_idx, out_idx);
        moved_idx_new = true;
        fs::remove(idx_bak, ec); ec.clear(); fs::remove(sub_bak, ec);
    } catch (...) {
        // Remove only outputs that were actually replaced by this transaction.
        // If moving an old file to backup failed, its original path is still the
        // user's file and must never be deleted by rollback.
        if (moved_idx_new) { ec.clear(); fs::remove(out_idx, ec); }
        if (moved_sub_new) { ec.clear(); fs::remove(out_sub, ec); }
        if (moved_idx_old) { ec.clear(); fs::rename(idx_bak, out_idx, ec); }
        if (moved_sub_old) { ec.clear(); fs::rename(sub_bak, out_sub, ec); }
        ec.clear(); fs::remove(tmp_idx, ec); ec.clear(); fs::remove(tmp_sub, ec);
        throw;
    }
}

} // namespace

VobSubSyncResult rewrite_vobsub_timing(
    const fs::path& input_idx,
    const fs::path& input_sub,
    const fs::path& output_idx,
    const fs::path& output_sub,
    const VobSubSyncSpec& spec,
    bool overwrite) {

    VobSubSyncResult result;
    fs::path tmp_idx, tmp_sub;
    try {
        if (input_idx.empty() || input_sub.empty() || output_idx.empty() || output_sub.empty())
            throw std::runtime_error("VobSub input/output path is empty.");
        std::error_code ec;
        if (!fs::exists(input_idx, ec) || ec) throw std::runtime_error("Input VobSub IDX file does not exist.");
        ec.clear();
        if (!fs::exists(input_sub, ec) || ec) throw std::runtime_error("Input VobSub SUB file does not exist.");

        const std::string idx_text = read_text_binary(input_idx);
        const auto cues = parse_idx(idx_text);
        auto sub_data = read_binary(input_sub);
        if (sub_data.empty()) throw std::runtime_error("Input VobSub SUB file is empty.");

        std::vector<std::int64_t> new_starts;
        new_starts.reserve(cues.size());
        for (const auto& cue : cues) {
            const double raw = raw_transform_ms(static_cast<double>(cue.start_ms), spec);
            if (raw < 0.0) ++result.timestamps_clamped_to_zero;
            new_starts.push_back(static_cast<std::int64_t>(std::llround(std::max(0.0, raw))));
        }

        for (std::size_t i = 0; i < cues.size(); ++i) {
            if (cues[i].filepos >= sub_data.size()) throw std::runtime_error("VobSub IDX filepos lies beyond the SUB file.");
            const std::size_t range_start = static_cast<std::size_t>(cues[i].filepos);
            const std::size_t range_end = (i + 1 < cues.size())
                ? static_cast<std::size_t>(cues[i + 1].filepos)
                : sub_data.size();
            if (range_end > sub_data.size()) throw std::runtime_error("VobSub IDX filepos lies beyond the SUB file.");

            const SpuView view = collect_spu(sub_data, range_start, range_end);
            const std::uint64_t old_pts = decode_mpeg_pts(sub_data, view.pes_pts_filepos);
            const std::uint64_t new_pts = static_cast<std::uint64_t>(new_starts[i]) * 90u;
            if (new_pts > kPtsMask) throw std::runtime_error("Corrected VobSub PES PTS exceeds 33-bit MPEG range.");
            if (old_pts != new_pts) {
                encode_mpeg_pts(sub_data, view.pes_pts_filepos, new_pts);
                ++result.pes_pts_changed;
            }

            patch_spu_control_delays(sub_data, view,
                                     static_cast<double>(cues[i].start_ms),
                                     static_cast<double>(new_starts[i]),
                                     spec, result);
        }

        std::string out_idx = idx_text;
        for (std::size_t r = cues.size(); r-- > 0;) {
            const std::string replacement = format_timestamp_ms(new_starts[r]);
            const std::string old = out_idx.substr(cues[r].timestamp_start,
                                                   cues[r].timestamp_end - cues[r].timestamp_start);
            if (old != replacement) ++result.idx_timestamps_changed;
            out_idx.replace(cues[r].timestamp_start,
                            cues[r].timestamp_end - cues[r].timestamp_start,
                            replacement);
        }

        tmp_idx = output_idx; tmp_idx += ".vobsync.tmp";
        tmp_sub = output_sub; tmp_sub += ".vobsync.tmp";
        fs::remove(tmp_idx, ec); ec.clear(); fs::remove(tmp_sub, ec);
        write_text_binary(tmp_idx, out_idx);
        write_binary(tmp_sub, sub_data);
        commit_pair(output_idx, output_sub, tmp_idx, tmp_sub, overwrite);

        result.success = true;
        result.cues = cues.size();
        result.message = "VobSub timing rewritten successfully; bitmap/RLE image payload was not decoded.";
        return result;
    } catch (const std::exception& e) {
        std::error_code ec;
        if (!tmp_idx.empty()) fs::remove(tmp_idx, ec);
        ec.clear(); if (!tmp_sub.empty()) fs::remove(tmp_sub, ec);
        result.message = e.what();
        return result;
    }
}

} // namespace winterstatic::subtitle
