# Atlas Retrieval Engine

[![CI](https://github.com/sonali4275/atlas-retrieval-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/sonali4275/atlas-retrieval-engine/actions/workflows/ci.yml)

A search engine core written from scratch in C++17 with no third-party dependencies:
a positional inverted index, BM25 ranking, phrase queries, Boolean queries with
proper `NOT` semantics, and a validated, checksummed on-disk index format.

It ships as a small library (`atlas_core`), an interactive command-line tool,
a test suite that runs under ASan/UBSan/TSan in CI, and a benchmark.

## Quick start

```bash
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure      # run the tests
./build/atlas --docs docs                       # index ./docs and open a prompt
```

No CMake? Everything is plain C++17:

```bash
g++ -std=c++17 -O2 -Iinclude src/main.cpp src/tokenizer.cpp src/inverted_index.cpp \
    src/query_processor.cpp src/index_serializer.cpp src/document_loader.cpp -o atlas
```

```text
$ ./atlas --docs docs --save demo.index
Indexed 4 documents from docs in 0.03 ms
Saved index to demo.index

atlas> vector search
  1. doc 3 (document3.txt)  score 1.2634  terms: search vector  positions: 0 6 7 8 9
  2. doc 1 (document1.txt)  score 1.0754  terms: search vector  positions: 0 1 9 12
  3. doc 4 (document4.txt)  score 0.1396  terms: search  positions: 9 10
  4. doc 2 (document2.txt)  score 0.1101  terms: search  positions: 6
4 result(s) in 0.013 ms

atlas> "vector search"
  1. doc 3 (document3.txt)  score 1.1312  terms: search vector  positions: 6 8
  2. doc 1 (document1.txt)  score 0.7749  terms: search vector  positions: 0
2 result(s) in 0.005 ms

atlas> bool: vector AND NOT databases
  1. doc 1 (document1.txt)  score 0.9335  terms: vector  positions: 0 12
1 result(s) in 0.006 ms

atlas> bool: vector search
invalid query: unexpected 'search' (put AND, OR or NOT between terms)
```

Later: `./atlas --load demo.index` reloads the saved index without re-reading documents.

### Query syntax

| Input | Meaning |
|---|---|
| `vector search` | BM25 ranking over the union of the terms |
| `"vector search"` | phrase: the terms must be adjacent, in order |
| `bool: a AND (b OR c)` | Boolean query; `AND`, `OR`, `NOT` (case-insensitive) and parentheses |

Boolean precedence is `NOT` > `AND` > `OR`; `a NOT b` means `a AND NOT b`. `NOT x` on its own
returns every document that does not contain `x`. Invalid queries return a message
explaining what is wrong. Matches are ranked by BM25 over the terms that are not negated.

## How it works

```text
Documents ──> Tokenizer ──> Inverted index (term -> doc -> positions)
                                  │
   Query ──> Query processor ─────┤
                 ├── BM25 ranking ─────────┐
                 ├── Phrase search ────────┼──> Top-K results
                 └── Boolean (set algebra) ┘
```

- **Index.** `term -> (document id -> ascending token positions)`, plus each document's length.
  Term frequency is the number of positions, so ranking and phrase matching share one structure.
- **Ranking.** BM25 with `k1 = 1.2`, `b = 0.75` and a Lucene-style IDF that is never negative:

  ```text
  score(D,Q) = Σ IDF(t) · tf·(k1+1) / (tf + k1·(1 − b + b·|D|/avgdl))
  IDF(t)     = ln(1 + (N − df + 0.5) / (df + 0.5))
  ```

- **Top-K.** Candidates are scored as bare `(id, score)` pairs and selected with
  `partial_sort`; positions and matched terms are built only for the K winners.
- **Phrase search.** Starts from the rarest term's documents, then checks each following term at
  `start + offset` with binary search. The number of phrase occurrences replaces `tf` in BM25.
- **Boolean search.** Parsed by recursive descent into an AST and evaluated with sorted-set
  operations on posting lists (`A AND NOT B` becomes a set difference, so the whole-collection
  universe is only built when a bare `NOT` needs it). Queries over 512 tokens are rejected, which
  bounds recursion depth.
- **Thread safety.** All `const` methods are safe to call concurrently; there are no mutable caches.
- **Persistence.** Little-endian format with a magic number, version and FNV-1a checksum.
  Loading validates every length and id against the remaining bytes, parses into a temporary
  index and swaps it in only on success. Saving writes `<file>.tmp` and renames it. Output is
  sorted, so the same index always produces identical bytes.

| Operation | Cost (N docs, df = posting length, M = matching docs, K = top-K) |
|---|---|
| `add_document` | O(tokens); re-adding an id is O(vocabulary + tokens) |
| BM25 search | O(Σ df + M log K) |
| Phrase search | O(df of rarest term × phrase length × log positions) |
| Boolean search | O(Σ df log df) for the set operations, then O(M × terms) to rank |

## Testing

`tests/test_core.cpp` has 21 tests (about 2,200 checks). It deliberately avoids `assert()`,
which is compiled out by `NDEBUG` in Release builds and would make every test pass silently.

Beyond the basics, it covers:

- **Regression tests** for each bug fixed so far (see [CHANGES.md](CHANGES.md)).
- **Property tests:** top-K equals a prefix of the full ranking; pure-OR Boolean ranking is
  identical to BM25; Boolean `AND` equals posting-list intersection; De Morgan's law holds.
- **Persistence tests:** save→load→save is byte-identical, and corrupted, truncated, and
  hostile files (crafted with valid checksums) are rejected without touching the live index.
- **Hostile input:** 200,000 nested parentheses and 100,000-term queries return an error.
- **Concurrency:** four threads querying one index return the same results as one thread.

CI runs the suite with GCC and Clang, on Linux and macOS, warnings-as-errors, and again under
AddressSanitizer + UBSan and ThreadSanitizer.

## Benchmarks

Synthetic but Zipf-distributed corpus (term *i* has weight 1/(i+1)), 50,000 documents,
4,000,000 tokens, 50,000-term vocabulary, seed 42. Each query is warmed up, then timed 30
times with `steady_clock`. Measured with GCC 13 `-O2` on a single shared 2.1 GHz Xeon vCPU;
absolute numbers will differ on your machine, so run `./build/atlas_benchmark` yourself.

| Query type | Query | p50 (ms) | p99 (ms) |
|---|---|---|---|
| BM25 | two very common terms | 7.63 | 10.02 |
| BM25 | two mid-frequency terms | 0.47 | 0.70 |
| BM25 | one rare term | 0.002 | 0.03 |
| Phrase | two common terms | 8.73 | 15.71 |
| Phrase | three terms | 4.00 | 4.48 |
| Boolean | `common AND common` | 8.15 | 9.24 |
| Boolean | `common OR common` | 9.10 | 10.75 |
| Boolean | `common AND NOT common` | 6.89 | 7.78 |
| Boolean | `NOT common` | 6.28 | 6.69 |

| Indexing | Value |
|---|---|
| Throughput | 1.58 M tokens/s |
| Memory | ~294 MB (~77 bytes per token) |
| Save / load (with full validation) | 968 ms / 581 ms for a 43 MB file |

Latency is dominated by common terms, because their posting lists cover much of the
collection and scoring is exhaustive. That is the target of the WAND item in the roadmap.
Memory is about 7× the on-disk size, which is the cost of the current hash-of-hash layout.

## Limitations and roadmap

Known limitations, stated plainly:

- **Tokenizer** is ASCII-only, with no stemming or stop words. Non-ASCII bytes act as
  separators, so accented words are split.
- **Memory layout** is `unordered_map` of `unordered_map` of `vector`: simple, but about
  77 bytes per token, with no compression and no skip pointers.
- **The whole index lives in RAM**, and loading reads the entire file.
- **Relevance quality has not been evaluated** against a labelled dataset (nDCG / MAP);
  only speed and correctness are measured.
- Document filenames are not stored in the index file, so `--load` shows ids only.

Planned, roughly in order of payoff:

1. Sorted `vector<Posting>` layout with delta + varint compression (smaller and faster).
2. Skip pointers and WAND / MaxScore early termination for top-K.
3. Relevance evaluation (nDCG@10) on a public dataset such as BEIR SciFact.
4. Parallel index build and memory-mapped loading.
5. Unicode-aware tokenization.

## Project layout

```text
include/   public headers          src/        implementation + CLI + benchmark
tests/     test suite              docs/       sample documents
.github/   CI workflow             CHANGES.md  bug fixes and their regression tests
```

MIT licensed.
