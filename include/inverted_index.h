#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "search_result.h"

class IndexSerializer;

// In-memory positional inverted index with BM25 ranking.
//
// Thread safety: every const method is safe to call concurrently from many
// threads (there are no mutable caches). add_document() needs exclusive
// access.
class InvertedIndex {
public:
    // document_id -> ascending token positions of one term in that document.
    using PostingMap = std::unordered_map<int, std::vector<int>>;

    // Adds a document. Re-adding an existing id replaces the old content
    // (old postings are removed; this costs O(vocabulary)).
    void add_document(int document_id, const std::vector<std::string>& tokens);

    // BM25 ranking over the union of the query terms.
    std::vector<SearchResult> search(const std::vector<std::string>& query_tokens,
                                     std::size_t top_k = 10) const;

    // Documents containing the query terms consecutively, ranked with BM25
    // using the number of phrase occurrences as the term frequency.
    std::vector<SearchResult> phrase_search(
        const std::vector<std::string>& query_tokens,
        std::size_t top_k = 10) const;

    // BM25-ranks only the given candidate documents (used by Boolean search).
    // With no query terms every candidate scores 0 and ties break by id.
    std::vector<SearchResult> rank_candidates(
        const std::vector<std::string>& query_tokens,
        const std::vector<int>& candidate_ids, std::size_t top_k) const;

    // Ascending ids of documents containing `term` (empty if unknown).
    std::vector<int> documents_containing(const std::string& term) const;

    // Ascending ids of every indexed document.
    std::vector<int> all_document_ids() const;

    int document_count() const;

private:
    friend class IndexSerializer;

    struct ScoredDocument {
        int document_id;
        double score;
    };

    // term -> (document_id -> positions)
    std::unordered_map<std::string, PostingMap> index_;

    // document_id -> number of tokens in that document.
    std::unordered_map<int, int> document_lengths_;

    std::size_t total_document_length_ = 0;

    double average_document_length() const;
    double idf(std::size_t document_frequency) const;

    static double bm25_term_score(double idf, double term_frequency,
                                  double document_length,
                                  double average_length);

    void remove_document_postings(int document_id);

    // Start positions of the phrase in one document, given the posting map of
    // each phrase term in order. Empty if the phrase does not occur.
    static std::vector<int> phrase_starts(
        const std::vector<const PostingMap*>& lists, int document_id);

    // Keeps the best top_k documents, then attaches matched terms and
    // positions to those winners only.
    std::vector<SearchResult> build_results(
        std::vector<ScoredDocument> scored,
        const std::vector<std::string>& terms, std::size_t top_k) const;

    static constexpr double kBm25K1 = 1.2;
    static constexpr double kBm25B = 0.75;
};
