#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "tokenizer.h"


// ============================================================
// TEST HELPERS
// ============================================================

InvertedIndex create_test_index() {
    Tokenizer tokenizer;
    InvertedIndex index;

    index.add_document(
        1,
        tokenizer.tokenize(
            "vector search is an important technique"
        )
    );

    index.add_document(
        2,
        tokenizer.tokenize(
            "information retrieval systems use search"
        )
    );

    index.add_document(
        3,
        tokenizer.tokenize(
            "vector databases support vector search"
        )
    );

    return index;
}


// ============================================================
// BASIC TESTS
// ============================================================

void test_tokenizer() {
    Tokenizer tokenizer;

    const auto tokens =
        tokenizer.tokenize("Vector Search is FAST!");

    assert(tokens.size() == 4);
    assert(tokens[0] == "vector");
    assert(tokens[1] == "search");
    assert(tokens[2] == "is");
    assert(tokens[3] == "fast");

    std::cout << "[PASS] Tokenizer\n";
}


void test_document_count() {
    InvertedIndex index = create_test_index();

    assert(index.document_count() == 3);

    std::cout << "[PASS] Document count\n";
}


void test_normal_search() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.search("vector");

    assert(!results.empty());

    std::cout << "[PASS] Normal search\n";
}


void test_phrase_search() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.phrase_search("vector search");

    assert(!results.empty());

    // Document 3 contains the exact phrase.
    bool found_document_3 = false;

    for (const auto& result : results) {

        if (result.document_id == 3) {
            found_document_3 = true;
            break;
        }
    }

    assert(found_document_3);

    std::cout << "[PASS] Phrase search\n";
}


void test_boolean_and() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.boolean_search("vector AND search");

    assert(!results.empty());

    // Documents 1 and 3 contain both terms.
    bool found_1 = false;
    bool found_3 = false;

    for (const auto& result : results) {

        if (result.document_id == 1) {
            found_1 = true;
        }

        if (result.document_id == 3) {
            found_3 = true;
        }
    }

    assert(found_1);
    assert(found_3);

    std::cout << "[PASS] Boolean AND\n";
}


void test_boolean_or() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.boolean_search("vector OR retrieval");

    assert(!results.empty());

    std::cout << "[PASS] Boolean OR\n";
}


void test_boolean_not() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.boolean_search("vector NOT databases");

    assert(!results.empty());

    // Document 1 contains vector but not databases.
    bool found_document_1 = false;

    for (const auto& result : results) {

        if (result.document_id == 1) {
            found_document_1 = true;
        }

        // Document 3 contains databases and must be excluded.
        assert(result.document_id != 3);
    }

    assert(found_document_1);

    std::cout << "[PASS] Boolean NOT\n";
}


void test_boolean_parentheses() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.boolean_search(
            "vector AND (search OR retrieval)"
        );

    assert(!results.empty());

    std::cout << "[PASS] Boolean parentheses\n";
}


void test_boolean_precedence() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.boolean_search(
            "vector OR retrieval AND search"
        );

    assert(!results.empty());

    std::cout << "[PASS] Boolean precedence\n";
}


void test_index_serialization() {
    Tokenizer tokenizer;

    InvertedIndex original;

    original.add_document(
        1,
        tokenizer.tokenize(
            "vector search engine"
        )
    );

    original.add_document(
        2,
        tokenizer.tokenize(
            "information retrieval"
        )
    );

    const std::string filename =
        "test_atlas.index";

    assert(
        IndexSerializer::save(
            original,
            filename
        )
    );

    InvertedIndex loaded;

    assert(
        IndexSerializer::load(
            loaded,
            filename
        )
    );

    assert(
        loaded.document_count() ==
        original.document_count()
    );

    QueryProcessor processor(
        tokenizer,
        loaded
    );

    const auto results =
        processor.search("vector");

    assert(!results.empty());

    std::remove(filename.c_str());

    std::cout << "[PASS] Index serialization\n";
}


// ============================================================
// EDGE CASE TESTS
// ============================================================

