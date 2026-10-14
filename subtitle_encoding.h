#pragma once

#include <string>
#include <string_view>

namespace winterstatic::subtitle {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
};

struct NormalizeTextResult {
    bool success = false;
    bool converted = false;
    TextEncoding source_encoding = TextEncoding::Utf8;
    std::string utf8;
    std::string message;
};

// Normalize subtitle/text bytes to BOM-free UTF-8.
//
// Supported inputs:
//   * valid UTF-8 (ASCII included), with or without UTF-8 BOM
//   * UTF-16 LE with BOM
//   * UTF-16 BE with BOM
//
// Explicitly rejected:
//   * UTF-32 BOM input
//   * malformed UTF-8
//   * malformed UTF-16 / invalid surrogate sequences
//   * BOM-less legacy encodings that are not valid UTF-8
//
// The function does not inspect subtitle syntax. It only normalizes encoding.
NormalizeTextResult normalize_text_to_utf8(std::string_view bytes);

const char* text_encoding_name(TextEncoding encoding);

}  // namespace winterstatic::subtitle
