#pragma once

#include <string>
#include <vector>

struct Document {
    int id;
    std::string text;
    std::string filename;
};

class DocumentLoader {
public:

    static std::vector<Document> load_from_directory(
        const std::string& directory
    );

private:

    static bool is_text_file(
        const std::string& filename
    );
};