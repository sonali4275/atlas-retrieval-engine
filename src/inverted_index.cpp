#include "inverted_index.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <utility>


// ============================================================
// ADD DOCUMENT
// ============================================================

void InvertedIndex::add_document(
    int document_id,
    const std::vector<std::string>& tokens
) {
    /*
     * If the document already exists, remove its old
     * contribution from the total document length.
     *
     * This makes add_document() safe if a document is
     * re-indexed.
     */
    auto existing_document =
        document_lengths_.find(document_id);

    if (existing_document != document_lengths_.end()) {

        total_document_length_ -=
            static_cast<std::size_t>(
                existing_document->second
            );
    }

    const int document_length =
        static_cast<int>(tokens.size());

    document_lengths_[document_id] =
        document_length;

    total_document_length_ +=
        static_cast<std::size_t>(document_length);

    /*
     * Add every token to the inverted index together
     * with its position.
     */
    for (
        int position = 0;
        position < static_cast<int>(tokens.size());
        ++position
    ) {

        const std::string& term =
            tokens[position];

        index_[term][document_id].push_back(
            position
        );
    }

    /*
     * Adding a document changes:
     *
     * - document count
     * - average document length
     * - IDF values
     *
     * Therefore invalidate all caches.
     */
    average_length_cached_ = false;
    cached_average_document_length_ = 0.0;

    idf_cache_.clear();
}


// ============================================================
// DOCUMENT COUNT
// ============================================================

int InvertedIndex::document_count() const {

    return static_cast<int>(
        document_lengths_.size()
    );
}


// ============================================================
// AVERAGE DOCUMENT LENGTH
// ============================================================

double InvertedIndex::average_document_length() const {

    /*
     * Return the cached value if it is already available.
     */
    if (average_length_cached_) {
        return cached_average_document_length_;
    }

    if (document_lengths_.empty()) {

        cached_average_document_length_ = 0.0;
        average_length_cached_ = true;

        return 0.0;
    }

    cached_average_document_length_ =
        static_cast<double>(
            total_document_length_
        ) /
        static_cast<double>(
            document_lengths_.size()
        );

    average_length_cached_ = true;

    return cached_average_document_length_;
}


// ============================================================
// IDF
// ============================================================

double InvertedIndex::calculate_idf(
    const std::string& term
) const {

    /*
     * Check the IDF cache first.
     */
    auto cached =
        idf_cache_.find(term);

    if (cached != idf_cache_.end()) {
        return cached->second;
    }

    /*
     * Find the posting list for this term.
     */
    auto term_it =
        index_.find(term);

    if (term_it == index_.end()) {

        idf_cache_[term] = 0.0;

        return 0.0;
    }

    const double total_documents =
        static_cast<double>(
            document_count()
        );

    const double document_frequency =
        static_cast<double>(
            term_it->second.size()
        );

    if (
        total_documents == 0.0 ||
        document_frequency == 0.0
    ) {

        idf_cache_[term] = 0.0;

        return 0.0;
    }

    /*
     * BM25 IDF with smoothing.
     *
     * IDF =
     * log(
     *     1 +
     *     (N - df + 0.5) /
     *     (df + 0.5)
     * )
     */
    const double idf =
        std::log(
            1.0 +
            (
                total_documents -
                document_frequency +
                0.5
            ) /
            (
                document_frequency +
                0.5
            )
        );

    /*
     * Store it for future searches.
     */
    idf_cache_[term] = idf;

    return idf;
}


// ============================================================
// BM25 SCORE
// ============================================================

double InvertedIndex::calculate_bm25_score(
    const std::string& term,
    int document_id
) const {

    /*
     * Find the term.
     */
    auto term_it =
        index_.find(term);

    if (term_it == index_.end()) {
        return 0.0;
    }

    /*
     * Find the document inside the term's posting list.
     */
    auto document_it =
        term_it->second.find(document_id);

    if (document_it == term_it->second.end()) {
        return 0.0;
    }

    /*
     * Term frequency = number of positions.
     */
    const double term_frequency =
        static_cast<double>(
            document_it->second.size()
        );

    /*
     * Find document length.
     */
    auto length_it =
        document_lengths_.find(document_id);

    if (length_it == document_lengths_.end()) {
        return 0.0;
    }

    const double document_length =
        static_cast<double>(
            length_it->second
        );

    /*
     * Average document length is cached.
     */
    const double average_length =
        average_document_length();

    if (average_length == 0.0) {
        return 0.0;
    }

    /*
     * Cached IDF.
     */
    const double idf =
        calculate_idf(term);

    /*
     * IMPORTANT:
     *
     * Do not use a local variable named "b".
     *
     * The class already contains:
     *
     * static constexpr double b = 0.75;
     *
     * Using another variable named "b" produces
     * the compiler warning that appeared previously.
     */
    const double length_normalization =
        1.0 -
        InvertedIndex::b +
        InvertedIndex::b *
        (
            document_length /
            average_length
        );

    const double denominator =
        term_frequency +
        InvertedIndex::k1 *
        length_normalization;

    if (denominator == 0.0) {
        return 0.0;
    }

    return
        idf *
        (
            (
                term_frequency *
                (InvertedIndex::k1 + 1.0)
            ) /
            denominator
        );
}


