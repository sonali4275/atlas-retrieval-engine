#include "index_serializer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace {

constexpr char kMagic[4] = {'A', 'T', 'L', 'S'};
constexpr std::uint32_t kVersion = 2;
constexpr std::size_t kChecksumBytes = 8;

std::uint64_t fnv1a(const unsigned char* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

class ByteWriter {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            buffer_.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            buffer_.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
    }
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        buffer_.insert(buffer_.end(), p, p + size);
    }
    std::vector<unsigned char>& buffer() { return buffer_; }

private:
    std::vector<unsigned char> buffer_;
};

// Bounds-checked reader: every read reports failure instead of overrunning.
class ByteReader {
public:
    ByteReader(const unsigned char* data, std::size_t size)
        : data_(data), remaining_(size) {}

    std::size_t remaining() const { return remaining_; }

    bool u32(std::uint32_t& out) {
        if (remaining_ < 4) return false;
        out = static_cast<std::uint32_t>(data_[0]) |
              (static_cast<std::uint32_t>(data_[1]) << 8) |
              (static_cast<std::uint32_t>(data_[2]) << 16) |
              (static_cast<std::uint32_t>(data_[3]) << 24);
        advance(4);
        return true;
    }
    bool i32(std::int32_t& out) {
        std::uint32_t v = 0;
        if (!u32(v)) return false;
        out = static_cast<std::int32_t>(v);
        return true;
    }
    bool bytes(std::string& out, std::size_t size) {
        if (remaining_ < size) return false;
        out.assign(reinterpret_cast<const char*>(data_), size);
        advance(size);
        return true;
    }

private:
    void advance(std::size_t n) {
        data_ += n;
        remaining_ -= n;
    }
    const unsigned char* data_;
    std::size_t remaining_;
};

}  // namespace

// --------------------------------------------------------------------- save

