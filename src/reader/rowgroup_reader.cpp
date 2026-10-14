// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/reader/rowgroup_reader.cpp
// ────────────────────────────────────────────────────────
#include "fls/reader/rowgroup_reader.hpp" //
#include "fls/common/alias.hpp"
#include "fls/connection.hpp"  // for Connector (ptr only)
#include "fls/cor/lyt/buf.hpp" // for Buf
#include "fls/cor/lyt/buf_pool.hpp"
#include "fls/csv/csv.hpp"                        // for CSV
#include "fls/encoder/materializer.hpp"           //
#include "fls/expression/decoding_operator.hpp"   //
#include "fls/expression/encoding_operator.hpp"   //
#include "fls/expression/expression_executor.hpp" //
#include "fls/expression/interpreter.hpp"         // for Interpreter
#include "fls/expression/physical_expression.hpp" // for PhysicalExpr
#include "fls/footer/rowgroup_descriptor.hpp"     // for Footer, ColumnMeta...
#include "fls/io/file.hpp"                        // for File
#include "fls/io/io.hpp"                          // for IO, io
#include "fls/reader/column_view.hpp"
#include "fls/reader/rowgroup_view.hpp"
#include "fls/std/filesystem.hpp"
#include "fls/table/chunk.hpp" // for Chunk
#include <memory>              // for make_unique, uniqu...
#include <numeric>
#include <optional>

