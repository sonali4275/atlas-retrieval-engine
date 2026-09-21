// Atlas Retrieval Engine - interactive command-line search.
//
//   atlas --docs <dir> [--save <index-file>] [--top-k N]
//   atlas --load <index-file> [--top-k N]
//
// At the prompt:
//   vector search          BM25 ranked search
//   "vector search"        phrase search (wrap the query in double quotes)
//   bool: a AND (b OR c)   Boolean search (AND, OR, NOT, parentheses)
//   :stats  :help  :quit

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "document_loader.h"
#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "tokenizer.h"

namespace {

struct Options {
    std::string docs_dir;
    std::string load_file;
    std::string save_file;
    std::size_t top_k = 10;
    bool help = false;
};

void print_usage() {
    std::cout <<
        "Usage:\n"
        "  atlas --docs <dir> [--save <index-file>] [--top-k N]\n"
        "  atlas --load <index-file> [--top-k N]\n"
        "\n"
        "With no arguments, documents are read from ./docs.\n"
        "Only .txt files are indexed; document ids follow sorted file names.\n";
}

void print_prompt_help() {
    std::cout <<
        "  <words>                BM25 ranked search\n"
        "  \"<words>\"              phrase search\n"
        "  bool: <expression>     Boolean search: AND, OR, NOT, ( )\n"
        "  :stats                 index statistics\n"
        "  :quit                  exit\n";
}

bool parse_args(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                error = arg + " needs a value";
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--docs") {
            if (!value(options.docs_dir)) return false;
        } else if (arg == "--load") {
            if (!value(options.load_file)) return false;
        } else if (arg == "--save") {
            if (!value(options.save_file)) return false;
        } else if (arg == "--top-k") {
            std::string text;
            if (!value(text)) return false;
            const long parsed = std::strtol(text.c_str(), nullptr, 10);
            if (parsed <= 0) {
                error = "--top-k must be a positive integer";
                return false;
            }
            options.top_k = static_cast<std::size_t>(parsed);
        } else {
            error = "unknown argument: " + arg;
            return false;
        }
    }
    if (!options.docs_dir.empty() && !options.load_file.empty()) {
        error = "use either --docs or --load, not both";
        return false;
    }
    if (options.docs_dir.empty() && options.load_file.empty()) {
        options.docs_dir = "docs";
    }
    return true;
}

std::string trim(const std::string& s) {
    const char* whitespace = " \t\r\n";
    const std::size_t begin = s.find_first_not_of(whitespace);
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(whitespace) - begin + 1);
}

void print_results(const std::vector<SearchResult>& results,
                   const std::unordered_map<int, std::string>& filenames) {
    std::cout << std::fixed << std::setprecision(4);
    int rank = 1;
    for (const SearchResult& r : results) {
        std::cout << std::setw(3) << rank++ << ". doc " << r.document_id;
        auto name = filenames.find(r.document_id);
        if (name != filenames.end()) std::cout << " (" << name->second << ")";
        std::cout << "  score " << r.score << "  terms:";
        for (const std::string& term : r.matched_terms) std::cout << ' ' << term;
        std::cout << "  positions:";
        const std::size_t shown = std::min<std::size_t>(r.positions.size(), 8);
        for (std::size_t i = 0; i < shown; ++i) std::cout << ' ' << r.positions[i];
        if (r.positions.size() > shown) std::cout << " ...";
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_args(argc, argv, options, error)) {
        std::cerr << "error: " << error << "\n\n";
        print_usage();
        return 2;
    }
    if (options.help) {
        print_usage();
        return 0;
    }

    Tokenizer tokenizer;
    InvertedIndex index;
    std::unordered_map<int, std::string> filenames;

    if (!options.load_file.empty()) {
        if (!IndexSerializer::load(index, options.load_file, &error)) {
            std::cerr << "error: cannot load index: " << error << '\n';
            return 1;
        }
        std::cout << "Loaded " << index.document_count() << " documents from "
                  << options.load_file << '\n';
    } else {
        const std::vector<Document> documents =
            DocumentLoader::load_from_directory(options.docs_dir);
        if (documents.empty()) {
            std::cerr << "error: no .txt documents found in '" << options.docs_dir
                      << "'\n";
            return 1;
        }
        const auto start = std::chrono::steady_clock::now();
        for (const Document& document : documents) {
            index.add_document(document.id, tokenizer.tokenize(document.text));
            filenames[document.id] = document.filename;
        }
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - start;
        std::cout << "Indexed " << index.document_count() << " documents from "
                  << options.docs_dir << " in " << std::fixed
                  << std::setprecision(2) << elapsed.count() << " ms\n";
    }

    if (!options.save_file.empty()) {
        if (!IndexSerializer::save(index, options.save_file, &error)) {
            std::cerr << "error: cannot save index: " << error << '\n';
            return 1;
        }
        std::cout << "Saved index to " << options.save_file << '\n';
    }

    const QueryProcessor processor(tokenizer, index);
    std::cout << "\nType a query (:help for commands, :quit to exit).\n";

    std::string line;
    while (std::cout << "\natlas> " << std::flush, std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line == ":quit" || line == ":q" || line == ":exit") break;
        if (line == ":help") {
            print_prompt_help();
            continue;
        }
        if (line == ":stats") {
            std::cout << "documents: " << index.document_count() << '\n';
            continue;
        }

        std::vector<SearchResult> results;
        std::string query_error;
        const auto start = std::chrono::steady_clock::now();
        if (line.rfind("bool:", 0) == 0) {
            results = processor.boolean_search(trim(line.substr(5)),
                                               options.top_k, &query_error);
        } else if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
            results = processor.phrase_search(line.substr(1, line.size() - 2),
                                              options.top_k);
        } else {
            results = processor.search(line, options.top_k);
        }
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - start;

        if (!query_error.empty()) {
            std::cout << "invalid query: " << query_error << '\n';
            continue;
        }
        print_results(results, filenames);
        std::cout << results.size() << " result(s) in " << std::fixed
                  << std::setprecision(3) << elapsed.count() << " ms\n";
    }
    std::cout << '\n';
    return 0;
}
