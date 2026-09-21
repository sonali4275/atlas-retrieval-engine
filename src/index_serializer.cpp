#include "index_serializer.h"

#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>


bool IndexSerializer::save(
    const InvertedIndex& index,
    const std::string& filename
) {
    std::ofstream output(
        filename,
        std::ios::binary
    );

    if (!output) {
        return false;
    }

    /*
     * ============================================================
     * FILE FORMAT VERSION
     * ============================================================
     */

    const std::uint32_t version = 1;

    output.write(
        reinterpret_cast<const char*>(&version),
        sizeof(version)
    );


    /*
     * ============================================================
     * DOCUMENT COUNT
     * ============================================================
     */

    const std::uint32_t document_count =
        static_cast<std::uint32_t>(
            index.document_lengths_.size()
        );

    output.write(
        reinterpret_cast<const char*>(&document_count),
        sizeof(document_count)
    );


    /*
     * ============================================================
     * DOCUMENT LENGTHS
     * ============================================================
     */

    for (const auto& entry :
         index.document_lengths_) {

        const std::int32_t document_id =
            static_cast<std::int32_t>(
                entry.first
            );

        const std::int32_t document_length =
            static_cast<std::int32_t>(
                entry.second
            );

        output.write(
            reinterpret_cast<const char*>(&document_id),
            sizeof(document_id)
        );

        output.write(
            reinterpret_cast<const char*>(&document_length),
            sizeof(document_length)
        );
    }


    /*
     * ============================================================
     * NUMBER OF TERMS
     * ============================================================
     */

    const std::uint32_t term_count =
        static_cast<std::uint32_t>(
            index.index_.size()
        );

    output.write(
        reinterpret_cast<const char*>(&term_count),
        sizeof(term_count)
    );


    /*
     * ============================================================
     * TERMS AND POSTINGS
     * ============================================================
     */

    for (const auto& term_entry :
         index.index_) {

        const std::string& term =
            term_entry.first;


        /*
         * --------------------------------------------------------
         * TERM
         * --------------------------------------------------------
         */

        const std::uint32_t term_length =
            static_cast<std::uint32_t>(
                term.size()
            );

        output.write(
            reinterpret_cast<const char*>(&term_length),
            sizeof(term_length)
        );

        output.write(
            term.data(),
            term_length
        );


        /*
         * --------------------------------------------------------
         * POSTING COUNT
         * --------------------------------------------------------
         */

        const std::uint32_t posting_count =
            static_cast<std::uint32_t>(
                term_entry.second.size()
            );

        output.write(
            reinterpret_cast<const char*>(&posting_count),
            sizeof(posting_count)
        );


        /*
         * --------------------------------------------------------
         * POSTINGS
         * --------------------------------------------------------
         */

        for (const auto& posting_entry :
             term_entry.second) {

            const std::int32_t document_id =
                static_cast<std::int32_t>(
                    posting_entry.first
                );

            output.write(
                reinterpret_cast<const char*>(&document_id),
                sizeof(document_id)
            );


            /*
             * ----------------------------------------------------
             * POSITION COUNT
             * ----------------------------------------------------
             */

            const std::uint32_t position_count =
                static_cast<std::uint32_t>(
                    posting_entry.second.size()
                );

            output.write(
                reinterpret_cast<const char*>(&position_count),
                sizeof(position_count)
            );


            /*
             * ----------------------------------------------------
             * POSITIONS
             * ----------------------------------------------------
             */

            for (int position :
                 posting_entry.second) {

                const std::int32_t stored_position =
                    static_cast<std::int32_t>(
                        position
                    );

                output.write(
                    reinterpret_cast<const char*>(
                        &stored_position
                    ),
                    sizeof(stored_position)
                );
            }
        }
    }


    /*
     * ============================================================
     * VERIFY WRITE
     * ============================================================
     */

    return output.good();
}


