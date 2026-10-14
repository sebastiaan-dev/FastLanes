// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/include/fls/reader/column_view.hpp
// ────────────────────────────────────────────────────────
#ifndef FLS_READER_COLUMN_VIEW_HPP
#define FLS_READER_COLUMN_VIEW_HPP

#include "fls/common/alias.hpp"
#include "fls/std/span.hpp"
#include "fls/std/vector.hpp"

namespace fastlanes {
/*--------------------------------------------------------------------------------------------------------------------*/
struct ColumnDescriptor;
struct RowgroupDescriptor;
class SegmentView;
/*--------------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------------*\
 * ColumnView
\*--------------------------------------------------------------------------------------------------------------------*/
class ColumnView {
public:
	explicit ColumnView(span<std::byte>           column_span,
	                    const ColumnDescriptor&   column_descriptor,
	                    const RowgroupDescriptor& rowgroup_descriptor,
	                    uint64_t                  column_offset,
	                    std::shared_ptr<void>     column_owner = nullptr);
	[[nodiscard]] SegmentView GetSegment(n_t segment_idx) const;

public:
	span<std::byte>         column_span;
	const ColumnDescriptor& column_descriptor;
	uint64_t                base_offset;
	vector<ColumnView>      children;
	std::shared_ptr<void>   owner;
};

} // namespace fastlanes

#endif // FLS_READER_COLUMN_VIEW_HPP
