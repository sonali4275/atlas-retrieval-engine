#pragma once

#include <string>
#include <vector>

class Tokenizer {
public:
    std::vector<std::string> tokenize(const std::string& text) const;

private:
    std::string normalize(const std::string& token) const;
};