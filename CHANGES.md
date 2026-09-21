# Changes in 0.2.0

Every item below was found by running the code (tests, probes, sanitizers, or measurement),
not just by reading it, and each has a regression test in `tests/test_core.cpp`.

## Correctness bugs

| # | Bug | Effect | Fix | Guarded by |
|---|---|---|---|---|
| 1 | `add_document` on an existing id kept the old postings and merged old and new positions into one unsorted list | Stale documents still matched; phrase search's `binary_search` ran on unsorted data. The original `test_reindex_document` **failed** in a normal Debug build. | Old postings are removed before re-indexing | `test_reindex_*` |
| 2 | Boolean `NOT` was evaluated only against documents that contained a query term | `NOT x` returned nothing; `a OR NOT b` missed documents containing neither | Evaluate against the whole collection using sorted-set algebra | `test_boolean_not_is_evaluated_against_whole_collection` |
| 3 | Tokenizer deleted punctuation instead of splitting on it | `state-of-the-art` became one token `stateoftheart`; `hello,world` became `helloworld` | Split on non-alphanumerics; apostrophes inside words are dropped | `test_tokenizer_punctuation_splits_words` |
| 4 | Boolean query tokenizer duplicated (and shared) the same bug | Hyphenated terms could never match | Reuse `Tokenizer`; a multi-token word becomes an AND group | `test_boolean_hyphenated_words` |
| 5 | Every phrase match scored the same value (sum of IDFs) | Phrase results were ordered by document id, not relevance | BM25 with phrase frequency in place of `tf` | `test_phrase_ranking_*` |

## Robustness and safety

| # | Problem | Fix | Guarded by |
|---|---|---|---|
| 6 | ~20,000 nested parentheses overflowed the stack (SIGSEGV) | Reject queries over 512 tokens; `unique_ptr` AST | `test_boolean_hostile_input_does_not_crash` |
| 7 | `const search()` mutated `mutable` caches: a **data race** (found with ThreadSanitizer) | Removed the caches; IDF and average length are cheap to compute | `test_concurrent_searches_*` |
| 8 | A 16-byte crafted index file could force a ~4 GB allocation (`bad_alloc`) | Every length is checked against remaining bytes before allocating | `test_serialization_rejects_bad_files_*` |
| 9 | A failed `load` left the target index wiped or half-filled | Parse into a temporary index and swap on success | same test |
| 10 | No integrity check; a crash during `save` could corrupt the only copy | Magic number, checksum, and atomic temp-file + rename | same test |
| 11 | Malformed Boolean queries silently returned nothing | Error messages via an optional `std::string* error` | `test_boolean_reports_errors` |

## Performance

Top-K used to build a full `SearchResult` (positions, matched terms) for **every** matching
document and only then discard all but K. Boolean queries ran a full BM25 ranking over the
entire collection once for the query and once more per term. Now candidates are scored as bare
`(id, score)` pairs, K winners are selected, and details are built for those K only; Boolean
queries use posting-list set operations.

Measured with one harness on the same corpus (50,000 docs, 4M Zipf tokens, p50, ms):

| Query | Before | After | Speedup |
|---|---|---|---|
| BM25, two common terms | 28.2 | 7.2 | 3.9× |
| BM25, two mid-frequency terms | 1.40 | 0.46 | 3.1× |
| Phrase, two common terms | 21.9 | 8.6 | 2.6× |
| Boolean `a AND b` (common) | 119 | 8.5 | 14× |
| Boolean `a OR b` (common) | 124 | 8.6 | 14× |
| Boolean `a AND NOT b` (common) | 114 | 7.0 | 16× |
| Boolean `a AND b` (mid-frequency) | 3.86 | 0.27 | 15× |

Unchanged: indexing time (3.2 s before vs 3.0 s after, within noise) and memory (~77 bytes per
token), because the data layout is the same. Save is slower (about 620 → 980 ms) because output
is now sorted for reproducibility and checksummed. Load takes about the same time despite full
validation.

## Tooling

- Tests no longer use `assert()`. Under a Release build (`-DNDEBUG`) the old suite printed
  "All tests passed" while checking nothing. The new suite always runs and exits non-zero on failure.
- CMake: a shared `atlas_core` library, tests registered with CTest, `ATLAS_SANITIZER=address|thread`.
- GitHub Actions: GCC, Clang, macOS, ASan+UBSan and TSan.
- `main.cpp` is now an interactive CLI instead of a 700-line hard-coded demo.
- The benchmark uses a Zipf corpus, warm-up, repeated runs and p50/p99, and covers phrase,
  Boolean and persistence.
- Removed dead code: the unused `Posting` struct and the unused `calculate_bm25_score`.
