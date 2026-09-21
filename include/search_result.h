#pragma once

#include <string>
#include <vector>

struct SearchResult {
    int document_id;

    double score;

    std::vector<int> positions;

    // Query terms that matched this document.
    std::vector<std::string> matched_terms;
};