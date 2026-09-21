#include "inverted_index.h"

#include <algorithm>
#include <cmath>

namespace {

// Orders by score (descending), then document id (ascending), so results are
// deterministic. Works for any type with `score` and `document_id` members.
template <typename T>
void keep_top_k(std::vector<T>& items, std::size_t k) {
    auto ranks_before = [](const T& a, const T& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.document_id < b.document_id;
    };
    if (items.size() > k) {
        std::partial_sort(items.begin(), items.begin() + static_cast<long>(k),
                          items.end(), ranks_before);
        items.resize(k);
    } else {
        std::sort(items.begin(), items.end(), ranks_before);
    }
}

// Sorted, de-duplicated, non-empty query terms.
std::vector<std::string> unique_terms(const std::vector<std::string>& tokens) {
    std::vector<std::string> terms;
    terms.reserve(tokens.size());
    for (const std::string& token : tokens) {
        if (!token.empty()) terms.push_back(token);
    }
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
    return terms;
}

}  // namespace

// ---------------------------------------------------------------- indexing

void InvertedIndex::remove_document_postings(int document_id) {
    for (auto it = index_.begin(); it != index_.end();) {
        it->second.erase(document_id);
        it = it->second.empty() ? index_.erase(it) : std::next(it);
    }
}

void InvertedIndex::add_document(int document_id,
                                 const std::vector<std::string>& tokens) {
    auto existing = document_lengths_.find(document_id);
    if (existing != document_lengths_.end()) {
        total_document_length_ -= static_cast<std::size_t>(existing->second);
        remove_document_postings(document_id);
    }

    document_lengths_[document_id] = static_cast<int>(tokens.size());
    total_document_length_ += tokens.size();

    for (std::size_t position = 0; position < tokens.size(); ++position) {
        index_[tokens[position]][document_id].push_back(
            static_cast<int>(position));
    }
}

int InvertedIndex::document_count() const {
    return static_cast<int>(document_lengths_.size());
}

