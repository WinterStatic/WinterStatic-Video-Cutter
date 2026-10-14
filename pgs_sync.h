#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace winterstatic::subtitle {

struct PgsSyncSpec {
    int constant_offset_ms = 0;
    bool drift_enabled = false;
    double video_a_seconds = 0.0;
    double video_b_seconds = 0.0;
    int offset_a_ms = 0;
    int offset_b_ms = 0;
    int timeline_shift_ms = 0;
};

struct PgsSyncResult {
    bool success = false;
    std::string message;
    std::size_t segments = 0;
    std::size_t pts_changed = 0;
    std::size_t dts_changed = 0;
    std::size_t timestamps_clamped_to_zero = 0;
};

// Rewrites only the 32-bit PTS/DTS fields in Blu-ray PGS .sup segment headers.
// Segment payload bytes are copied unchanged. DTS==0 is preserved as 0 because
// valid SUP writers commonly use it to mean "not supplied".
PgsSyncResult rewrite_pgs_sup_timing(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const PgsSyncSpec& spec,
    bool overwrite = false);

} // namespace winterstatic::subtitle
