// Atlas Retrieval Engine - test suite.
//
// This file deliberately does NOT use assert(): assert() compiles to nothing
// when NDEBUG is defined (the default for CMake Release builds), which would
// turn every test into a silent pass. CHECK always runs, and main() returns a
// non-zero exit code if any check failed so CTest / CI can see it.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "tokenizer.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::cerr << __FILE__ << ":" << __LINE__                        \
                      << ": CHECK failed: " #condition "\n";                \
        }                                                                   \
    } while (0)

#define RUN_TEST(test)                                                      \
    do {                                                                    \
        const int failures_before = g_failures;                             \
        test();                                                             \
        std::cout << (g_failures == failures_before ? "[PASS] " : "[FAIL] ") \
                  << #test << "\n";                                         \
    } while (0)

using Ids = std::vector<int>;
using Tokens = std::vector<std::string>;

// Document ids in result (ranking) order.
Ids ranked_ids(const std::vector<SearchResult>& results) {
    Ids ids;
    for (const SearchResult& r : results) ids.push_back(r.document_id);
    return ids;
}

// Document ids sorted ascending (for set-style comparisons).
Ids id_set(const std::vector<SearchResult>& results) {
    Ids ids = ranked_ids(results);
    std::sort(ids.begin(), ids.end());
    return ids;
}

struct Fixture {
    Tokenizer tokenizer;
    InvertedIndex index;

    void add(int id, const std::string& text) {
        index.add_document(id, tokenizer.tokenize(text));
    }
    QueryProcessor processor() const { return QueryProcessor(tokenizer, index); }
};

// The three-document collection from the original test suite.
void fill_basic(Fixture& f) {
    f.add(1, "vector search is an important technique");
    f.add(2, "information retrieval systems use search");
    f.add(3, "vector databases support vector search");
}

// Four documents that make NOT semantics easy to reason about.
void fill_boolean(Fixture& f) {
    f.add(1, "vector search");
    f.add(2, "database storage");
    f.add(3, "vector database");
    f.add(4, "network routing");
}

void fill_random(Fixture& f, unsigned seed, int documents, int vocabulary,
                 int length) {
    std::mt19937 rng(seed);
    for (int d = 1; d <= documents; ++d) {
        Tokens tokens;
        for (int i = 0; i < length; ++i) {
            tokens.push_back(
                "t" + std::to_string(rng() % static_cast<unsigned>(vocabulary)));
        }
        f.index.add_document(d, tokens);
    }
}

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("atlas_test_" + name))
        .string();
}

std::vector<unsigned char> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

void write_file(const std::string& path, const std::vector<unsigned char>& b) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()),
              static_cast<std::streamsize>(b.size()));
}

// Independent copy of the on-disk checksum, used to craft files that pass the
// integrity check but contain hostile values, so the bounds checks themselves
// are exercised.
std::uint64_t fnv1a(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char b : bytes) {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct Crafted {
    std::vector<unsigned char> bytes{'A', 'T', 'L', 'S'};
    Crafted& u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
        return *this;
    }
    Crafted& raw(const std::string& s) {
        bytes.insert(bytes.end(), s.begin(), s.end());
        return *this;
    }
    void save_with_checksum(const std::string& path) const {
        std::vector<unsigned char> out = bytes;
        const std::uint64_t sum = fnv1a(bytes);
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<unsigned char>((sum >> (8 * i)) & 0xFF));
        write_file(path, out);
    }
};

// ---------------------------------------------------------------- tokenizer

void test_tokenizer_basic() {
    Tokenizer t;
    CHECK((t.tokenize("Vector Search is FAST!") ==
           Tokens{"vector", "search", "is", "fast"}));
    CHECK(t.tokenize("").empty());
    CHECK(t.tokenize("  \t\n  ").empty());
    CHECK(t.tokenize("!!! --- ...").empty());
}

