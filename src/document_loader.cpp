#include "document_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

bool DocumentLoader::is_text_file(
    const std::string& filename
) {
    const fs::path path(filename);

    std::string extension =
        path.extension().string();

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return extension == ".txt";
}

std::vector<Document>
DocumentLoader::load_from_directory(
    const std::string& directory
) {
    std::vector<Document> documents;

    const fs::path directory_path(directory);

    if (!fs::exists(directory_path) ||
        !fs::is_directory(directory_path)) {
        return documents;
    }

    std::vector<fs::path> files;

    for (const auto& entry :
         fs::directory_iterator(directory_path)) {

        if (!entry.is_regular_file()) {
            continue;
        }

        if (is_text_file(
                entry.path().filename().string())) {

            files.push_back(entry.path());
        }
    }

    // Keep document IDs deterministic.
    std::sort(
        files.begin(),
        files.end()
    );

    int document_id = 1;

    for (const fs::path& file : files) {

        std::ifstream input(
            file,
            std::ios::in | std::ios::binary
        );

        if (!input.is_open()) {
            continue;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();

        if (input.bad()) {
            continue;
        }

        Document document;

        document.id = document_id++;

        document.text = buffer.str();

        document.filename =
            file.filename().string();

        documents.push_back(
            std::move(document)
        );
    }

    return documents;
}