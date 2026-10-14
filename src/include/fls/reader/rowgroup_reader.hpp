// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/include/fls/reader/rowgroup_reader.hpp
// ────────────────────────────────────────────────────────
#ifndef FLS_READER_ROWGROUP_READER_HPP
#define FLS_READER_ROWGROUP_READER_HPP

#include "fls/common/alias.hpp"                   // for up, n_t
#include "fls/cor/lyt/buf.hpp"                    // for Buf
#include "fls/expression/physical_expression.hpp" // for PhysicalExpr
#include "fls/io/io.hpp"
#include "fls/reader/rowgroup_reader.hpp"
#include "fls/std/filesystem.hpp" // for path
#include "fls/std/vector.hpp"     // for vector
#include "fls/table/chunk.hpp"    // for Chunk
#include <memory>

namespace fastlanes {
/*--------------------------------------------------------------------------------------------------------------------*/
class Connection;
class RowgroupView;
class Rowgroup;
class BufPool;
/*--------------------------------------------------------------------------------------------------------------------*/
class RowgroupReader {
public:
	explicit RowgroupReader(const io&                 io,
	                        const RowgroupDescriptor& rowgroup_descriptor,
	                        Connection&               fls,
	                        const std::vector<idx_t>& column_ids,
	                        std::shared_ptr<BufPool>  buffer_pool);
	explicit RowgroupReader(const path&               file_path,
	                        const RowgroupDescriptor& rowgroup_descriptor,
	                        Connection&               fls,
	                        std::shared_ptr<BufPool>  buffer_pool);

public:
	vector<sp<PhysicalExpr>>& get_chunk(n_t vec_idx);
	///
	void reset();
	///!
	up<Rowgroup> materialize();
	///
	void to_csv(const path& dir_path);
	///
	[[nodiscard]] const RowgroupDescriptor& get_descriptor() const;
	///!
	[[nodiscard]] vector<string> get_column_names() const;
	///
	[[nodiscard]] vector<DataType> get_data_types() const;

public:
	vector<sp<PhysicalExpr>> m_expressions;

private:
	void Initialize(const path& file_path);

	void NormalizeColumnIds();

	Connection&                       m_connection;
	const RowgroupDescriptor&         m_rowgroup_descriptor;
	std::shared_ptr<BufPool>          m_buffer_pool;
	std::vector<std::shared_ptr<Buf>> m_column_bufs;
	up<RowgroupView>                  m_rowgroup_view;
	std::vector<idx_t>                m_column_ids;
};

} // namespace fastlanes

#endif