// Regression: punctuation used to be deleted, gluing words together
// ("state-of-the-art" -> "stateoftheart").
void test_tokenizer_punctuation_splits_words() {
    Tokenizer t;
    CHECK((t.tokenize("state-of-the-art") == Tokens{"state", "of", "the", "art"}));
    CHECK((t.tokenize("hello,world") == Tokens{"hello", "world"}));
    CHECK((t.tokenize("end.Start") == Tokens{"end", "start"}));
    CHECK((t.tokenize("C++ and C#") == Tokens{"c", "and", "c"}));
    CHECK((t.tokenize("don't") == Tokens{"dont"}));
    CHECK((t.tokenize("'quoted'") == Tokens{"quoted"}));
    CHECK((t.tokenize("rock 'n' roll") == Tokens{"rock", "n", "roll"}));
}

// --------------------------------------------------------- basic ranked search

void test_document_count_and_empty_index() {
    Fixture f;
    fill_basic(f);
    CHECK(f.index.document_count() == 3);

    Fixture empty;
    CHECK(empty.index.document_count() == 0);
    CHECK(empty.processor().search("vector").empty());
    CHECK(empty.processor().phrase_search("vector").empty());
    CHECK(empty.processor().boolean_search("vector").empty());
}

void test_normal_search() {
    Fixture f;
    fill_basic(f);
    auto results = f.processor().search("vector");
    CHECK((id_set(results) == Ids{1, 3}));
    CHECK(results.size() == 2);
    if (results.size() == 2) {
        // Doc 3 has "vector" twice in a similar-length document: ranks first.
        CHECK(results[0].document_id == 3);
        CHECK(results[0].score > results[1].score);
        CHECK((results[0].positions == std::vector<int>{0, 3}));
        CHECK((results[0].matched_terms == Tokens{"vector"}));
    }
}

void test_search_edge_cases() {
    Fixture f;
    fill_basic(f);
    QueryProcessor q = f.processor();
    CHECK(q.search("").empty());
    CHECK(q.search("quantum").empty());
    CHECK(q.search("vector", 0).empty());
    CHECK(q.search("vector", 100).size() == 2);
    // Repeating a query term must not change ranking.
    CHECK((ranked_ids(q.search("vector vector vector search")) ==
           ranked_ids(q.search("vector search"))));
}

// ------------------------------------------------------------ phrase search

void test_phrase_search() {
    Fixture f;
    fill_basic(f);
    QueryProcessor q = f.processor();
    // Docs 1 ("vector search is ...") and 3 ("... vector search") both contain
    // the phrase; doc 2 has "search" but not "vector".
    auto hits = q.phrase_search("vector search");
    CHECK((id_set(hits) == Ids{1, 3}));
    for (const SearchResult& hit : hits) {
        if (hit.document_id == 1) CHECK((hit.positions == std::vector<int>{0}));
        if (hit.document_id == 3) CHECK((hit.positions == std::vector<int>{3}));
    }
    CHECK(q.phrase_search("vector information").empty());
    CHECK(q.phrase_search("search vector").empty());  // order matters
    CHECK((id_set(q.phrase_search("vector")) == Ids{1, 3}));
    CHECK(q.phrase_search("vector search", 0).empty());
}

// Regression: every matching document used to get the identical score
// (sum of IDFs), so phrase results were ordered by document id only.
void test_phrase_ranking_uses_frequency_and_length() {
    Fixture f;
    f.add(1, "vector search");
    std::string long_doc = "vector search";
    for (int i = 0; i < 500; ++i) long_doc += " filler" + std::to_string(i);
    f.add(2, long_doc);
    f.add(3, "vector search vector search vector search");
    f.add(4, "unrelated words entirely");

    auto results = f.processor().phrase_search("vector search");
    CHECK((ranked_ids(results) == Ids{3, 1, 2}));
    CHECK(results.size() == 3);
    if (results.size() == 3) {
        CHECK(results[0].score > results[1].score);
        CHECK(results[1].score > results[2].score);
    }
}

