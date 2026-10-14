#include "subtitle_encoding.h"

#include <cstddef>
#include <cstdint>

namespace winterstatic::subtitle {
namespace {

bool is_continuation(std::uint8_t b) {
    return (b & 0xC0u) == 0x80u;
}

bool valid_utf8(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto b0 = static_cast<std::uint8_t>(bytes[i]);
        if (b0 <= 0x7Fu) {
            ++i;
            continue;
        }

        if (b0 >= 0xC2u && b0 <= 0xDFu) {
            if (i + 1 >= bytes.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
            if (!is_continuation(b1)) return false;
            i += 2;
            continue;
        }

        if (b0 == 0xE0u) {
            if (i + 2 >= bytes.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
            const auto b2 = static_cast<std::uint8_t>(bytes[i + 2]);
            if (b1 < 0xA0u || b1 > 0xBFu || !is_continuation(b2)) return false;
            i += 3;
            continue;
        }

        if (b0 >= 0xE1u && b0 <= 0xECu) {
            if (i + 2 >= bytes.size()) return false;
            if (!is_continuation(static_cast<std::uint8_t>(bytes[i + 1])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 2]))) {
                return false;
            }
            i += 3;
            continue;
        }

        if (b0 == 0xEDu) {
            if (i + 2 >= bytes.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
            const auto b2 = static_cast<std::uint8_t>(bytes[i + 2]);
            // Exclude UTF-8 encodings of UTF-16 surrogate code points.
            if (b1 < 0x80u || b1 > 0x9Fu || !is_continuation(b2)) return false;
            i += 3;
            continue;
        }

        if (b0 >= 0xEEu && b0 <= 0xEFu) {
            if (i + 2 >= bytes.size()) return false;
            if (!is_continuation(static_cast<std::uint8_t>(bytes[i + 1])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 2]))) {
                return false;
            }
            i += 3;
            continue;
        }

        if (b0 == 0xF0u) {
            if (i + 3 >= bytes.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
            if (b1 < 0x90u || b1 > 0xBFu ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 2])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 3]))) {
                return false;
            }
            i += 4;
            continue;
        }

        if (b0 >= 0xF1u && b0 <= 0xF3u) {
            if (i + 3 >= bytes.size()) return false;
            if (!is_continuation(static_cast<std::uint8_t>(bytes[i + 1])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 2])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 3]))) {
                return false;
            }
            i += 4;
            continue;
        }

        if (b0 == 0xF4u) {
            if (i + 3 >= bytes.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(bytes[i + 1]);
            if (b1 < 0x80u || b1 > 0x8Fu ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 2])) ||
                !is_continuation(static_cast<std::uint8_t>(bytes[i + 3]))) {
                return false;
            }
            i += 4;
            continue;
        }

        return false;
    }
    return true;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

NormalizeTextResult fail(const char* message) {
    NormalizeTextResult result;
    result.message = message;
    return result;
}

NormalizeTextResult decode_utf16(std::string_view bytes, bool little_endian) {
    if (bytes.size() < 2) return fail("UTF-16 input is missing its BOM.");

    const std::size_t payload_bytes = bytes.size() - 2;
    if ((payload_bytes % 2u) != 0u) {
        return fail("UTF-16 input has an incomplete final code unit.");
    }

    std::string utf8;
    utf8.reserve(payload_bytes);

    auto read_unit = [&](std::size_t index) -> std::uint16_t {
        const auto first = static_cast<std::uint8_t>(bytes[index]);
        const auto second = static_cast<std::uint8_t>(bytes[index + 1]);
        if (little_endian) {
            return static_cast<std::uint16_t>(first |
                (static_cast<std::uint16_t>(second) << 8));
        }
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(first) << 8) | second);
    };

    for (std::size_t i = 2; i < bytes.size(); i += 2) {
        const std::uint16_t unit = read_unit(i);

        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (i + 3 >= bytes.size()) {
                return fail("UTF-16 input ends with an unmatched high surrogate.");
            }
            const std::uint16_t low = read_unit(i + 2);
            if (low < 0xDC00u || low > 0xDFFFu) {
                return fail("UTF-16 input contains an invalid surrogate pair.");
            }
            const std::uint32_t cp = 0x10000u +
                ((static_cast<std::uint32_t>(unit) - 0xD800u) << 10) +
                (static_cast<std::uint32_t>(low) - 0xDC00u);
            append_utf8(utf8, cp);
            i += 2;
            continue;
        }

        if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            return fail("UTF-16 input contains an unmatched low surrogate.");
        }

        append_utf8(utf8, unit);
    }

    NormalizeTextResult result;
    result.success = true;
    result.converted = true;
    result.source_encoding = little_endian ? TextEncoding::Utf16Le : TextEncoding::Utf16Be;
    result.utf8 = std::move(utf8);
    result.message = "UTF-16 normalized to UTF-8.";
    return result;
}

}  // namespace