bool IndexSerializer::load(
    InvertedIndex& index,
    const std::string& filename
) {
    std::ifstream input(
        filename,
        std::ios::binary
    );

    if (!input) {
        return false;
    }


    /*
     * ============================================================
     * CLEAR EXISTING INDEX
     * ============================================================
     */

    index.index_.clear();
    index.document_lengths_.clear();

    /*
     * Reset derived/performance state as well.
     */
    index.total_document_length_ = 0;
    index.cached_average_document_length_ = 0.0;
    index.average_length_cached_ = false;
    index.idf_cache_.clear();


    /*
     * ============================================================
     * READ FILE VERSION
     * ============================================================
     */

    std::uint32_t version = 0;

    input.read(
        reinterpret_cast<char*>(&version),
        sizeof(version)
    );

    if (!input || version != 1) {
        return false;
    }


    /*
     * ============================================================
     * READ DOCUMENT COUNT
     * ============================================================
     */

    std::uint32_t document_count = 0;

    input.read(
        reinterpret_cast<char*>(&document_count),
        sizeof(document_count)
    );

    if (!input) {
        return false;
    }


    /*
     * ============================================================
     * READ DOCUMENT LENGTHS
     * ============================================================
     */

    for (std::uint32_t i = 0;
         i < document_count;
         ++i) {

        std::int32_t document_id = 0;
        std::int32_t document_length = 0;

        input.read(
            reinterpret_cast<char*>(&document_id),
            sizeof(document_id)
        );

        input.read(
            reinterpret_cast<char*>(&document_length),
            sizeof(document_length)
        );

        if (!input) {
            return false;
        }

        index.document_lengths_[
            static_cast<int>(document_id)
        ] =
            static_cast<int>(document_length);
    }


    /*
     * ============================================================
     * REBUILD TOTAL DOCUMENT LENGTH
     * ============================================================
     *
     * This value is derived from document_lengths_ and is required
     * by BM25 scoring.
     */

    index.total_document_length_ = 0;

    for (const auto& entry :
         index.document_lengths_) {

        index.total_document_length_ +=
            static_cast<std::size_t>(
                entry.second
            );
    }


    /*
     * ============================================================
     * READ TERM COUNT
     * ============================================================
     */

    std::uint32_t term_count = 0;

    input.read(
        reinterpret_cast<char*>(&term_count),
        sizeof(term_count)
    );

    if (!input) {
        return false;
    }


    /*
     * ============================================================
     * READ TERMS AND POSTINGS
     * ============================================================
     */

    for (std::uint32_t i = 0;
         i < term_count;
         ++i) {

        /*
         * --------------------------------------------------------
         * READ TERM LENGTH
         * --------------------------------------------------------
         */

        std::uint32_t term_length = 0;

        input.read(
            reinterpret_cast<char*>(&term_length),
            sizeof(term_length)
        );

        if (!input) {
            return false;
        }


        /*
         * --------------------------------------------------------
         * READ TERM
         * --------------------------------------------------------
         */

        std::string term(
            term_length,
            '\0'
        );

        if (term_length > 0) {

            input.read(
                term.data(),
                term_length
            );

            if (!input) {
                return false;
            }
        }


        /*
         * --------------------------------------------------------
         * READ POSTING COUNT
         * --------------------------------------------------------
         */

        std::uint32_t posting_count = 0;

        input.read(
            reinterpret_cast<char*>(&posting_count),
            sizeof(posting_count)
        );

        if (!input) {
            return false;
        }


        /*
         * --------------------------------------------------------
         * READ POSTINGS
         * --------------------------------------------------------
         */

        for (std::uint32_t j = 0;
             j < posting_count;
             ++j) {

            std::int32_t document_id = 0;

            input.read(
                reinterpret_cast<char*>(&document_id),
                sizeof(document_id)
            );

            if (!input) {
                return false;
            }


            /*
             * ----------------------------------------------------
             * READ POSITION COUNT
             * ----------------------------------------------------
             */

            std::uint32_t position_count = 0;

            input.read(
                reinterpret_cast<char*>(&position_count),
                sizeof(position_count)
            );

            if (!input) {
                return false;
            }


            /*
             * ----------------------------------------------------
             * READ POSITIONS
             * ----------------------------------------------------
             */

            std::vector<int> positions;

            positions.reserve(
                position_count
            );

            for (std::uint32_t k = 0;
                 k < position_count;
                 ++k) {

                std::int32_t position = 0;

                input.read(
                    reinterpret_cast<char*>(&position),
                    sizeof(position)
                );

                if (!input) {
                    return false;
                }

                positions.push_back(
                    static_cast<int>(position)
                );
            }


            /*
             * ----------------------------------------------------
             * STORE POSTING
             * ----------------------------------------------------
             */

            index.index_[term][
                static_cast<int>(document_id)
            ] =
                std::move(positions);
        }
    }


    /*
     * ============================================================
     * RESET BM25 CACHES
     * ============================================================
     *
     * total_document_length_ has already been rebuilt above.
     *
     * Average document length and IDF are calculated lazily by
     * InvertedIndex, so their caches must be invalidated.
     */

    index.cached_average_document_length_ = 0.0;
    index.average_length_cached_ = false;

    index.idf_cache_.clear();


    /*
     * ============================================================
     * VERIFY READ
     * ============================================================
     */

    return input.good() || input.eof();
}