// ------------------------------------------------------------- re-indexing

// Regression: re-adding a document used to keep its old postings and merge old
// and new positions into one (unsorted) list.
void test_reindex_replaces_old_content() {
    Fixture f;
    f.add(1, "vector search");
    f.add(2, "unrelated text");
    f.add(1, "database storage");

    QueryProcessor q = f.processor();
    CHECK(f.index.document_count() == 2);
    CHECK(q.search("vector").empty());
    CHECK((id_set(q.search("database")) == Ids{1}));
    CHECK(q.phrase_search("vector search").empty());
    CHECK((id_set(q.phrase_search("database storage")) == Ids{1}));
}

void test_reindex_keeps_positions_sorted_for_phrases() {
    Fixture f;
    f.add(1, "alpha beta");
    f.add(1, "beta alpha");
    QueryProcessor q = f.processor();
    CHECK(q.phrase_search("alpha beta").empty());
    auto hit = q.phrase_search("beta alpha");
    CHECK(hit.size() == 1);
    if (hit.size() == 1) CHECK((hit[0].positions == std::vector<int>{0}));
    auto alpha = q.search("alpha");
    CHECK(alpha.size() == 1);
    if (alpha.size() == 1) CHECK((alpha[0].positions == std::vector<int>{1}));
}

// ----------------------------------------------------------------- Boolean

void test_boolean_operators() {
    Fixture f;
    fill_basic(f);
    QueryProcessor q = f.processor();
    CHECK((id_set(q.boolean_search("vector AND search")) == Ids{1, 3}));
    CHECK((id_set(q.boolean_search("vector OR retrieval")) == Ids{1, 2, 3}));
    CHECK((id_set(q.boolean_search("vector NOT databases")) == Ids{1}));
    CHECK((id_set(q.boolean_search("vector AND (search OR retrieval)")) ==
           Ids{1, 3}));
    CHECK(q.boolean_search("vector AND quantum").empty());
    // AND binds tighter than OR: vector OR (retrieval AND search) -> 1, 2, 3
    CHECK((id_set(q.boolean_search("vector OR retrieval AND search")) ==
           Ids{1, 2, 3}));
    // ... whereas parentheses change the answer.
    CHECK((id_set(q.boolean_search("(vector OR retrieval) AND databases")) ==
           Ids{3}));
    CHECK((id_set(q.boolean_search("vector and search")) == Ids{1, 3}));
}

// Regression: NOT used to be evaluated only against documents that contained a
// query term, so "NOT x" returned nothing and "a OR NOT b" missed documents.
void test_boolean_not_is_evaluated_against_whole_collection() {
    Fixture f;
    fill_boolean(f);
    QueryProcessor q = f.processor();
    CHECK((id_set(q.boolean_search("NOT database")) == Ids{1, 4}));
    CHECK((id_set(q.boolean_search("vector OR NOT database")) == Ids{1, 3, 4}));
    CHECK((id_set(q.boolean_search("vector AND NOT database")) == Ids{1}));
    CHECK((id_set(q.boolean_search("NOT database AND vector")) == Ids{1}));
    CHECK((id_set(q.boolean_search("NOT NOT vector")) == Ids{1, 3}));
    CHECK((id_set(q.boolean_search("NOT (vector OR database)")) == Ids{4}));
    CHECK((id_set(q.boolean_search("(vector OR database) AND NOT search")) ==
           Ids{2, 3}));
    // De Morgan: NOT (a AND b) == (NOT a) OR (NOT b)
    CHECK((id_set(q.boolean_search("NOT (vector AND database)")) ==
           id_set(q.boolean_search("(NOT vector) OR (NOT database)"))));
}

