#pragma once

#include <string>

#include "inverted_index.h"

// Binary index format (version 2, all integers little-endian):
//
//   "ATLS" | u32 version | u32 doc_count | doc_count x (i32 id, i32 length)
//   u32 term_count | term_count x (
//       u32 term_length | term bytes | u32 posting_count |
//       posting_count x (i32 doc_id | u32 position_count | i32 positions...))
//   u64 FNV-1a checksum of every preceding byte
//
// Documents, terms and postings are written in sorted order, so saving the
// same index twice produces identical bytes.
class IndexSerializer {
public:
    // Writes to "<filename>.tmp" and renames, so a crash never leaves a
    // half-written index at `filename`.
    static bool save(const InvertedIndex& index, const std::string& filename,
                     std::string* error = nullptr);

    // Validates the whole file (checksum, bounds, ids, ordering) and only
    // replaces `index` if everything is valid. On failure `index` is left
    // untouched.
    static bool load(InvertedIndex& index, const std::string& filename,
                     std::string* error = nullptr);
};
