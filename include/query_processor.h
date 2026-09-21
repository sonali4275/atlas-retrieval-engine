#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "inverted_index.h"
#include "search_result.h"
#include "tokenizer.h"

class QueryProcessor {
public:
    // Boolean queries longer than this many tokens are rejected. This bounds
    // parser/evaluator recursion depth, so hostile input cannot overflow the
    // stack.
    static constexpr std::size_t kMaxBooleanQueryTokens = 512;

    QueryProcessor(const Tokenizer& tokenizer, const InvertedIndex& index);

    std::vector<SearchResult> search(const std::string& query,
                                     std::size_t top_k = 10) const;

    std::vector<SearchResult> phrase_search(const std::string& query,
                                            std::size_t top_k = 10) const;

    // Boolean search with AND, OR, NOT (case-insensitive) and parentheses.
    // Precedence: NOT > AND > OR.  "A NOT B" means "A AND NOT B".
    // NOT is evaluated against the whole collection, so "NOT x" returns every
    // document without x. Matches are ranked by BM25 over the non-negated
    // terms.
    //
    // If the query is invalid, no results are returned and, when `error` is
    // non-null, it receives a human-readable message (it is cleared on
    // success).
    std::vector<SearchResult> boolean_search(const std::string& query,
                                             std::size_t top_k = 10,
                                             std::string* error = nullptr) const;

private:
    const Tokenizer& tokenizer_;
    const InvertedIndex& index_;

    std::vector<std::string> tokenize_boolean_query(
        const std::string& query) const;
};
