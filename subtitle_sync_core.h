#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace winterstatic::subsync {

enum class SubtitleKind {
    Unknown,
    Srt,
    Ass,
    Ssa,
    WebVtt,
    Pgs,
    VobSub,
};

struct SyncSpec {
    // Positive means "move the subtitle later".
    std::int64_t constant_offset_ms = 0;

    // Two-point drift mode uses the same model as the old Cutter SUBS SYNC bar:
    // at video_a_seconds the required subtitle offset is offset_a_ms;
    // at video_b_seconds the required subtitle offset is offset_b_ms.
    bool drift_enabled = false;
    double video_a_seconds = 0.0;
    double video_b_seconds = 0.0;
    std::int64_t offset_a_ms = 0;
    std::int64_t offset_b_ms = 0;
};

struct SyncResult {
    bool success = false;
    std::string message;
    SubtitleKind kind = SubtitleKind::Unknown;
    std::filesystem::path output_primary;
    std::filesystem::path output_companion;
    std::size_t timestamps_changed = 0;
    std::size_t timestamps_clamped = 0;
};

// Small, integration-friendly boundary.
// Cutter can eventually call this without knowing anything about the standalone UI.
SubtitleKind detect_kind(const std::filesystem::path& input);
const char* kind_name(SubtitleKind kind);

bool drift_geometry_valid(const SyncSpec& spec);

// Same proven affine transform used by the Cutter experiments/labs.
// For constant mode: corrected = source + constant_offset.
// For drift mode: the two anchor offsets define an affine mapping between
// source-subtitle time and corrected-video time.
std::int64_t transform_ms(std::int64_t source_ms, const SyncSpec& spec);

// Returns the non-destructive default output path(s), e.g. foo.synced.srt.
std::filesystem::path default_output_path(const std::filesystem::path& input);

// Applies sync to a single external subtitle payload.
// VobSub accepts either .idx or .sub and writes a matching .synced.idx/.synced.sub pair.
// Existing output files are not overwritten unless overwrite=true.
SyncResult apply_sync(
    const std::filesystem::path& input,
    const SyncSpec& spec,
    bool overwrite = false);

} // namespace winterstatic::subsync