// ============================================================
// PHRASE MATCHING
// ============================================================

bool InvertedIndex::matches_phrase(
    const std::vector<std::string>& query_tokens,
    int document_id
) const {

    if (query_tokens.empty()) {
        return false;
    }

    /*
     * A one-term phrase is simply a term lookup.
     */
    if (query_tokens.size() == 1) {

        auto term_it =
            index_.find(query_tokens[0]);

        if (term_it == index_.end()) {
            return false;
        }

        return
            term_it->second.find(document_id)
            != term_it->second.end();
    }

    /*
     * Find the first term.
     */
    auto first_term_it =
        index_.find(query_tokens[0]);

    if (first_term_it == index_.end()) {
        return false;
    }

    auto first_document_it =
        first_term_it->second.find(document_id);

    if (first_document_it ==
        first_term_it->second.end()) {

        return false;
    }

    /*
     * Candidate starting positions are positions
     * where the first query term occurs.
     */
    const std::vector<int>& first_positions =
        first_document_it->second;

    for (int start_position : first_positions) {

        bool phrase_matches = true;

        /*
         * Check every subsequent query term.
         */
        for (
            std::size_t offset = 1;
            offset < query_tokens.size();
            ++offset
        ) {

            auto term_it =
                index_.find(
                    query_tokens[offset]
                );

            if (term_it == index_.end()) {

                phrase_matches = false;
                break;
            }

            auto document_it =
                term_it->second.find(
                    document_id
                );

            if (
                document_it ==
                term_it->second.end()
            ) {

                phrase_matches = false;
                break;
            }

            const int expected_position =
                start_position +
                static_cast<int>(offset);

            const std::vector<int>& positions =
                document_it->second;

            /*
             * Positions are inserted in increasing
             * order, so binary search is appropriate.
             */
            if (
                !std::binary_search(
                    positions.begin(),
                    positions.end(),
                    expected_position
                )
            ) {

                phrase_matches = false;
                break;
            }
        }

        if (phrase_matches) {
            return true;
        }
    }

    return false;
}


// ============================================================
// NORMAL SEARCH
// ============================================================