namespace fastlanes {

RowgroupReader::RowgroupReader(const io&                 io,
                               const RowgroupDescriptor& rowgroup_descriptor,
                               Connection&               connection,
                               const std::vector<idx_t>& column_ids,
                               std::shared_ptr<BufPool>  buffer_pool)
    : m_connection(connection)
    , m_rowgroup_descriptor(rowgroup_descriptor)
    , m_buffer_pool(std::move(buffer_pool))
    , m_column_ids(column_ids) {

	// constexpr uint64_t GAP_TO_MERGE   = 128ull * 1024;      // merge gaps ≤ 128 KiB
	// constexpr uint64_t MAX_MERGE_READ = 8ull * 1024 * 1024; // cap merged span ≤ 8 MiB

	// read file
	// {
	// 	io io_handle = make_unique<File>(file_path);
	//
	// 	// We’ll fill this with slices that reference merged read buffers.
	// 	std::unordered_map<idx_t, ColumnBufferReference> column_map;
	// 	column_map.reserve(m_column_ids.size());
	//
	// 	// We can still keep buffers here if other code expects it, but now they’re merged reads,
	// 	// not per-column buffers.
	// 	m_column_bufs.clear();
	//
	// 	ThreadPool& pool = IOExecutor::pool();
	//
	// 	// 1) Build chunks (one per projected column in this rowgroup)
	// 	struct Chunk {
	// 		idx_t    col_idx;
	// 		uint64_t off; // file offset (absolute)
	// 		uint64_t len; // bytes to read for this column
	// 	};
	// 	std::vector<Chunk> chunks;
	// 	chunks.reserve(m_column_ids.size());
	// 	for (const auto& col_idx : m_column_ids) {
	// 		const auto&    cd         = m_rowgroup_descriptor.m_column_descriptors[col_idx];
	// 		const uint64_t col_size   = cd->total_size;
	// 		const uint64_t col_offset = m_rowgroup_descriptor.m_offset + cd->column_offset;
	// 		chunks.push_back(Chunk {col_idx, col_offset, col_size});
	// 	}
	//
	// 	if (!chunks.empty()) {
	// 		// 2) Sort by file offset
	// 		std::sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) { return a.off < b.off; });
	//
	// 		// 3) Coalesce into merged reads
	// 		struct SubSlice {
	// 			idx_t    col_idx;
	// 			uint32_t rel;
	// 			uint32_t len;
	// 		}; // rel = offset within merged span
	// 		struct Merged {
	// 			uint64_t              off;
	// 			uint64_t              len;
	// 			std::vector<SubSlice> subs;
	// 		};
	// 		std::vector<Merged> merged;
	// 		merged.reserve(chunks.size());
	//
	// 		uint64_t cur_off = chunks[0].off;
	// 		uint64_t cur_end = chunks[0].off + chunks[0].len;
	// 		Merged   cur {cur_off, 0, {}};
	// 		cur.subs.push_back(SubSlice {chunks[0].col_idx, 0u, static_cast<uint32_t>(chunks[0].len)});
	//
	// 		auto flush_cur = [&]() {
	// 			cur.len = cur_end - cur.off;
	// 			merged.push_back(std::move(cur));
	// 		};
	//
	// 		for (size_t i = 1; i < chunks.size(); ++i) {
	// 			const auto&    c              = chunks[i];
	// 			const uint64_t gap            = (c.off > cur_end) ? (c.off - cur_end) : 0;
	// 			const uint64_t span_if_merged = (c.off + c.len) - cur.off;
	//
	// 			const bool can_merge_gap   = gap <= GAP_TO_MERGE;
	// 			const bool within_max_span = span_if_merged <= MAX_MERGE_READ;
	//
	// 			if (can_merge_gap && within_max_span) {
	// 				// extend current merged span
	// 				const uint32_t rel = static_cast<uint32_t>(c.off - cur.off);
	// 				cur.subs.push_back(SubSlice {c.col_idx, rel, static_cast<uint32_t>(c.len)});
	// 				cur_end = std::max(cur_end, c.off + c.len);
	// 			} else {
	// 				// close current, start new
	// 				flush_cur();
	// 				cur.off = c.off;
	// 				cur_end = c.off + c.len;
	// 				cur.subs.clear();
	// 				cur.subs.push_back(SubSlice {c.col_idx, 0u, static_cast<uint32_t>(c.len)});
	// 			}
	// 		}
	// 		flush_cur();
	//
	// 		// 4) Submit one read per merged span; on completion, populate column_map with slices
	// 		struct Job {
	// 			std::shared_ptr<Buf>  buf;
	// 			std::vector<SubSlice> subs;
	// 			uint64_t              off;
	// 		};
	// 		std::vector<std::future<Job>> futs;
	// 		futs.reserve(merged.size());
	//
	// 		for (auto& m : merged) {
	// 			futs.emplace_back(pool.submit([&, off = m.off, len = m.len, subs = m.subs]() mutable -> Job {
	// 				auto merged_buf = std::make_shared<Buf>(len);     // TODO: take from a buffer pool
	// 				IO::range_read(io_handle, *merged_buf, off, len); // single pread loop
	// 				return Job {std::move(merged_buf), std::move(subs), off};
	// 			}));
	// 		}
	//
	// 		// 5) Collect; create per-column views that alias the merged buffer (no copy)
	// 		for (auto& fut : futs) {
	// 			Job job = fut.get();
	//
	// 			// Keep at least one owner reference around if your code expects m_column_bufs to hold buffers
	// 			m_column_bufs.push_back(job.buf);
	//
	// 			const auto base_span = job.buf->Span(); // std::span<std::byte> or similar
	// 			for (const auto& s : job.subs) {
	// 				auto slice_span = base_span.subspan(s.rel, s.len);
	// 				// aliasing owner keeps merged buffer alive as long as any ColumnBufferReference exists
	// 				auto owner = std::shared_ptr<void>(job.buf, job.buf.get());
	// 				column_map.emplace(s.col_idx, ColumnBufferReference {slice_span, std::move(owner)});
	// 			}
	// 		}
	// 	}
	//
	// 	m_rowgroup_view = make_unique<RowgroupView>(column_map, m_rowgroup_descriptor);
	// }

	// // read file
	// {
	// 	io io_handle = make_unique<File>(file_path); // your IO variant wrapping File
	// 	std::unordered_map<idx_t, ColumnBufferReference> column_map;
	// 	column_map.reserve(m_column_ids.size());
	// 	m_column_bufs.reserve(m_column_ids.size());
	//
	// 	// pool size: min(#cols, HW threads) but at least 1
	// 	// const size_t hw = std::max(1u, std::thread::hardware_concurrency());
	// 	// ThreadPool   pool(std::min(m_column_ids.size(), static_cast<size_t>(hw)));
	// 	ThreadPool* pool_ptr = &IOExecutor::pool();
	// 	ThreadPool& pool     = *pool_ptr;
	//
	// 	struct Job {
	// 		idx_t                col_idx;
	// 		std::shared_ptr<Buf> buf;
	// 	};
	// 	std::vector<std::future<Job>> futs;
	// 	futs.reserve(m_column_ids.size());
	//
	// 	for (const auto& col_idx : m_column_ids) {
	// 		const auto& column_descriptor = m_rowgroup_descriptor.m_column_descriptors[col_idx];
	// 		const auto  column_size       = column_descriptor->total_size;
	// 		const auto  column_offset     = m_rowgroup_descriptor.m_offset + column_descriptor->column_offset;
	//
	// 		futs.emplace_back(pool.submit([&, col_idx, column_size, column_offset]() -> Job {
	// 			auto buffer = std::make_shared<Buf>(column_size);               // todo[memory_pool]
	// 			IO::range_read(io_handle, *buffer, column_offset, column_size); // calls File::ReadRange -> pread
	// 			return Job {col_idx, std::move(buffer)};
	// 		}));
	// 	}
	//
	// 	// Collect results (order-independent)
	// 	for (auto& fut : futs) {
	// 		auto job = fut.get();
	// 		m_column_bufs.push_back(job.buf);
	// 		column_map.emplace(job.col_idx,
	// 		                   ColumnBufferReference {job.buf->Span(), std::shared_ptr<void>(job.buf, job.buf.get())});
	// 	}
	//
	// 	m_rowgroup_view = make_unique<RowgroupView>(column_map, m_rowgroup_descriptor);
	// }

	// IoTracer::get().dump_summary("pread");
	{
		const size_t n_physical_cols = m_rowgroup_descriptor.m_column_descriptors()->size();
		std::vector<std::optional<ColumnBufferReference>> columns(n_physical_cols);

		m_column_bufs.reserve(m_column_ids.size());

		for (const auto& col_idx : m_column_ids) {
			const auto& cd         = m_rowgroup_descriptor.m_column_descriptors()->Get(col_idx);
			auto        col_size   = cd->total_size();
			auto        col_offset = m_rowgroup_descriptor.m_offset() + cd->column_offset();

			auto buffer = m_buffer_pool ? m_buffer_pool->Acquire(col_size) : std::make_shared<Buf>(col_size);
			IO::range_read(io, *buffer, col_offset, col_size);
			buffer->Advance(col_size);
			m_column_bufs.push_back(buffer);

			columns[col_idx] = ColumnBufferReference {buffer->Span(), std::shared_ptr<void>(buffer, buffer.get())};
		}

		m_rowgroup_view = make_unique<RowgroupView>(columns, m_rowgroup_descriptor);
	}

	// {
	// 	// allocate buffer
	// 	std::unordered_map<idx_t, ColumnBufferReference> column_map;
	// 	column_map.reserve(m_column_ids.size());
	// 	m_column_bufs.reserve(m_column_ids.size());
	//
	// 	for (const auto& col_idx : m_column_ids) {
	// 		auto& column_descriptor = m_rowgroup_descriptor.m_column_descriptors[col_idx];
	// 		auto  column_size       = column_descriptor->total_size;
	// 		auto  column_offset     = m_rowgroup_descriptor.m_offset + column_descriptor->column_offset;
	//
	// 		auto buffer = std::make_shared<Buf>(column_size); // todo[memory_pool]
	// 		IO::range_read(io, *buffer, column_offset, column_size);
	// 		m_column_bufs.push_back(buffer);
	// 		column_map.emplace(col_idx,
	// 		                   ColumnBufferReference {buffer->Span(), std::shared_ptr<void>(buffer, buffer.get())});
	// 	}
	//
	// 	m_rowgroup_view = make_unique<RowgroupView>(column_map, m_rowgroup_descriptor);
	// }

	// init level 1 expression
	{
		m_expressions.reserve(m_column_ids.size());
		for (const auto& col_idx : m_column_ids) {
			const auto& column_descriptor = m_rowgroup_descriptor.m_column_descriptors()->Get(col_idx);
			auto&       column_view       = (*m_rowgroup_view)[col_idx];

			InterpreterState state;
			auto             physical_expr = make_decoding_expression(*column_descriptor, column_view, *this, state);
			ExprExecutor::CountOperator(*physical_expr);
			m_expressions.emplace_back(physical_expr);
		}
	}
}

RowgroupReader::RowgroupReader(const path&               file_path,
                               const RowgroupDescriptor& rowgroup_descriptor,
                               Connection&               connection,
                               std::shared_ptr<BufPool>  buffer_pool)
    : m_connection(connection)
    , m_rowgroup_descriptor(rowgroup_descriptor)
    , m_buffer_pool(std::move(buffer_pool)) {

	m_column_ids.resize(m_rowgroup_descriptor.m_column_descriptors()->size());
	std::iota(m_column_ids.begin(), m_column_ids.end(), idx_t {0});

	// read file
	{
		io           io              = make_unique<File>(file_path);
		const size_t n_physical_cols = m_rowgroup_descriptor.m_column_descriptors()->size();
		std::vector<std::optional<ColumnBufferReference>> columns(n_physical_cols);

		m_column_bufs.reserve(m_column_ids.size());

		for (const auto& col_idx : m_column_ids) {
			const auto& cd         = m_rowgroup_descriptor.m_column_descriptors()->Get(col_idx);
			auto        col_size   = cd->total_size();
			auto        col_offset = m_rowgroup_descriptor.m_offset() + cd->column_offset();

			auto buffer = m_buffer_pool ? m_buffer_pool->Acquire(col_size) : std::make_shared<Buf>(col_size);
			IO::range_read(io, *buffer, col_offset, col_size);
			buffer->Advance(col_size);
			m_column_bufs.push_back(buffer);

			columns[col_idx] = ColumnBufferReference {buffer->Span(), std::shared_ptr<void>(buffer, buffer.get())};
		}

		m_rowgroup_view = make_unique<RowgroupView>(columns, m_rowgroup_descriptor);
	}
	// {
	// 	// allocate buffer
	// 	io                                               io     = make_unique<File>(file_path); // todo[IO]
	// 	uint64_t                                         offset = m_rowgroup_descriptor.m_offset;
	// 	std::unordered_map<idx_t, ColumnBufferReference> column_map;
	// 	column_map.reserve(m_column_ids.size());
	//
	// 	for (const auto& col_idx : m_column_ids) {
	// 		auto& column_descriptor = m_rowgroup_descriptor.m_column_descriptors[col_idx];
	// 		auto  column_size       = column_descriptor->total_size;
	//
	// 		auto buffer = std::make_shared<Buf>(column_size); // todo[memory_pool]
	// 		IO::range_read(io, *buffer, offset, column_size);
	// 		offset += column_size;
	// 		m_column_bufs.push_back(buffer);
	// 		column_map.emplace(col_idx,
	// 		                   ColumnBufferReference {buffer->Span(), std::shared_ptr<void>(buffer, buffer.get())});
	// 	}
	//
	// 	m_rowgroup_view = make_unique<RowgroupView>(column_map, m_rowgroup_descriptor);
	// }

	// init level 1 expression[
	{
		m_expressions.reserve(m_column_ids.size());
		for (const auto& col_idx : m_column_ids) {
			const auto& column_descriptor = m_rowgroup_descriptor.m_column_descriptors()->Get(col_idx);
			auto&       column_view       = (*m_rowgroup_view)[col_idx];

			InterpreterState state;
			auto             physical_expr = make_decoding_expression(*column_descriptor, column_view, *this, state);
			ExprExecutor::CountOperator(*physical_expr);
			m_expressions.emplace_back(physical_expr);
		}
	}
}

vector<sp<PhysicalExpr>>& RowgroupReader::get_chunk(const n_t vec_idx) {
	for (idx_t expr_idx {0}; expr_idx < m_expressions.size(); ++expr_idx) {
		auto& physical_expr = *m_expressions[expr_idx];
		ExprExecutor::smart_execute(physical_expr, vec_idx);
	}
	return m_expressions;
}

void RowgroupReader::reset() {
}

const RowgroupDescriptor& RowgroupReader::get_descriptor() const {
	return m_rowgroup_descriptor;
}

up<Rowgroup> RowgroupReader::materialize() {
	// Convert FlatBuffers table -> native T
	auto rg_native = up<RowgroupDescriptorT>(m_rowgroup_descriptor.UnPack());

	// Construct Rowgroup from the native descriptor
	// TODO: Capacity is not used
	auto               rowgroup_up = std::make_unique<Rowgroup>(*rg_native, 0);
	const Materializer materializer {*rowgroup_up};

	const n_t n_vec = static_cast<n_t>(m_rowgroup_descriptor.m_n_vec());
	for (n_t vec_idx {0}; vec_idx < n_vec; vec_idx++) {
		auto& expressions = get_chunk(vec_idx);
		materializer.Materialize(expressions, vec_idx);
	}

	// materializer.rowgroup.Cast();
	materializer.rowgroup.Finalize();
	materializer.rowgroup.GetStatistics();

	return rowgroup_up;
}

void RowgroupReader::to_csv(const path& dir_path) {
	const auto& materialized_rowgroup = materialize();
	CSV::to_csv(dir_path, *materialized_rowgroup, materialized_rowgroup->m_descriptor);
}

} // namespace fastlanes
