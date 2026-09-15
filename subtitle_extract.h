// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WinterStatic

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace winterstatic::subtitle {

struct ToolPaths {
    std::filesystem::path ffmpeg = "ffmpeg";
    std::filesystem::path ffprobe = "ffprobe";
};

struct SubtitleTrack {
    int stream_index = -1;          // Global FFmpeg stream index (used as -map 0:<index>)
    std::string codec;
    std::string language;
    std::string title;
};

struct ExtractOptions {
    ToolPaths tools;
    bool overwrite = false;
};

struct ExtractResult {
    bool success = false;
    std::string message;
    std::filesystem::path output_path;
};

// Discover subtitle streams. Throws std::runtime_error if ffprobe cannot be run
// or the media cannot be probed successfully.
std::vector<SubtitleTrack> probe_subtitle_tracks(
    const std::filesystem::path& media,
    const ToolPaths& tools = {}
);

// Conservative guard only: codecs we know are bitmap/image subtitles.
bool is_known_bitmap_codec(const std::string& codec);

// Preferred format-preserving extraction API. Preserves standalone text subtitle formats
// where practical (ASS/SSA -> ASS, SubRip -> SRT, WebVTT -> VTT) and falls
// back to SRT conversion for other text codecs such as mov_text.
//
// output_without_extension is a base path. The selected extension is appended,
// not substituted, so a base such as "clip.embedded-subtitle" becomes
// "clip.embedded-subtitle.ass".
ExtractResult extract_subtitle_text(
    const std::filesystem::path& media,
    int stream_index,
    const std::string& source_codec,
    const std::filesystem::path& output_without_extension,
    const ExtractOptions& options = {}
);

// Legacy/explicit conversion API retained for compatibility. Extract exactly
// one global stream index to SRT via FFmpeg.
ExtractResult extract_subtitle_srt(
    const std::filesystem::path& media,
    int stream_index,
    const std::filesystem::path& output,
    const ExtractOptions& options = {}
);

} // namespace winterstatic::subtitle