// Regression: malformed queries used to return "no results" with no hint why.
void test_boolean_reports_errors() {
    Fixture f;
    fill_boolean(f);
    QueryProcessor q = f.processor();
    std::string error;

    for (const char* bad :
         {"vector search", "vector AND (search", "((vector)", "AND vector",
          "vector AND", "()", ")vector(", "vector OR OR search", "   ", ""}) {
        error.clear();
        CHECK(q.boolean_search(bad, 10, &error).empty());
        CHECK(!error.empty());
    }

    CHECK(!q.boolean_search("vector", 10, &error).empty());
    CHECK(error.empty());  // cleared on success
}

// Regression: ~20,000 nested parentheses used to overflow the stack (SIGSEGV).
void test_boolean_hostile_input_does_not_crash() {
    Fixture f;
    fill_boolean(f);
    QueryProcessor q = f.processor();
    std::string error;

    const std::string deep_parens =
        std::string(200000, '(') + "vector" + std::string(200000, ')');
    CHECK(q.boolean_search(deep_parens, 10, &error).empty());
    CHECK(error.find("too long") != std::string::npos);

    std::string long_chain = "vector";
    for (int i = 0; i < 100000; ++i) long_chain += " AND vector";
    error.clear();
    CHECK(q.boolean_search(long_chain, 10, &error).empty());
    CHECK(error.find("too long") != std::string::npos);

    std::string nots;
    for (int i = 0; i < 100000; ++i) nots += "NOT ";
    error.clear();
    CHECK(q.boolean_search(nots + "vector", 10, &error).empty());
    CHECK(error.find("too long") != std::string::npos);

    // Legitimate deep nesting below the limit still works.
    const std::string ok =
        std::string(200, '(') + "vector" + std::string(200, ')');
    CHECK((id_set(q.boolean_search(ok)) == Ids{1, 3}));
}

// Regression: the Boolean tokenizer used to glue hyphenated words together.
void test_boolean_hyphenated_words() {
    Fixture f;
    f.add(1, "a state of the art design");
    f.add(2, "state of the union");
    QueryProcessor q = f.processor();
    CHECK((id_set(q.boolean_search("state-of-the-art")) == Ids{1}));
    CHECK((id_set(q.boolean_search("design AND state-of-the-art")) == Ids{1}));
}

// Boolean ranking must agree with plain BM25 when the query is a pure OR.
void test_boolean_ranking_matches_bm25() {
    Fixture f;
    fill_random(f, 7, 300, 30, 20);
    QueryProcessor q = f.processor();
    auto bm25 = q.search("t1 t2 t3", 25);
    auto boolean = q.boolean_search("t1 OR t2 OR t3", 25);
    CHECK(!bm25.empty());
    CHECK((ranked_ids(bm25) == ranked_ids(boolean)));
    for (std::size_t i = 0; i < bm25.size() && i < boolean.size(); ++i) {
        CHECK(bm25[i].score == boolean[i].score);
    }
}

void test_boolean_and_matches_posting_intersection() {
    Fixture f;
    fill_random(f, 11, 400, 25, 15);
    QueryProcessor q = f.processor();
    Ids a = f.index.documents_containing("t3");
    Ids b = f.index.documents_containing("t9");
    Ids expected;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::back_inserter(expected));
    CHECK(!expected.empty());
    CHECK((id_set(q.boolean_search("t3 AND t9", 100000)) == expected));
}

// ----------------------------------------------------------- top-K / ranking

void test_top_k_is_a_prefix_of_the_full_ranking() {
    Fixture f;
    fill_random(f, 42, 500, 40, 25);
    QueryProcessor q = f.processor();
    for (const char* query : {"t1", "t1 t2", "t5 t6 t7", "t0 t39"}) {
        auto full = q.search(query, 100000);
        for (std::size_t k : {1u, 3u, 10u, 50u}) {
            auto top = q.search(query, k);
            CHECK(top.size() == std::min<std::size_t>(k, full.size()));
            for (std::size_t i = 0; i < top.size() && i < full.size(); ++i) {
                CHECK(top[i].document_id == full[i].document_id);
                CHECK(top[i].score == full[i].score);
            }
        }
        for (std::size_t i = 1; i < full.size(); ++i) {  // strictly ordered
            CHECK(full[i - 1].score > full[i].score ||
                  (full[i - 1].score == full[i].score &&
                   full[i - 1].document_id < full[i].document_id));
        }
    }
}

