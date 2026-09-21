#pragma once

#include <string>

#include "inverted_index.h"

class IndexSerializer {
public:

    static bool save(
        const InvertedIndex& index,
        const std::string& filename
    );

    static bool load(
        InvertedIndex& index,
        const std::string& filename
    );
};