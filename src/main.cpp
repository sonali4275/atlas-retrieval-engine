#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "document_loader.h"
#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "search_result.h"
#include "tokenizer.h"

namespace {

void print_section_header(const std::string& title) {
    std::cout << "\n========================================\n";
    std::cout << title << "\n";
    std::cout << "========================================\n";
}

void print_results(
    const std::vector<SearchResult>& results
) {
    if (results.empty()) {
        return;
    }

    for (const auto& result : results) {

        std::cout
            << "Document ID: "
            << result.document_id
            << "\n";

        std::cout
            << "Score: "
            << std::fixed
            << std::setprecision(4)
            << result.score
            << "\n";

        std::cout
            << "Matched terms: ";

        for (const auto& term : result.matched_terms) {
            std::cout
                << term
                << " ";
        }

        std::cout << "\n";

        std::cout
            << "Positions: ";

        for (int position : result.positions) {
            std::cout
                << position
                << " ";
        }

        std::cout << "\n\n";
    }
}

void print_search_results(
    const std::string& label,
    const std::string& query,
    const std::vector<SearchResult>& results,
    double elapsed_ms
) {
    print_section_header(label);

    std::cout
        << "Query: "
        << query
        << "\n";

    std::cout
        << "Results: "
        << results.size()
        << "\n\n";

    print_results(results);

    std::cout
        << "Search time: "
        << std::fixed
        << std::setprecision(4)
        << elapsed_ms
        << " ms\n";
}

} // namespace


int main() {

    std::cout
        << "Atlas Retrieval Engine\n"
        << "======================\n\n";


    /*
     * ============================================================
     * 1. CREATE TOKENIZER AND INDEX
     * ============================================================
     */

    Tokenizer tokenizer;

    InvertedIndex index;


    /*
     * ============================================================
     * 2. LOAD DOCUMENTS
     * ============================================================
     */

    const std::string document_directory = "docs";

    std::cout
        << "Loading documents from: "
        << document_directory
        << "\n\n";

    const std::vector<Document> documents =
        DocumentLoader::load_from_directory(
            document_directory
        );

    if (documents.empty()) {

        std::cerr
            << "ERROR: No documents were loaded.\n";

        std::cerr
            << "Expected .txt files inside: "
            << document_directory
            << "\n";

        return 1;
    }


    std::cout
        << "Documents loaded: "
        << documents.size()
        << "\n\n";


    /*
     * ============================================================
     * 3. TOKENIZE AND INDEX DOCUMENTS
     * ============================================================
     */

    for (const auto& document : documents) {

        const std::vector<std::string> tokens =
            tokenizer.tokenize(
                document.text
            );

        std::cout
            << "----------------------------------------\n";

        std::cout
            << "Document ID: "
            << document.id
            << "\n";

        std::cout
            << "Filename: "
            << document.filename
            << "\n";

        std::cout
            << "Characters loaded: "
            << document.text.size()
            << "\n";

        if (document.text.empty()) {

            std::cout
                << "WARNING: Document text is EMPTY.\n";
        }

        std::cout
            << "Tokens generated: "
            << tokens.size()
            << "\n";

        if (!tokens.empty()) {

            std::cout
                << "First tokens: ";

            const std::size_t preview_count =
                std::min<std::size_t>(
                    tokens.size(),
                    10
                );

            for (std::size_t i = 0;
                 i < preview_count;
                 ++i) {

                std::cout
                    << tokens[i]
                    << " ";
            }

            std::cout << "\n";
        }

        index.add_document(
            document.id,
            tokens
        );

        std::cout
            << "----------------------------------------\n\n";
    }


    std::cout
        << "Total documents indexed: "
        << index.document_count()
        << "\n\n";


    /*
     * ============================================================
     * 4. SAVE INDEX
     * ============================================================
     */

    const std::string index_file =
        "atlas.index";

    if (IndexSerializer::save(
            index,
            index_file
        )) {

        std::cout
            << "Index saved to: "
            << index_file
            << "\n";
    }
    else {

        std::cerr
            << "ERROR: Failed to save index.\n";

        return 1;
    }


    /*
     * ============================================================
     * 5. LOAD INDEX
     * ============================================================
     */

    InvertedIndex loaded_index;

    if (IndexSerializer::load(
            loaded_index,
            index_file
        )) {

        std::cout
            << "Index loaded successfully.\n";

        std::cout
            << "Loaded documents: "
            << loaded_index.document_count()
            << "\n";
    }
    else {

        std::cerr
            << "ERROR: Failed to load index.\n";

        return 1;
    }


    /*
     * ============================================================
     * 6. CREATE QUERY PROCESSOR
     * ============================================================
     *
     * IMPORTANT:
     *
     * QueryProcessor requires:
     *
     *     QueryProcessor(tokenizer, index)
     *
     * Do NOT use:
     *
     *     QueryProcessor processor(loaded_index);
     */

    QueryProcessor processor(
        tokenizer,
        loaded_index
    );


    /*
     * ============================================================
     * 7. NORMAL SEARCH
     * ============================================================
     */

    {
        const std::string query =
            "vector search";

        print_section_header(
            "NORMAL SEARCH"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 8. PHRASE SEARCH
     * ============================================================
     */

    {
        const std::string phrase =
            "vector databases";

        print_section_header(
            "PHRASE SEARCH"
        );

        std::cout
            << "Phrase: \""
            << phrase
            << "\"\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.phrase_search(
                phrase,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 9. BOOLEAN AND
     * ============================================================
     */

    {
        const std::string query =
            "vector AND search";

        print_section_header(
            "BOOLEAN AND"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 10. BOOLEAN OR
     * ============================================================
     */

    {
        const std::string query =
            "vector OR retrieval";

        print_section_header(
            "BOOLEAN OR"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 11. BOOLEAN NOT
     * ============================================================
     */

    {
        const std::string query =
            "vector NOT databases";

        print_section_header(
            "BOOLEAN NOT"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 12. BOOLEAN PARENTHESES
     * ============================================================
     */

    {
        const std::string query =
            "vector AND (search OR retrieval)";

        print_section_header(
            "BOOLEAN PARENTHESES"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 13. BOOLEAN PRECEDENCE
     * ============================================================
     */

    {
        const std::string query =
            "vector OR retrieval AND search";

        print_section_header(
            "BOOLEAN PRECEDENCE"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        std::cout
            << "Expected interpretation:\n"
            << "vector OR (retrieval AND search)\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "\nResults: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 14. COMPLEX BOOLEAN QUERY
     * ============================================================
     */

    {
        const std::string query =
            "(vector OR retrieval) AND search";

        print_section_header(
            "COMPLEX BOOLEAN QUERY"
        );

        std::cout
            << "Query: "
            << query
            << "\n";

        auto start =
            std::chrono::high_resolution_clock::now();

        const std::vector<SearchResult> results =
            processor.boolean_search(
                query,
                10
            );

        auto end =
            std::chrono::high_resolution_clock::now();

        const std::chrono::duration<double, std::milli>
            elapsed =
                end - start;

        std::cout
            << "Results: "
            << results.size()
            << "\n\n";

        print_results(results);

        std::cout
            << "Search time: "
            << std::fixed
            << std::setprecision(4)
            << elapsed.count()
            << " ms\n";
    }


    /*
     * ============================================================
     * 15. FINISHED
     * ============================================================
     */

    std::cout
        << "\nAtlas Retrieval Engine execution completed.\n";

    return 0;
}