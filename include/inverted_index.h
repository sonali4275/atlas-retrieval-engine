#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "search_result.h"

// Forward declaration.
// IndexSerializer is allowed to access the internal index data
// for saving and loading the persisted index.
class IndexSerializer;

struct Posting {
    int document_id;
    std::vector<int> positions;
};

class InvertedIndex {
public:

    // Add a document to the inverted index.
    void add_document(
        int document_id,
        const std::vector<std::string>& tokens
    );

    // Normal BM25 search.
    std::vector<SearchResult> search(
        const std::vector<std::string>& query_tokens,
        std::size_t top_k = 10
    ) const;

    // Phrase search using positional information.
    std::vector<SearchResult> phrase_search(
        const std::vector<std::string>& query_tokens,
        std::size_t top_k = 10
    ) const;

    // Return the number of indexed documents.
    int document_count() const;

private:

    /*
     * ============================================================
     * SERIALIZATION ACCESS
     * ============================================================
     */

    // IndexSerializer needs direct access to the internal index
    // when saving and loading the persisted index.
    friend class IndexSerializer;


    /*
     * ============================================================
     * INDEX DATA
     * ============================================================
     */

    // Inverted index:
    //
    // term
    //   -> document_id
    //       -> token positions
    //
    // Example:
    // "vector" -> { 1: [3], 3: [0, 5] }
    std::unordered_map<
        std::string,
        std::unordered_map<int, std::vector<int>>
    > index_;

    // document_id -> number of tokens in that document.
    std::unordered_map<int, int> document_lengths_;


    /*
     * ============================================================
     * PERFORMANCE CACHE
     * ============================================================
     */

    // Total number of tokens across all indexed documents.
    std::size_t total_document_length_ = 0;

    // Cached average document length.
    mutable double cached_average_document_length_ = 0.0;

    // Indicates whether the cached average document length is valid.
    mutable bool average_length_cached_ = false;

    // Cached IDF values.
    mutable std::unordered_map<
        std::string,
        double
    > idf_cache_;


    /*
     * ============================================================
     * SCORING
     * ============================================================
     */

    // Calculate average document length.
    double average_document_length() const;

    // Calculate inverse document frequency.
    double calculate_idf(
        const std::string& term
    ) const;

    // Calculate BM25 score for a term/document pair.
    double calculate_bm25_score(
        const std::string& term,
        int document_id
    ) const;


    /*
     * ============================================================
     * PHRASE SEARCH
     * ============================================================
     */

    // Check whether the query terms occur consecutively
    // in the specified document.
    bool matches_phrase(
        const std::vector<std::string>& query_tokens,
        int document_id
    ) const;


    /*
     * ============================================================
     * BM25 PARAMETERS
     * ============================================================
     */

    static constexpr double k1 = 1.2;
    static constexpr double b = 0.75;
};