bool IndexSerializer::save(const InvertedIndex& index,
                           const std::string& filename, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();

    ByteWriter w;
    w.bytes(kMagic, sizeof(kMagic));
    w.u32(kVersion);

    std::vector<std::pair<int, int>> documents(index.document_lengths_.begin(),
                                               index.document_lengths_.end());
    std::sort(documents.begin(), documents.end());
    w.u32(static_cast<std::uint32_t>(documents.size()));
    for (const auto& [document_id, length] : documents) {
        w.i32(document_id);
        w.i32(length);
    }

    using TermEntry = std::pair<const std::string, InvertedIndex::PostingMap>;
    std::vector<const TermEntry*> terms;
    terms.reserve(index.index_.size());
    for (const TermEntry& entry : index.index_) terms.push_back(&entry);
    std::sort(terms.begin(), terms.end(),
              [](const TermEntry* a, const TermEntry* b) {
                  return a->first < b->first;
              });

    w.u32(static_cast<std::uint32_t>(terms.size()));
    for (const TermEntry* term_entry : terms) {
        const std::string& term = term_entry->first;
        w.u32(static_cast<std::uint32_t>(term.size()));
        w.bytes(term.data(), term.size());

        using PostingEntry = std::pair<const int, std::vector<int>>;
        std::vector<const PostingEntry*> postings;
        postings.reserve(term_entry->second.size());
        for (const PostingEntry& posting : term_entry->second)
            postings.push_back(&posting);
        std::sort(postings.begin(), postings.end(),
                  [](const PostingEntry* a, const PostingEntry* b) {
                      return a->first < b->first;
                  });

        w.u32(static_cast<std::uint32_t>(postings.size()));
        for (const PostingEntry* posting : postings) {
            w.i32(posting->first);
            w.u32(static_cast<std::uint32_t>(posting->second.size()));
            for (int position : posting->second) w.i32(position);
        }
    }

    w.u64(fnv1a(w.buffer().data(), w.buffer().size()));

    // Write to a temporary file, then rename over the destination.
    const std::string temp_name = filename + ".tmp";
    {
        std::ofstream out(temp_name, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot create " + temp_name);
        out.write(reinterpret_cast<const char*>(w.buffer().data()),
                  static_cast<std::streamsize>(w.buffer().size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temp_name, ignored);
            return fail("write failed");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp_name, filename, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp_name, ignored);
        return fail("cannot rename into place: " + ec.message());
    }
    return true;
}

// --------------------------------------------------------------------- load

bool IndexSerializer::load(InvertedIndex& index, const std::string& filename,
                           std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (error) error->clear();

    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    if (!in) return fail("cannot open " + filename);
    const std::streamoff file_size = in.tellg();
    if (file_size < 0) return fail("cannot determine file size");

    std::vector<unsigned char> data(static_cast<std::size_t>(file_size));
    in.seekg(0);
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    }
    if (!in) return fail("read error");

    if (data.size() < sizeof(kMagic) + 4 + kChecksumBytes)
        return fail("file too small to be an Atlas index");

    // Integrity first: nothing below runs on a corrupted file.
    const std::size_t payload_size = data.size() - kChecksumBytes;
    std::uint64_t stored_checksum = 0;
    for (int i = 0; i < 8; ++i) {
        stored_checksum |= static_cast<std::uint64_t>(data[payload_size + i])
                           << (8 * i);
    }
    if (fnv1a(data.data(), payload_size) != stored_checksum)
        return fail("checksum mismatch (file is corrupted or truncated)");

    ByteReader r(data.data(), payload_size);

    std::string magic;
    if (!r.bytes(magic, sizeof(kMagic)) ||
        magic != std::string(kMagic, sizeof(kMagic)))
        return fail("bad magic number (not an Atlas index)");

    std::uint32_t version = 0;
    if (!r.u32(version) || version != kVersion)
        return fail("unsupported index version");

    InvertedIndex loaded;  // only swapped into `index` on full success
    std::size_t total_length = 0;

    std::uint32_t document_count = 0;
    if (!r.u32(document_count) || document_count > r.remaining() / 8)
        return fail("invalid document count");

    for (std::uint32_t i = 0; i < document_count; ++i) {
        std::int32_t document_id = 0;
        std::int32_t length = 0;
        if (!r.i32(document_id) || !r.i32(length) || length < 0)
            return fail("invalid document entry");
        if (!loaded.document_lengths_.emplace(document_id, length).second)
            return fail("duplicate document id");
        total_length += static_cast<std::size_t>(length);
    }

    std::uint32_t term_count = 0;
    if (!r.u32(term_count) || term_count > r.remaining() / 8)
        return fail("invalid term count");

    for (std::uint32_t t = 0; t < term_count; ++t) {
        std::uint32_t term_length = 0;
        if (!r.u32(term_length) || term_length == 0 ||
            term_length > r.remaining())
            return fail("invalid term length");

        std::string term;
        if (!r.bytes(term, term_length)) return fail("truncated term");

        std::uint32_t posting_count = 0;
        if (!r.u32(posting_count) || posting_count == 0 ||
            posting_count > r.remaining() / 8)
            return fail("invalid posting count");

        auto [term_it, inserted] =
            loaded.index_.emplace(std::move(term), InvertedIndex::PostingMap{});
        if (!inserted) return fail("duplicate term");

        for (std::uint32_t p = 0; p < posting_count; ++p) {
            std::int32_t document_id = 0;
            std::uint32_t position_count = 0;
            if (!r.i32(document_id) || !r.u32(position_count))
                return fail("truncated posting");

            auto length_it = loaded.document_lengths_.find(document_id);
            if (length_it == loaded.document_lengths_.end())
                return fail("posting refers to unknown document");
            if (position_count == 0 || position_count > r.remaining() / 4)
                return fail("invalid position count");

            std::vector<int> positions;
            positions.reserve(position_count);
            int previous = -1;
            for (std::uint32_t k = 0; k < position_count; ++k) {
                std::int32_t position = 0;
                if (!r.i32(position)) return fail("truncated positions");
                if (position <= previous || position >= length_it->second)
                    return fail("positions out of order or out of range");
                previous = position;
                positions.push_back(position);
            }
            if (!term_it->second.emplace(document_id, std::move(positions))
                     .second)
                return fail("duplicate posting");
        }
    }

    if (r.remaining() != 0) return fail("unexpected trailing data");

    loaded.total_document_length_ = total_length;
    index = std::move(loaded);
    return true;
}