std::vector<SearchResult> InvertedIndex::search(
    const std::vector<std::string>& query_tokens,
    std::size_t top_k
) const {

    if (
        query_tokens.empty() ||
        top_k == 0
    ) {
        return {};
    }

    /*
     * Remove duplicate query terms.
     *
     * Example:
     *
     * vector vector search
     *
     * becomes:
     *
     * vector
     * search
     */
    std::unordered_set<std::string> unique_terms;

    unique_terms.reserve(
        query_tokens.size()
    );

    for (const std::string& term : query_tokens) {

        if (!term.empty()) {
            unique_terms.insert(term);
        }
    }

    if (unique_terms.empty()) {
        return {};
    }

    /*
     * Cache IDF for all query terms before scoring.
     *
     * This avoids repeatedly looking up and calculating
     * IDF values.
     */
    std::unordered_map<std::string, double> query_idf;

    query_idf.reserve(
        unique_terms.size()
    );

    for (const std::string& term : unique_terms) {

        query_idf.emplace(
            term,
            calculate_idf(term)
        );
    }

    /*
     * Score documents.
     *
     * Each document is visited through the posting lists
     * of the query terms that occur in it.
     */
    std::unordered_map<int, double> scores;

    /*
     * Reserve some space to reduce hash-table reallocations.
     */
    scores.reserve(
        std::min<std::size_t>(
            document_lengths_.size(),
            1024
        )
    );

    for (const std::string& term : unique_terms) {

        auto term_it =
            index_.find(term);

        if (term_it == index_.end()) {
            continue;
        }

        /*
         * We calculate the term's IDF once.
         */
        const double idf =
            query_idf[term];

        if (idf == 0.0) {
            continue;
        }

        /*
         * Calculate the BM25 contribution directly
         * instead of repeatedly searching for the same
         * term/document pair.
         */
        for (
            const auto& document_entry :
            term_it->second
        ) {

            const int document_id =
                document_entry.first;

            const std::vector<int>& positions =
                document_entry.second;

            auto length_it =
                document_lengths_.find(
                    document_id
                );

            if (
                length_it ==
                document_lengths_.end()
            ) {
                continue;
            }

            const double term_frequency =
                static_cast<double>(
                    positions.size()
                );

            const double document_length =
                static_cast<double>(
                    length_it->second
                );

            const double average_length =
                average_document_length();

            if (average_length == 0.0) {
                continue;
            }

            const double length_normalization =
                1.0 -
                InvertedIndex::b +
                InvertedIndex::b *
                (
                    document_length /
                    average_length
                );

            const double denominator =
                term_frequency +
                InvertedIndex::k1 *
                length_normalization;

            if (denominator == 0.0) {
                continue;
            }

            const double score =
                idf *
                (
                    (
                        term_frequency *
                        (InvertedIndex::k1 + 1.0)
                    ) /
                    denominator
                );

            scores[document_id] += score;
        }
    }

    if (scores.empty()) {
        return {};
    }

    /*
     * --------------------------------------------------------
     * Build SearchResult objects.
     * --------------------------------------------------------
     */
    std::vector<SearchResult> candidates;

    candidates.reserve(
        scores.size()
    );

    for (const auto& entry : scores) {

        SearchResult result;

        result.document_id =
            entry.first;

        result.score =
            entry.second;

        /*
         * Collect the terms that matched this document.
         */
        for (const std::string& term : unique_terms) {

            auto term_it =
                index_.find(term);

            if (term_it == index_.end()) {
                continue;
            }

            auto document_it =
                term_it->second.find(
                    result.document_id
                );

            if (
                document_it !=
                term_it->second.end()
            ) {

                /*
                 * Add matched term.
                 */
                result.matched_terms.push_back(
                    term
                );

                /*
                 * Add all positions.
                 */
                result.positions.insert(
                    result.positions.end(),
                    document_it->second.begin(),
                    document_it->second.end()
                );
            }
        }

        /*
         * Positions should be displayed in document order.
         */
        std::sort(
            result.positions.begin(),
            result.positions.end()
        );

        candidates.push_back(
            std::move(result)
        );
    }

    /*
     * --------------------------------------------------------
     * Top-K optimization.
     * --------------------------------------------------------
     *
     * Instead of sorting every result, we only keep the
     * best top_k results.
     */
    if (candidates.size() <= top_k) {

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const SearchResult& a,
               const SearchResult& b) {

                if (a.score != b.score) {
                    return a.score > b.score;
                }

                return
                    a.document_id <
                    b.document_id;
            }
        );

        return candidates;
    }

    /*
     * Comparator for the worst element in the heap.
     *
     * The heap keeps the WORST result at the top.
     */
    auto worse_first =
        [](const SearchResult& a,
           const SearchResult& b) {

            if (a.score != b.score) {
                return a.score > b.score;
            }

            return
                a.document_id <
                b.document_id;
        };

    std::priority_queue<
        SearchResult,
        std::vector<SearchResult>,
        decltype(worse_first)
    > top_results(
        worse_first
    );

    for (auto& candidate : candidates) {

        if (top_results.size() < top_k) {

            top_results.push(
                std::move(candidate)
            );

            continue;
        }

        /*
         * Compare against the current worst result.
         */
        const SearchResult& worst =
            top_results.top();

        bool better = false;

        if (
            candidate.score >
            worst.score
        ) {

            better = true;

        } else if (
            candidate.score ==
            worst.score
        ) {

            better =
                candidate.document_id <
                worst.document_id;
        }

        if (better) {

            top_results.pop();

            top_results.push(
                std::move(candidate)
            );
        }
    }

    /*
     * Extract the final Top-K results.
     */
    std::vector<SearchResult> results;

    results.reserve(
        top_results.size()
    );

    while (!top_results.empty()) {

        results.push_back(
            std::move(
                const_cast<SearchResult&>(
                    top_results.top()
                )
            )
        );

        top_results.pop();
    }

    /*
     * Heap extraction gives reverse order.
     *
     * Sort only the final Top-K results.
     */
    std::sort(
        results.begin(),
        results.end(),
        [](const SearchResult& a,
           const SearchResult& b) {

            if (a.score != b.score) {
                return a.score > b.score;
            }

            return
                a.document_id <
                b.document_id;
        }
    );

    return results;
}


