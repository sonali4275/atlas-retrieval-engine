#include "tokenizer.h"

namespace {

bool is_ascii_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

char to_lower_ascii(unsigned char c) {
    return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
}

}  // namespace

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (is_ascii_alnum(c)) {
            current += to_lower_ascii(c);
        } else if (c == '\'' && !current.empty() && i + 1 < text.size() &&
                   is_ascii_alnum(static_cast<unsigned char>(text[i + 1]))) {
            // Apostrophe inside a word ("don't"): join, don't split.
        } else {
            flush();
        }
    }
    flush();

    return tokens;
}
