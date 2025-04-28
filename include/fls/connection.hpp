#ifndef FLS_CONNECTION_HPP
#define FLS_CONNECTION_HPP

#include "fls/common/alias.hpp"             // for up, idx_t
#include "fls/encoder/rowgroup_encoder.hpp" // for RowgroupEncodingResult
#include "fls/encoder/single_col_decoder.hpp"
#include "fls/encoder/single_col_encoder.hpp"
#include "fls/footer/rowgroup_footer.hpp" // for Footer
#include "fls/reader/column_view.hpp"     //
#include "fls/reader/reader.hpp"          // for Reader
#include "fls/reader/segment.hpp"
#include "fls/std/filesystem.hpp" // for path
#include "fls/std/vector.hpp"     // for vector
#include "fls/table/rowgroup.hpp" // for Rowgroup

namespace fastlanes {
    /*--------------------------------------------------------------------------------------------------------------------*/
    class Dir;
    /*--------------------------------------------------------------------------------------------------------------------*/
    /*--------------------------------------------------------------------------------------------------------------------*\
     * Config
    \*--------------------------------------------------------------------------------------------------------------------*/
    //! The configuration for a connection.
    class Config {
    public:
        Config();

    public:
        //! Whether the current connection enforces an encoding schema pool.
        bool is_forced_schema_pool;
        vector<OperatorToken> forced_schema_pool;
        //! Whether the current connection enforces an encoding schema.
        bool is_forced_schema;
        vector<OperatorToken> forced_schema;
        n_t sample_size;
    };

    /*--------------------------------------------------------------------------------------------------------------------*\
     * FLS
    \*--------------------------------------------------------------------------------------------------------------------*/
    /*!
     * The Connection class provides an external interface for reading and writing
     * the FastLanes file format.
     */
    class Connection {
    public:
        friend class column;
        friend class equal;
        friend class RowgroupEncoder;
        friend class Wizard;
        friend class NewEncoder;

    public:
        //! Create a connection with a default configuration.
        Connection();

        //! Create a connection with a predefined configuration.
        explicit Connection(const Config &config);

    public:
        /*!
         * Reads a directory containing a data file which is either a csv or json, and a schema definition.
         * The data file is then parsed into a FastLanes representation, in memory associated with the connection.
         *
         * @param dir_path Path pointing to the directory containing the data file and schema.
         */
        Connection &read(const path &dir_path);

        /*!
         * Creates a reader for a FastLanes file represented by a directory containing a binary data file and a schema.
         *
         * @param dir_path Path pointing to the directory containing the data file and schema.
         */
        Reader &read_fls(const path &dir_path);

        ///!
        Connection &spell();

        /*!
         * Writes the in-memory representation of FastLanes to a file. This function should only be called when
         * the current connection has called read or read_fls beforehand.
         *
         * @param dir_path Target directory to write the in-memory data of the current connection.
         */
        Connection &to_fls(const path &dir_path);

        //! verify if encodings works. TODO: What?
        void verify_encoding();

        /*!
         * Set the current connection to a clean state. This function should be used between subsequent read or read_fls
         * calls, otherwise the connection might reuse the footer of a previous read operation on a subsequent read.
         */
        Connection &reset();

        //! TODO
        Connection &project(const vector<idx_t> &idxs);

        /*!
         * Whether the current connection enforces an encoding pool, this affects how the footer is constructed
         * when writing from a non-FastLanes file to a FastLanes format.
         */
        bool is_forced_schema_pool() const;

        /*!
         * Whether the current connection enforces an encoding schema, this affects how the footer is constructed
         * when writing from a non-FastLanes file to a FastLanes format.
         */
        bool is_forced_schema() const;

        //! Get the enforced schema pool of the current connection.
        const vector<OperatorToken> &get_forced_schema_pool() const;

        //! Constrains the encoding scheme from a predefined set of operators. This function should be called before
        //! reading a file with an external format. TODO: What does a schema pool do?
        Connection &force_schema_pool(const vector<OperatorToken> &operator_token);

        /**
         * TODO: What does sample_size do?
         * A sample size of 0 is reserved for forcing fastlanes to use the entire row group as the sample size.
         * If the specified sample size exceeds the row group size, it is capped at the row group size.
         *
         * @param n_vecs The desired sample size in terms of vector count. Use 0 to default to the row group size.
         */
        Connection &set_sample_size(n_t n_vecs);

        //! TODO
        n_t get_sample_size() const;

        //! TODO: What does this do?
        Connection &force_schema(const vector<OperatorToken> &operator_token);

        //! Get the enforced schema of the current connection.
        const vector<OperatorToken> &get_forced_schema() const;

    public:
        //! TODO
        static void encode_from_memory(void *input_p,
                                       n_t n_tup,
                                       n_t capacity,
                                       void *encoded_p,
                                       bsz_t *encoded_byte_size,
                                       const DataType &data_type,
                                       n_t expr_id);

        //! TODO
        static void decode_to_memory(void *encoded_p, void *decoded_p, const DataType &data_type);

        //! TODO
        [[nodiscard]] Rowgroup &rowgroup() const;

    private:
        /**
         * Constructs the internal state of the current connection after the external facing read has constructed
         * a directory object based on the given path.
         *
         * @param dir Directory which contains references to the FastLanes files.
         */
        Connection &read(const Dir &dir);

        void prepare_rowgroup();

    private:
        //! In-memory representation of FastLanes.
        up<Rowgroup> m_rowgroup;
        //! In-memory representation of the FastLanes footer.
        up<Footer> m_rowgroup_footer;
        //! Reader for a FastLanes file.
        up<Reader> m_reader;
        up<RowgroupEncodingResult> m_encoding_result;
        //! Configuration of the current connection.
        up<Config> m_config;
    };

    constexpr static auto const *FOOTER_FILE_NAME{"fls_footer.json"};
    constexpr static auto const *PROFILING_FILE_NAME{"fls_profiling.json"};
    constexpr static auto const *FASTLANES_FILE_NAME{"data.fls"};
    constexpr static auto const *SCHEMA_FILE_NAME{"schema.json"};
} // namespace fastlanes

#endif
