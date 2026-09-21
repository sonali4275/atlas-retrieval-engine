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

struct QueryBenchmarkResult {
    std::string query;
    double search_time_ms;
    std::size_t results_returned;
};

int main() {

    std::cout << "Atlas Retrieval Engine - Benchmark\n";
    std::cout << "==================================\n\n";

    Tokenizer tokenizer;
    InvertedIndex index;

    const int document_count = 10000;
    const std::size_t top_k = 10;

    std::vector<BenchmarkDocument> documents;
    documents.reserve(document_count);

    /*
     * ============================================================
     * GENERATE SYNTHETIC CORPUS
     * ============================================================
     */

    for (int i = 1; i <= document_count; ++i) {

        BenchmarkDocument document;
        document.id = i;

        document.text =
            "technical system architecture "
            "software engineering distributed computing "
            "performance reliability scalability ";

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
     * MULTI-QUERY SEARCH BENCHMARK
     * ============================================================
     */

    const std::vector<std::string> queries = {
        "vector search",
        "retrieval search",
        "database",
        "machine learning",
        "index optimization"
    };

    std::vector<QueryBenchmarkResult> benchmark_results;

    benchmark_results.reserve(queries.size());

    for (const auto& query : queries) {

        std::vector<std::string> query_tokens =
            tokenizer.tokenize(query);

        auto search_start =
            std::chrono::high_resolution_clock::now();

        std::vector<SearchResult> results =
            index.search(
                query_tokens,
                top_k
            );

        auto search_end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            search_time =
                search_end - search_start;

        benchmark_results.push_back(
            {
                query,
                search_time.count(),
                results.size()
            }
        );
    }


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
        << "Top-K: "
        << top_k
        << "\n\n";


    std::cout
        << "Indexing benchmark\n";

    std::cout
        << "------------------\n";

    std::cout
        << std::fixed
        << std::setprecision(4);

    std::cout
        << "Indexing time: "
        << indexing_time.count()
        << " ms\n\n";


    /*
     * ============================================================
     * QUERY RESULTS
     * ============================================================
     */

    std::cout
        << "Query benchmarks\n";

    std::cout
        << "----------------\n";

    for (const auto& result : benchmark_results) {

        std::cout
            << "Query: "
            << result.query
            << "\n";

        std::cout
            << "Search time: "
            << result.search_time_ms
            << " ms\n";

        std::cout
            << "Results returned: "
            << result.results_returned
            << "\n\n";
    }


    /*
     * ============================================================
     * TOP RESULTS FOR PRIMARY QUERY
     * ============================================================
     */

    const std::string primary_query =
        "vector search";

    std::vector<std::string> primary_tokens =
        tokenizer.tokenize(primary_query);

    std::vector<SearchResult> primary_results =
        index.search(
            primary_tokens,
            top_k
        );

    std::cout
        << "Top results for: "
        << primary_query
        << "\n";

    std::cout
        << "-------------------------------\n";

    for (const auto& result : primary_results) {

        std::cout
            << "Document ID: "
            << result.document_id
            << " | Score: "
            << result.score
            << "\n";
    }


    return 0;
}