#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "tokenizer.h"

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

int main() {
    std::cout << "Atlas Retrieval Engine - Core Tests\n";
    std::cout << "===================================\n\n";

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

    std::cout << "\n===================================\n";
    std::cout << "All tests passed.\n";

    return 0;
}