// ----------------------------------------------------------- serialization

void test_serialization_round_trip_and_determinism() {
    Fixture f;
    fill_random(f, 5, 200, 30, 20);
    const std::string p1 = temp_path("rt1.index");
    const std::string p2 = temp_path("rt2.index");
    const std::string p3 = temp_path("rt3.index");
    std::string error;

    CHECK(IndexSerializer::save(f.index, p1, &error));
    CHECK(IndexSerializer::save(f.index, p2, &error));
    CHECK(read_file(p1) == read_file(p2));  // deterministic output

    InvertedIndex loaded;
    CHECK(IndexSerializer::load(loaded, p1, &error));
    CHECK(loaded.document_count() == f.index.document_count());

    for (const char* query : {"t1", "t2 t3", "t10 t11 t12"}) {
        auto before = f.index.search(Tokenizer().tokenize(query), 20);
        auto after = loaded.search(Tokenizer().tokenize(query), 20);
        CHECK(before.size() == after.size());
        for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
            CHECK(before[i].document_id == after[i].document_id);
            CHECK(before[i].score == after[i].score);
            CHECK(before[i].positions == after[i].positions);
        }
    }

    CHECK(IndexSerializer::save(loaded, p3, &error));
    CHECK(read_file(p1) == read_file(p3));  // save(load(x)) == x

    CHECK(!std::filesystem::exists(p1 + ".tmp"));  // no leftover temp file
    for (const auto& p : {p1, p2, p3}) std::filesystem::remove(p);
}

void test_serialization_empty_index() {
    InvertedIndex empty;
    const std::string path = temp_path("empty.index");
    CHECK(IndexSerializer::save(empty, path));
    InvertedIndex loaded;
    CHECK(IndexSerializer::load(loaded, path));
    CHECK(loaded.document_count() == 0);
    std::filesystem::remove(path);
}

// Regression: a failed load used to wipe the index it was loading into, and a
// hostile length field could trigger a multi-gigabyte allocation.
void test_serialization_rejects_bad_files_and_keeps_existing_index() {
    Fixture good;
    fill_basic(good);
    const std::string path = temp_path("bad.index");
    std::string error;
    CHECK(IndexSerializer::save(good.index, path));
    const std::vector<unsigned char> valid = read_file(path);

    Fixture victim;  // 4 docs; must survive every failed load
    fill_boolean(victim);
    auto still_intact = [&]() {
        return victim.index.document_count() == 4 &&
               !victim.processor().search("routing").empty();
    };

    CHECK(!IndexSerializer::load(victim.index, temp_path("does_not_exist"),
                                 &error));
    CHECK(!error.empty());
    CHECK(still_intact());

    std::vector<unsigned char> truncated(
        valid.begin(), valid.begin() + static_cast<long>(valid.size() / 2));
    write_file(path, truncated);
    CHECK(!IndexSerializer::load(victim.index, path, &error));
    CHECK(still_intact());

    std::vector<unsigned char> flipped = valid;
    flipped[flipped.size() / 2] ^= 0x40;
    write_file(path, flipped);
    CHECK(!IndexSerializer::load(victim.index, path, &error));
    CHECK(error.find("checksum") != std::string::npos);
    CHECK(still_intact());

    write_file(path, {});
    CHECK(!IndexSerializer::load(victim.index, path, &error));
    CHECK(still_intact());

    // Valid checksum + hostile term length (~4 GB). Must be rejected by the
    // bounds check, not by running out of memory.
    Crafted huge_term;
    huge_term.u32(2).u32(0).u32(1).u32(0xFFFFFFF0u).u32(0);
    huge_term.save_with_checksum(path);
    CHECK(!IndexSerializer::load(victim.index, path, &error));
    CHECK(error.find("term length") != std::string::npos);
    CHECK(still_intact());

    // Valid checksum + position outside the document's length.
    Crafted bad_position;
    bad_position.u32(2).u32(1).u32(1).u32(2)   // 1 doc: id 1, length 2
        .u32(1).u32(1).raw("a").u32(1)         // 1 term "a", 1 posting
        .u32(1).u32(1).u32(5);                 // doc 1, 1 position: 5 (>= 2)
    bad_position.save_with_checksum(path);
    CHECK(!IndexSerializer::load(victim.index, path, &error));
    CHECK(error.find("out of range") != std::string::npos);
    CHECK(still_intact());

    // Sanity: the untouched valid file still loads.
    write_file(path, valid);
    CHECK(IndexSerializer::load(victim.index, path, &error));
    CHECK(victim.index.document_count() == 3);
    std::filesystem::remove(path);
}

