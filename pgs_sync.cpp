#include "pgs_sync.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace winterstatic::subtitle {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kHeaderSize = 13;
constexpr std::uint32_t kPtsHz = 90000;

std::uint16_t read_be16(const std::vector<std::uint8_t>& data, std::size_t pos) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[pos]) << 8) |
        static_cast<std::uint16_t>(data[pos + 1]));
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& data, std::size_t pos) {
    return (static_cast<std::uint32_t>(data[pos]) << 24) |
           (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
           (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
           static_cast<std::uint32_t>(data[pos + 3]);
}

void write_be32(std::vector<std::uint8_t>& data, std::size_t pos, std::uint32_t value) {
    data[pos]     = static_cast<std::uint8_t>((value >> 24) & 0xffu);
    data[pos + 1] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    data[pos + 2] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    data[pos + 3] = static_cast<std::uint8_t>(value & 0xffu);
}

double transform_ms(double old_ms, const PgsSyncSpec& spec) {
    double corrected = old_ms;

    if (spec.drift_enabled) {
        double v1 = spec.video_a_seconds * 1000.0;
        double v2 = spec.video_b_seconds * 1000.0;
        double o1 = static_cast<double>(spec.offset_a_ms);
        double o2 = static_cast<double>(spec.offset_b_ms);

        if (v2 < v1) {
            std::swap(v1, v2);
            std::swap(o1, o2);
        }

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
            const double ratio = corrected_span / source_span;
            corrected = v1 + (old_ms - s1) * ratio;
        }
    } else {
        corrected += static_cast<double>(spec.constant_offset_ms);
    }

    corrected -= static_cast<double>(spec.timeline_shift_ms);
    return std::max(0.0, corrected);
}

std::uint32_t transform_ticks(
    std::uint32_t old_ticks,
    const PgsSyncSpec& spec,
    std::size_t& clamped_counter) {

    const double old_ms = static_cast<double>(old_ticks) * 1000.0 /
                          static_cast<double>(kPtsHz);

    double raw_corrected = old_ms;
    if (spec.drift_enabled) {
        double v1 = spec.video_a_seconds * 1000.0;
        double v2 = spec.video_b_seconds * 1000.0;
        double o1 = static_cast<double>(spec.offset_a_ms);
        double o2 = static_cast<double>(spec.offset_b_ms);
        if (v2 < v1) {
            std::swap(v1, v2);
            std::swap(o1, o2);
        }
        const double s1 = v1 - o1;
        const double s2 = v2 - o2;
        const double corrected_span = v2 - v1;
        const double source_span = s2 - s1;
        if (corrected_span <= 1.0 || source_span <= 1.0) {
            raw_corrected += static_cast<double>(spec.constant_offset_ms);
        } else if (old_ms <= s1) {
            raw_corrected += o1;
        } else if (old_ms >= s2) {
            raw_corrected += o2;
        } else {
            raw_corrected = v1 + (old_ms - s1) *
                (corrected_span / source_span);
        }
    } else {
        raw_corrected += static_cast<double>(spec.constant_offset_ms);
    }
    raw_corrected -= static_cast<double>(spec.timeline_shift_ms);

    if (raw_corrected < 0.0) {
        ++clamped_counter;
    }

    const double corrected_ms = transform_ms(old_ms, spec);
    const long double ticks =
        static_cast<long double>(corrected_ms) *
        static_cast<long double>(kPtsHz) / 1000.0L;

    if (ticks > static_cast<long double>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("Corrected PGS timestamp exceeds the 32-bit SUP timestamp range.");
    }

    return static_cast<std::uint32_t>(std::llround(ticks));
}

std::vector<std::uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open input PGS file.");
    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if (!in.eof() && !in) throw std::runtime_error("Could not read input PGS file.");
    return data;
}

void write_all(const fs::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not open output PGS file.");
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    if (!out) throw std::runtime_error("Could not write output PGS file.");
}

} // namespace

PgsSyncResult rewrite_pgs_sup_timing(
    const fs::path& input,
    const fs::path& output,
    const PgsSyncSpec& spec,
    bool overwrite) {

    PgsSyncResult result;

    try {
        if (input.empty() || output.empty())
            throw std::runtime_error("Input/output path is empty.");

        std::error_code ec;
        if (!fs::exists(input, ec) || ec)
            throw std::runtime_error("Input PGS file does not exist.");

        if (fs::exists(output, ec) && !ec && !overwrite)
            throw std::runtime_error("Output already exists.");

        auto data = read_all(input);
        if (data.size() < kHeaderSize)
            throw std::runtime_error("PGS file is too small to contain a SUP segment.");

        std::size_t pos = 0;
        while (pos < data.size()) {
            if (data.size() - pos < kHeaderSize)
                throw std::runtime_error("Truncated PGS segment header.");
            if (data[pos] != 0x50 || data[pos + 1] != 0x47)
                throw std::runtime_error("Invalid PGS SUP magic; expected 'PG' segment header.");

            const std::uint16_t payload_size = read_be16(data, pos + 11);
            const std::size_t next = pos + kHeaderSize + payload_size;
            if (next > data.size())
                throw std::runtime_error("Truncated PGS segment payload.");

            const std::uint32_t old_pts = read_be32(data, pos + 2);
            const std::uint32_t old_dts = read_be32(data, pos + 6);

            const std::uint32_t new_pts =
                transform_ticks(old_pts, spec, result.timestamps_clamped_to_zero);
            if (new_pts != old_pts) {
                write_be32(data, pos + 2, new_pts);
                ++result.pts_changed;
            }

            // BDSup2Sub and other valid SUP writers use DTS==0 as an omitted DTS.
            // Keep that sentinel untouched. Real non-zero DTS values receive the
            // same timing transformation as PTS.
            if (old_dts != 0) {
                const std::uint32_t new_dts =
                    transform_ticks(old_dts, spec, result.timestamps_clamped_to_zero);
                if (new_dts != old_dts) {
                    write_be32(data, pos + 6, new_dts);
                    ++result.dts_changed;
                }
            }

            ++result.segments;
            pos = next;
        }

        if (result.segments == 0)
            throw std::runtime_error("No PGS SUP segments were found.");

        // Transactional-enough standalone primitive: write sibling temp first,
        // then replace output. Integration can later reuse Cutter temp cleanup.
        fs::path temp = output;
        temp += ".pgssync.tmp";
        fs::remove(temp, ec);
        write_all(temp, data);

        if (overwrite) {
            fs::remove(output, ec);
            ec.clear();
        }
        fs::rename(temp, output, ec);
        if (ec) {
            fs::remove(temp, ec);
            throw std::runtime_error("Could not commit corrected PGS output.");
        }

        result.success = true;
        result.message = "PGS timestamps rewritten successfully; bitmap payload was not decoded.";
        return result;

    } catch (const std::exception& e) {
        result.message = e.what();
        return result;
    }
}

} // namespace winterstatic::subtitle