std::vector<int> InvertedIndex::documents_containing(
    const std::string& term) const {
    std::vector<int> ids;
    auto it = index_.find(term);
    if (it == index_.end()) return ids;
    ids.reserve(it->second.size());
    for (const auto& entry : it->second) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int> InvertedIndex::all_document_ids() const {
    std::vector<int> ids;
    ids.reserve(document_lengths_.size());
    for (const auto& entry : document_lengths_) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ----------------------------------------------------------------- scoring

double InvertedIndex::average_document_length() const {
    if (document_lengths_.empty()) return 0.0;
    return static_cast<double>(total_document_length_) /
           static_cast<double>(document_lengths_.size());
}

// Lucene-style BM25 IDF: always positive, even for terms in every document.
double InvertedIndex::idf(std::size_t document_frequency) const {
    const double n = static_cast<double>(document_lengths_.size());
    const double df = static_cast<double>(document_frequency);
    return std::log(1.0 + (n - df + 0.5) / (df + 0.5));
}

double InvertedIndex::bm25_term_score(double idf, double term_frequency,
                                      double document_length,
                                      double average_length) {
    const double length_ratio =
        average_length > 0.0 ? document_length / average_length : 1.0;
    const double norm = 1.0 - kBm25B + kBm25B * length_ratio;
    return idf * (term_frequency * (kBm25K1 + 1.0)) /
           (term_frequency + kBm25K1 * norm);
}

// Selects the top_k documents first and only then materialises positions and
// matched terms, so the cost of building results is O(top_k), not O(matches).
std::vector<SearchResult> InvertedIndex::build_results(
    std::vector<ScoredDocument> scored, const std::vector<std::string>& terms,
    std::size_t top_k) const {
    keep_top_k(scored, top_k);

    std::vector<SearchResult> results;
    results.reserve(scored.size());
    for (const ScoredDocument& doc : scored) {
        SearchResult result;
        result.document_id = doc.document_id;
        result.score = doc.score;
        for (const std::string& term : terms) {
            auto term_it = index_.find(term);
            if (term_it == index_.end()) continue;
            auto doc_it = term_it->second.find(doc.document_id);
            if (doc_it == term_it->second.end()) continue;
            result.matched_terms.push_back(term);
            result.positions.insert(result.positions.end(),
                                    doc_it->second.begin(),
                                    doc_it->second.end());
        }
        std::sort(result.positions.begin(), result.positions.end());
        results.push_back(std::move(result));
    }
    return results;
}

// ------------------------------------------------------------- BM25 search

std::vector<SearchResult> InvertedIndex::search(
    const std::vector<std::string>& query_tokens, std::size_t top_k) const {
    if (top_k == 0 || document_lengths_.empty()) return {};

    const std::vector<std::string> terms = unique_terms(query_tokens);
    if (terms.empty()) return {};

    const double average_length = average_document_length();
    std::unordered_map<int, double> scores;

    for (const std::string& term : terms) {
        auto term_it = index_.find(term);
        if (term_it == index_.end()) continue;

        const double term_idf = idf(term_it->second.size());
        for (const auto& [document_id, positions] : term_it->second) {
            auto length_it = document_lengths_.find(document_id);
            if (length_it == document_lengths_.end()) continue;
            scores[document_id] += bm25_term_score(
                term_idf, static_cast<double>(positions.size()),
                static_cast<double>(length_it->second), average_length);
        }
    }

    std::vector<ScoredDocument> scored;
    scored.reserve(scores.size());
    for (const auto& [document_id, score] : scores) {
        scored.push_back({document_id, score});
    }
    return build_results(std::move(scored), terms, top_k);
}

std::vector<SearchResult> InvertedIndex::rank_candidates(
    const std::vector<std::string>& query_tokens,
    const std::vector<int>& candidate_ids, std::size_t top_k) const {
    if (top_k == 0 || candidate_ids.empty()) return {};

    const std::vector<std::string> terms = unique_terms(query_tokens);
    const double average_length = average_document_length();

    struct TermInfo {
        const PostingMap* postings;
        double idf;
    };
    std::vector<TermInfo> infos;
    for (const std::string& term : terms) {
        auto it = index_.find(term);
        if (it != index_.end()) {
            infos.push_back({&it->second, idf(it->second.size())});
        }
    }

    std::vector<ScoredDocument> scored;
    scored.reserve(candidate_ids.size());
    for (int document_id : candidate_ids) {
        auto length_it = document_lengths_.find(document_id);
        if (length_it == document_lengths_.end()) continue;

        double score = 0.0;
        for (const TermInfo& info : infos) {
            auto posting_it = info.postings->find(document_id);
            if (posting_it == info.postings->end()) continue;
            score += bm25_term_score(
                info.idf, static_cast<double>(posting_it->second.size()),
                static_cast<double>(length_it->second), average_length);
        }
        scored.push_back({document_id, score});
    }
    return build_results(std::move(scored), terms, top_k);
}

// ----------------------------------------------------------- phrase search

std::vector<int> InvertedIndex::phrase_starts(
    const std::vector<const PostingMap*>& lists, int document_id) {
    std::vector<const std::vector<int>*> positions;
    positions.reserve(lists.size());
    for (const PostingMap* list : lists) {
        auto it = list->find(document_id);
        if (it == list->end()) return {};
        positions.push_back(&it->second);
    }

    std::vector<int> starts;
    for (int start : *positions[0]) {
        bool matches = true;
        for (std::size_t offset = 1; offset < positions.size(); ++offset) {
            if (!std::binary_search(positions[offset]->begin(),
                                    positions[offset]->end(),
                                    start + static_cast<int>(offset))) {
                matches = false;
                break;
            }
        }
        if (matches) starts.push_back(start);
    }
    return starts;
}

std::vector<SearchResult> InvertedIndex::phrase_search(
    const std::vector<std::string>& query_tokens, std::size_t top_k) const {
    if (query_tokens.empty() || top_k == 0) return {};

    // Resolve every term once; start from the rarest term's documents.
    std::vector<const PostingMap*> lists;
    lists.reserve(query_tokens.size());
    std::size_t rarest = 0;
    for (std::size_t i = 0; i < query_tokens.size(); ++i) {
        auto it = index_.find(query_tokens[i]);
        if (it == index_.end()) return {};
        lists.push_back(&it->second);
        if (it->second.size() < lists[rarest]->size()) rarest = i;
    }

    const std::vector<std::string> terms = unique_terms(query_tokens);
    double idf_sum = 0.0;
    for (const std::string& term : terms) {
        idf_sum += idf(index_.find(term)->second.size());
    }

    const double average_length = average_document_length();
    std::vector<SearchResult> results;

    for (const auto& entry : *lists[rarest]) {
        const int document_id = entry.first;
        std::vector<int> starts = phrase_starts(lists, document_id);
        if (starts.empty()) continue;

        auto length_it = document_lengths_.find(document_id);
        if (length_it == document_lengths_.end()) continue;

        SearchResult result;
        result.document_id = document_id;
        // Phrase frequency plays the role of term frequency in BM25.
        result.score = bm25_term_score(
            idf_sum, static_cast<double>(starts.size()),
            static_cast<double>(length_it->second), average_length);
        result.positions = std::move(starts);
        result.matched_terms = terms;
        results.push_back(std::move(result));
    }

    keep_top_k(results, top_k);
    return results;
}