// ============================================================
// PHRASE SEARCH
// ============================================================

std::vector<SearchResult> InvertedIndex::phrase_search(
    const std::vector<std::string>& query_tokens,
    std::size_t top_k
) const {

    if (
        query_tokens.empty() ||
        top_k == 0
    ) {
        return {};
    }

    /*
     * Find the rarest query term.
     *
     * Starting phrase matching from the rarest term
     * reduces the number of candidate documents.
     */
    const std::string* rarest_term = nullptr;

    std::size_t smallest_posting_size =
        static_cast<std::size_t>(-1);

    for (const std::string& term : query_tokens) {

        auto term_it =
            index_.find(term);

        if (term_it == index_.end()) {
            return {};
        }

        const std::size_t posting_size =
            term_it->second.size();

        if (
            rarest_term == nullptr ||
            posting_size < smallest_posting_size
        ) {

            rarest_term = &term;

            smallest_posting_size =
                posting_size;
        }
    }

    if (rarest_term == nullptr) {
        return {};
    }

    auto rarest_it =
        index_.find(*rarest_term);

    if (rarest_it == index_.end()) {
        return {};
    }

    /*
     * Phrase score is based on all query terms.
     */
    double phrase_score = 0.0;

    for (const std::string& term : query_tokens) {

        phrase_score +=
            calculate_idf(term);
    }

    /*
     * Candidate documents come only from the rarest term.
     */
    std::vector<SearchResult> results;

    for (
        const auto& document_entry :
        rarest_it->second
    ) {

        const int document_id =
            document_entry.first;

        if (
            !matches_phrase(
                query_tokens,
                document_id
            )
        ) {
            continue;
        }

        SearchResult result;

        result.document_id =
            document_id;

        result.score =
            phrase_score;

        /*
         * The first query term's positions are the
         * starting positions of the phrase.
         */
        auto first_term_it =
            index_.find(
                query_tokens[0]
            );

        if (
            first_term_it !=
            index_.end()
        ) {

            auto first_document_it =
                first_term_it->second.find(
                    document_id
                );

            if (
                first_document_it !=
                first_term_it->second.end()
            ) {

                for (
                    int position :
                    first_document_it->second
                ) {

                    /*
                     * Only keep positions where the complete
                     * phrase actually begins.
                     */
                    bool valid_start = true;

                    for (
                        std::size_t offset = 1;
                        offset < query_tokens.size();
                        ++offset
                    ) {

                        auto term_it =
                            index_.find(
                                query_tokens[offset]
                            );

                        if (
                            term_it ==
                            index_.end()
                        ) {

                            valid_start = false;
                            break;
                        }

                        auto document_it =
                            term_it->second.find(
                                document_id
                            );

                        if (
                            document_it ==
                            term_it->second.end()
                        ) {

                            valid_start = false;
                            break;
                        }

                        const int expected =
                            position +
                            static_cast<int>(
                                offset
                            );

                        if (
                            !std::binary_search(
                                document_it->second.begin(),
                                document_it->second.end(),
                                expected
                            )
                        ) {

                            valid_start = false;
                            break;
                        }
                    }

                    if (valid_start) {

                        result.positions.push_back(
                            position
                        );
                    }
                }
            }
        }

        /*
         * Add all query terms to Matched Terms.
         */
        std::unordered_set<std::string>
            unique_phrase_terms;

        for (
            const std::string& term :
            query_tokens
        ) {

            if (
                unique_phrase_terms.insert(term)
                    .second
            ) {

                result.matched_terms.push_back(
                    term
                );
            }
        }

        if (
            !result.positions.empty()
        ) {

            results.push_back(
                std::move(result)
            );
        }
    }

    /*
     * Sort by score and then document ID.
     */
    std::sort(
        results.begin(),
        results.end(),
        [](const SearchResult& a,
           const SearchResult& b) {

            if (a.score != b.score) {
                return a.score > b.score;
            }

            return
                a.document_id <
                b.document_id;
        }
    );

    /*
     * Apply Top-K.
     */
    if (results.size() > top_k) {

        results.resize(top_k);
    }

    return results;
}