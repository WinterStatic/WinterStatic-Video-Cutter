#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace winterstatic::subtitle {

struct VobSubSyncSpec {
    std::int64_t constant_offset_ms = 0;
    bool drift_enabled = false;
    double video_a_seconds = 0.0;
    double video_b_seconds = 0.0;
    std::int64_t offset_a_ms = 0;
    std::int64_t offset_b_ms = 0;
    std::int64_t timeline_shift_ms = 0;
};

struct VobSubSyncResult {
    bool success = false;
    std::string message;
    std::size_t cues = 0;
    std::size_t idx_timestamps_changed = 0;
    std::size_t pes_pts_changed = 0;
    std::size_t control_delays_changed = 0;
    std::size_t timestamps_clamped_to_zero = 0;
};

// Rewrites timing for a VobSub .idx/.sub pair without decoding or OCR'ing
// the subtitle bitmap payload. The input paths must name a matching pair.
// The output paths must name another matching pair.
VobSubSyncResult rewrite_vobsub_timing(
    const std::filesystem::path& input_idx,
    const std::filesystem::path& input_sub,
    const std::filesystem::path& output_idx,
    const std::filesystem::path& output_sub,
    const VobSubSyncSpec& spec,
    bool overwrite = false);

} // namespace winterstatic::subtitle
