#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "inverted_index.h"
#include "search_result.h"
#include "tokenizer.h"

class QueryProcessor {
public:

    QueryProcessor(
        const Tokenizer& tokenizer,
        const InvertedIndex& index
    );

    std::vector<SearchResult> search(
        const std::string& query,
        std::size_t top_k = 10
    ) const;

    std::vector<SearchResult> phrase_search(
        const std::string& query,
        std::size_t top_k = 10
    ) const;

    std::vector<SearchResult> boolean_search(
        const std::string& query,
        std::size_t top_k = 10
    ) const;

private:

    const Tokenizer& tokenizer_;
    const InvertedIndex& index_;

    std::vector<std::string> tokenize_boolean_query(
        const std::string& query
    ) const;

    bool is_boolean_operator(
        const std::string& token
    ) const;
};