// -------------------------------------------------------------- concurrency

// Regression: const search() used to mutate a shared cache (data race). This
// test is only conclusive under ThreadSanitizer (see the CI "tsan" job), but it
// also checks that concurrent results equal single-threaded ones.
void test_concurrent_searches_are_safe_and_consistent() {
    Fixture f;
    fill_random(f, 99, 300, 30, 20);
    QueryProcessor q = f.processor();
    const auto expected_bm25 = ranked_ids(q.search("t1 t2", 10));
    const auto expected_phrase = ranked_ids(q.phrase_search("t1 t2", 10));
    const auto expected_bool =
        ranked_ids(q.boolean_search("t1 AND NOT t2", 10));

    std::vector<int> mismatches(4, 0);
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < mismatches.size(); ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; ++i) {
                if (ranked_ids(q.search("t1 t2", 10)) != expected_bm25)
                    ++mismatches[t];
                if (ranked_ids(q.phrase_search("t1 t2", 10)) != expected_phrase)
                    ++mismatches[t];
                if (ranked_ids(q.boolean_search("t1 AND NOT t2", 10)) !=
                    expected_bool)
                    ++mismatches[t];
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
    for (int m : mismatches) CHECK(m == 0);
}

}  // namespace

int main() {
    std::cout << "Atlas Retrieval Engine - tests\n"
              << "==============================\n";

    RUN_TEST(test_tokenizer_basic);
    RUN_TEST(test_tokenizer_punctuation_splits_words);
    RUN_TEST(test_document_count_and_empty_index);
    RUN_TEST(test_normal_search);
    RUN_TEST(test_search_edge_cases);
    RUN_TEST(test_phrase_search);
    RUN_TEST(test_phrase_ranking_uses_frequency_and_length);
    RUN_TEST(test_reindex_replaces_old_content);
    RUN_TEST(test_reindex_keeps_positions_sorted_for_phrases);
    RUN_TEST(test_boolean_operators);
    RUN_TEST(test_boolean_not_is_evaluated_against_whole_collection);
    RUN_TEST(test_boolean_reports_errors);
    RUN_TEST(test_boolean_hostile_input_does_not_crash);
    RUN_TEST(test_boolean_hyphenated_words);
    RUN_TEST(test_boolean_ranking_matches_bm25);
    RUN_TEST(test_boolean_and_matches_posting_intersection);
    RUN_TEST(test_top_k_is_a_prefix_of_the_full_ranking);
    RUN_TEST(test_serialization_round_trip_and_determinism);
    RUN_TEST(test_serialization_empty_index);
    RUN_TEST(test_serialization_rejects_bad_files_and_keeps_existing_index);
    RUN_TEST(test_concurrent_searches_are_safe_and_consistent);

    std::cout << "==============================\n"
              << g_checks << " checks, " << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
