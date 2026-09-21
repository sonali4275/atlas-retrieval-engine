# Atlas Retrieval Engine

A technical knowledge retrieval engine built from scratch in C++17 using a custom inverted index, BM25 ranking, phrase search, Boolean query processing, index serialization, and performance benchmarking.

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
- Performance benchmarking with synthetic documents
- Top-K search results
- CMake-based build system
- C++17 implementation

## Project Structure

```text
atlas-retrieval-engine/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
│
├── docs/
│   ├── document1.txt
│   ├── document2.txt
│   ├── document3.txt
│   └── document4.txt
│
├── include/
│   ├── document_loader.h
│   ├── index_serializer.h
│   ├── inverted_index.h
│   ├── query_processor.h
│   ├── search_result.h
│   └── tokenizer.h
│
├── src/
│   ├── benchmark.cpp
│   ├── document_loader.cpp
│   ├── index_serializer.cpp
│   ├── inverted_index.cpp
│   ├── main.cpp
│   ├── query_processor.cpp
│   └── tokenizer.cpp
│
├── benchmarks/
├── datasets/
└── tests/
