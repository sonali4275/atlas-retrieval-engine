#pragma once

#include <string>
#include <vector>

// Splits text into lowercase ASCII alphanumeric tokens.
//
// Rules:
//   * Any character that is not [A-Za-z0-9] ends the current token, so
//     "state-of-the-art" -> state, of, the, art and "end.Start" -> end, start.
//   * An apostrophe inside a word is dropped without splitting:
//     "don't" -> dont.
//   * Non-ASCII bytes are treated as separators. Real Unicode segmentation
//     and case folding (ICU / utf8proc) is out of scope for now.
class Tokenizer {
public:
    std::vector<std::string> tokenize(const std::string& text) const;
};