void test_empty_query() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.search("");

    assert(results.empty());

    std::cout << "[PASS] Empty query\n";
}


void test_unknown_term() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.search("quantum");

    assert(results.empty());

    std::cout << "[PASS] Unknown term\n";
}


void test_zero_top_k() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.search("vector", 0);

    assert(results.empty());

    std::cout << "[PASS] Zero Top-K\n";
}


void test_large_top_k() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.search("vector", 100);

    // There are only three documents in the test index.
    assert(results.size() <= 3);

    assert(!results.empty());

    std::cout << "[PASS] Large Top-K\n";
}


void test_nonexistent_phrase() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.phrase_search(
            "vector information"
        );

    assert(results.empty());

    std::cout << "[PASS] Non-existent phrase\n";
}


void test_single_word_phrase() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(tokenizer, index);

    const auto results =
        processor.phrase_search("vector");

    assert(!results.empty());

    std::cout << "[PASS] Single-word phrase\n";
}


void test_reindex_document() {
    Tokenizer tokenizer;
    InvertedIndex index;

    index.add_document(
        1,
        tokenizer.tokenize(
            "vector search"
        )
    );

    assert(index.document_count() == 1);

    // Re-index the same document with different content.
    index.add_document(
        1,
        tokenizer.tokenize(
            "database storage"
        )
    );

    // The document count must remain one.
    assert(index.document_count() == 1);

    QueryProcessor processor(
        tokenizer,
        index
    );

    const auto old_results =
        processor.search("vector");

    const auto new_results =
        processor.search("database");

    // The old content should no longer be searchable.
    assert(old_results.empty());

    // The new content should be searchable.
    assert(!new_results.empty());

    std::cout << "[PASS] Document re-indexing\n";
}


void test_empty_index() {
    Tokenizer tokenizer;
    InvertedIndex index;

    QueryProcessor processor(
        tokenizer,
        index
    );

    assert(index.document_count() == 0);

    const auto results =
        processor.search("vector");

    assert(results.empty());

    std::cout << "[PASS] Empty index\n";
}


void test_boolean_unknown_term() {
    Tokenizer tokenizer;
    InvertedIndex index = create_test_index();

    QueryProcessor processor(
        tokenizer,
        index
    );

    const auto results =
        processor.boolean_search(
            "vector AND quantum"
        );

    assert(results.empty());

    std::cout << "[PASS] Boolean unknown term\n";
}


void test_serialization_empty_index() {
    InvertedIndex original;

    const std::string filename =
        "test_empty_atlas.index";

    assert(
        IndexSerializer::save(
            original,
            filename
        )
    );

    InvertedIndex loaded;

    assert(
        IndexSerializer::load(
            loaded,
            filename
        )
    );

    assert(
        loaded.document_count() == 0
    );

    std::remove(filename.c_str());

    std::cout
        << "[PASS] Empty index serialization\n";
}


// ============================================================
// MAIN
// ============================================================

int main() {

    std::cout
        << "Atlas Retrieval Engine - Core Tests\n";

    std::cout
        << "===================================\n\n";


    // --------------------------------------------------------
    // Existing core tests
    // --------------------------------------------------------

    test_tokenizer();
    test_document_count();
    test_normal_search();
    test_phrase_search();

    test_boolean_and();
    test_boolean_or();
    test_boolean_not();

    test_boolean_parentheses();
    test_boolean_precedence();

    test_index_serialization();


    // --------------------------------------------------------
    // Edge-case tests
    // --------------------------------------------------------

    test_empty_query();
    test_unknown_term();
    test_zero_top_k();
    test_large_top_k();

    test_nonexistent_phrase();
    test_single_word_phrase();

    test_reindex_document();

    test_empty_index();

    test_boolean_unknown_term();

    test_serialization_empty_index();


    // --------------------------------------------------------
    // Final result
    // --------------------------------------------------------

    std::cout
        << "\n===================================\n";

    std::cout
        << "All tests passed.\n";


    return 0;
}