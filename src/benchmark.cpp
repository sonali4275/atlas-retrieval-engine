// Atlas Retrieval Engine - benchmark.
//
//   atlas_benchmark [--docs N] [--doc-length N] [--vocab N] [--reps N] [--seed N]
//
// The corpus is synthetic but Zipf-distributed (term i has weight 1/(i+1)),
// which is how real text behaves: a few terms are in almost every document and
// most terms are rare. Each query is warmed up, then timed `reps` times with a
// monotonic clock; the table reports the median and 99th percentile.
// Build with -O2 (the default CMake build type here is Release).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "index_serializer.h"
#include "inverted_index.h"
#include "query_processor.h"
#include "tokenizer.h"

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Resident set size in MB (Linux only; -1 elsewhere).
long resident_mb() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) return std::stol(line.substr(6)) / 1024;
    }
#endif
    return -1;
}

struct Config {
    int documents = 50000;
    int doc_length = 80;
    int vocabulary = 50000;
    int reps = 30;
    unsigned seed = 42;
};

Config parse_config(int argc, char** argv) {
    Config config;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        const long value = std::strtol(argv[i + 1], nullptr, 10);
        if (value <= 0) continue;
        if (flag == "--docs") config.documents = static_cast<int>(value);
        else if (flag == "--doc-length") config.doc_length = static_cast<int>(value);
        else if (flag == "--vocab") config.vocabulary = static_cast<int>(value);
        else if (flag == "--reps") config.reps = static_cast<int>(value);
        else if (flag == "--seed") config.seed = static_cast<unsigned>(value);
    }
    return config;
}

struct Row {
    std::string kind;
    std::string query;
    double p50_ms;
    double p99_ms;
    std::size_t hits;
};

template <typename Fn>
Row measure(const std::string& kind, const std::string& query, int reps, Fn run) {
    for (int i = 0; i < 3; ++i) run();  // warm-up
    std::vector<double> samples;
    std::size_t hits = 0;
    for (int i = 0; i < reps; ++i) {
        const auto start = Clock::now();
        hits = run();
        samples.push_back(elapsed_ms(start));
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double p) {  // nearest-rank
        const std::size_t rank = static_cast<std::size_t>(
            std::max(1.0, std::ceil(p * static_cast<double>(samples.size()))));
        return samples[rank - 1];
    };
    return {kind, query, percentile(0.50), percentile(0.99), hits};
}

}  // namespace

int main(int argc, char** argv) {
    const Config config = parse_config(argc, argv);

    // ---------------------------------------------------------- build corpus
    std::mt19937 rng(config.seed);
    std::vector<double> weights(static_cast<std::size_t>(config.vocabulary));
    for (std::size_t i = 0; i < weights.size(); ++i)
        weights[i] = 1.0 / static_cast<double>(i + 1);
    std::discrete_distribution<int> zipf(weights.begin(), weights.end());

    Tokenizer tokenizer;
    InvertedIndex index;
    const long memory_before = resident_mb();
    std::size_t total_tokens = 0;
    double tokenize_ms = 0.0;
    double index_ms = 0.0;

    for (int d = 1; d <= config.documents; ++d) {
        std::string text;
        text.reserve(static_cast<std::size_t>(config.doc_length) * 7);
        for (int j = 0; j < config.doc_length; ++j) {
            text += 'w';
            text += std::to_string(zipf(rng));
            text += ' ';
        }
        auto start = Clock::now();
        const std::vector<std::string> tokens = tokenizer.tokenize(text);
        tokenize_ms += elapsed_ms(start);

        start = Clock::now();
        index.add_document(d, tokens);
        index_ms += elapsed_ms(start);
        total_tokens += tokens.size();
    }
    const long memory_mb = resident_mb() - memory_before;
    const double build_ms = tokenize_ms + index_ms;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "## Corpus\n\n"
              << "| Documents | Tokens | Vocabulary | Seed |\n|---|---|---|---|\n"
              << "| " << config.documents << " | " << total_tokens << " | "
              << config.vocabulary << " | " << config.seed << " |\n\n";

    std::cout << "## Indexing\n\n"
              << "| Metric | Value |\n|---|---|\n"
              << "| Tokenize + index time | " << build_ms << " ms |\n"
              << "| Throughput | "
              << static_cast<double>(total_tokens) / (build_ms / 1000.0) / 1e6
              << " M tokens/s |\n";
    if (memory_mb >= 0) {
        std::cout << "| Memory (RSS growth) | ~" << memory_mb << " MB (~"
                  << static_cast<double>(memory_mb) * 1024.0 * 1024.0 /
                         static_cast<double>(total_tokens)
                  << " bytes/token) |\n";
    }
    std::cout << '\n';

    // -------------------------------------------------------------- queries
    const QueryProcessor q(tokenizer, index);
    const int reps = config.reps;
    std::vector<Row> rows;

    for (const char* query : {"w1 w3", "w100 w500", "w20000"})
        rows.push_back(measure("BM25", query, reps, [&] { return q.search(query, 10).size(); }));
    for (const char* query : {"w1 w2", "w2 w5 w9"})
        rows.push_back(measure("Phrase", query, reps, [&] { return q.phrase_search(query, 10).size(); }));
    for (const char* query : {"w1 AND w3", "w1 OR w3", "w1 AND NOT w3",
                              "w100 AND w500", "NOT w1"})
        rows.push_back(measure("Boolean", query, reps, [&] { return q.boolean_search(query, 10).size(); }));

    std::cout << "## Query latency (top-10, " << reps << " runs each)\n\n"
              << "| Type | Query | p50 (ms) | p99 (ms) | Hits |\n|---|---|---|---|---|\n";
    std::cout << std::setprecision(3);
    for (const Row& r : rows) {
        std::cout << "| " << r.kind << " | `" << r.query << "` | " << r.p50_ms
                  << " | " << r.p99_ms << " | " << r.hits << " |\n";
    }

    // -------------------------------------------------------- serialization
    const std::string path =
        (std::filesystem::temp_directory_path() / "atlas_benchmark.index").string();
    auto start = Clock::now();
    const bool saved = IndexSerializer::save(index, path);
    const double save_ms = elapsed_ms(start);

    InvertedIndex loaded;
    start = Clock::now();
    const bool loaded_ok = IndexSerializer::load(loaded, path);
    const double load_ms = elapsed_ms(start);
    const double file_mb = saved ? static_cast<double>(std::filesystem::file_size(path)) / 1e6 : 0.0;
    std::filesystem::remove(path);

    std::cout << "\n## Persistence\n\n"
              << "| Save | Load (with full validation) | File size |\n|---|---|---|\n"
              << std::setprecision(1) << "| " << save_ms << " ms | " << load_ms
              << " ms | " << file_mb << " MB |\n";

    return (saved && loaded_ok) ? 0 : 1;
}