const char* text_encoding_name(TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf8: return "UTF-8";
        case TextEncoding::Utf8Bom: return "UTF-8 BOM";
        case TextEncoding::Utf16Le: return "UTF-16 LE";
        case TextEncoding::Utf16Be: return "UTF-16 BE";
    }
    return "unknown";
}

NormalizeTextResult normalize_text_to_utf8(std::string_view bytes) {
    // UTF-32 BOMs must be checked before UTF-16 LE because FF FE 00 00
    // begins with a valid UTF-16 LE BOM prefix.
    if (bytes.size() >= 4) {
        const auto b0 = static_cast<std::uint8_t>(bytes[0]);
        const auto b1 = static_cast<std::uint8_t>(bytes[1]);
        const auto b2 = static_cast<std::uint8_t>(bytes[2]);
        const auto b3 = static_cast<std::uint8_t>(bytes[3]);
        if ((b0 == 0xFFu && b1 == 0xFEu && b2 == 0x00u && b3 == 0x00u) ||
            (b0 == 0x00u && b1 == 0x00u && b2 == 0xFEu && b3 == 0xFFu)) {
            return fail("UTF-32 input is not supported.");
        }
    }

    if (bytes.size() >= 2) {
        const auto b0 = static_cast<std::uint8_t>(bytes[0]);
        const auto b1 = static_cast<std::uint8_t>(bytes[1]);
        if (b0 == 0xFFu && b1 == 0xFEu) return decode_utf16(bytes, true);
        if (b0 == 0xFEu && b1 == 0xFFu) return decode_utf16(bytes, false);
    }

    std::string_view utf8_view = bytes;
    bool had_bom = false;
    if (bytes.size() >= 3 &&
        static_cast<std::uint8_t>(bytes[0]) == 0xEFu &&
        static_cast<std::uint8_t>(bytes[1]) == 0xBBu &&
        static_cast<std::uint8_t>(bytes[2]) == 0xBFu) {
        utf8_view.remove_prefix(3);
        had_bom = true;
    }

    // Subtitle text should never contain embedded NUL characters. Besides being
    // useless to the line-oriented timing parsers, rejecting them prevents
    // BOM-less UTF-16 ASCII text (e.g. '1\0\r\0\n\0') from being
    // misclassified as technically valid UTF-8.
    if (utf8_view.find('\0') != std::string_view::npos) {
        return fail("Input contains NUL bytes and is not supported as UTF-8 text.");
    }

    if (!valid_utf8(utf8_view)) {
        return fail("Input is neither supported BOM-marked UTF-16 nor valid UTF-8.");
    }

    NormalizeTextResult result;
    result.success = true;
    result.converted = had_bom;
    result.source_encoding = had_bom ? TextEncoding::Utf8Bom : TextEncoding::Utf8;
    result.utf8.assign(utf8_view.data(), utf8_view.size());
    result.message = had_bom ? "UTF-8 BOM removed." : "Input is valid UTF-8.";
    return result;
}

}  // namespace winterstatic::subtitle
