#include "tokenizer.h"

#include <cctype>
#include <sstream>

std::string Tokenizer::normalize(const std::string& token) const {
    std::string result;

    for (char ch : token) {
        unsigned char c = static_cast<unsigned char>(ch);

        if (std::isalnum(c)) {
            result += static_cast<char>(std::tolower(c));
        }
    }

    return result;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;

    std::istringstream stream(text);
    std::string word;

    while (stream >> word) {
        std::string normalized = normalize(word);

        if (!normalized.empty()) {
            tokens.push_back(normalized);
        }
    }

    return tokens;
}