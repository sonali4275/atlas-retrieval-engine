#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "inverted_index.h"
#include "tokenizer.h"

struct BenchmarkDocument {
    int id;
    std::string text;
};

int main() {

    std::cout << "Atlas Retrieval Engine - Benchmark\n";
    std::cout << "==================================\n\n";

    Tokenizer tokenizer;
    InvertedIndex index;

    const int document_count = 10000;

    std::vector<BenchmarkDocument> documents;
    documents.reserve(document_count);

    /*
     * ============================================================
     * GENERATE REALISTIC SYNTHETIC CORPUS
     * ============================================================
     *
     * Different documents contain different combinations
     * of technical terms.
     *
     * This gives BM25 meaningful score differences.
     */

    for (int i = 1; i <= document_count; ++i) {

        BenchmarkDocument document;
        document.id = i;

        /*
         * Every document contains some general
         * technical vocabulary.
         */
        document.text =
            "technical system architecture "
            "software engineering distributed computing "
            "performance reliability scalability ";

        /*
         * Documents are deliberately varied.
         */

        if (i % 2 == 0) {

            document.text +=
                "vector search indexing "
                "similarity retrieval ";

        }

        if (i % 3 == 0) {

            document.text +=
                "database storage query processing ";

        }

        if (i % 5 == 0) {

            document.text +=
                "search engine ranking "
                "document retrieval ";

        }

        if (i % 7 == 0) {

            document.text +=
                "machine learning embeddings "
                "vector database ";

        }

        if (i % 11 == 0) {

            document.text +=
                "distributed search "
                "index optimization ";

        }

        /*
         * Make some documents longer.
         */

        if (i % 13 == 0) {

            document.text +=
                "performance optimization "
                "memory management "
                "concurrent processing "
                "query execution "
                "system monitoring ";
        }

        document.text +=
            "document number " +
            std::to_string(i);

        documents.push_back(document);
    }


    /*
     * ============================================================
     * INDEXING BENCHMARK
     * ============================================================
     */

    auto indexing_start =
        std::chrono::high_resolution_clock::now();

    std::size_t total_tokens = 0;

    for (const auto& document : documents) {

        std::vector<std::string> tokens =
            tokenizer.tokenize(document.text);

        total_tokens += tokens.size();

        index.add_document(
            document.id,
            tokens
        );
    }

    auto indexing_end =
        std::chrono::high_resolution_clock::now();

    const std::chrono::duration<double, std::milli>
        indexing_time =
            indexing_end - indexing_start;


    /*
     * ============================================================
     * SEARCH BENCHMARK
     * ============================================================
     */

    const std::string query =
        "vector search";

    std::vector<std::string> query_tokens =
        tokenizer.tokenize(query);


    auto search_start =
        std::chrono::high_resolution_clock::now();

    std::vector<SearchResult> results =
        index.search(
            query_tokens,
            10
        );

    auto search_end =
        std::chrono::high_resolution_clock::now();


    const std::chrono::duration<double, std::milli>
        search_time =
            search_end - search_start;


    /*
     * ============================================================
     * OUTPUT
     * ============================================================
     */

    std::cout
        << "Benchmark configuration\n";

    std::cout
        << "-----------------------\n";

    std::cout
        << "Documents: "
        << index.document_count()
        << "\n";

    std::cout
        << "Total tokens: "
        << total_tokens
        << "\n";

    std::cout
        << "Query: "
        << query
        << "\n";

    std::cout
        << "Top-K: 10\n\n";


    std::cout
        << "Benchmark results\n";

    std::cout
        << "-----------------\n";

    std::cout
        << std::fixed
        << std::setprecision(4);

    std::cout
        << "Indexing time: "
        << indexing_time.count()
        << " ms\n";

    std::cout
        << "Search time: "
        << search_time.count()
        << " ms\n";

    std::cout
        << "Results returned: "
        << results.size()
        << "\n\n";


    /*
     * ============================================================
     * TOP-K RESULTS
     * ============================================================
     */

    std::cout
        << "Top results\n";

    std::cout
        << "-----------\n";

    for (const auto& result : results) {

        std::cout
            << "Document ID: "
            << result.document_id
            << " | Score: "
            << result.score
            << "\n";
    }


    return 0;
}