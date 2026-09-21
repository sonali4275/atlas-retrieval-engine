# Atlas Retrieval Engine

A technical knowledge retrieval engine built from scratch in C++17 using a custom inverted index, BM25 relevance ranking, positional phrase search, Boolean query processing, persistent index serialization, automated testing, and performance benchmarking.

## Features

- Custom inverted index
- Positional indexing for phrase search
- BM25 relevance ranking
- Normal keyword search
- Phrase search
- Boolean `AND`, `OR`, and `NOT` queries
- Parentheses support for Boolean queries
- Boolean operator precedence
- Persistent index serialization and loading
- Custom document loader
- Custom tokenizer and text normalization
- Top-K search results
- Core automated test suite
- Performance benchmarking with synthetic documents
- CMake-based build system
- C++17 implementation

## Architecture

```text
Documents
    |
    v
+-------------------+
|  Document Loader  |
+---------+---------+
          |
          v
+-------------------+
|     Tokenizer     |
+---------+---------+
          |
          v
+-------------------+
|  Inverted Index   |
+---------+---------+
          |
          v
+-------------------+
|  Query Processor  |
+---------+---------+
          |
    +-----+-----+----------------+
    |           |                |
    v           v                v
  BM25       Phrase           Boolean
 Ranking      Search            Search
    |           |                |
    +-----------+----------------+
                |
                v
           Top-K Results