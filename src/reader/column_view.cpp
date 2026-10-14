// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/reader/column_view.cpp
// ────────────────────────────────────────────────────────
#include "fls/reader/column_view.hpp"
#include "fls/common/alias.hpp"
#include "fls/common/assert.hpp"
#include "fls/footer/column_descriptor_generated.h"
#include "fls/reader/segment.hpp"
#include "fls/std/span.hpp"
#include <cstddef> // for std::byte
#include <cstdint> // for std::uint32_t
#include <memory>
#include <utility>

namespace fastlanes {

ColumnView::ColumnView(const span<std::byte>     column_span,
                       const ColumnDescriptor&   column_descriptor,
                       const RowgroupDescriptor& rowgroup_descriptor,
                       const uint64_t            column_offset,
                       std::shared_ptr<void>     column_owner)
    : column_span(column_span)
    , column_descriptor(column_descriptor)
    , base_offset(column_offset)
    , owner(std::move(column_owner)) {
	if (!column_descriptor.children()) {
		return;
	}
	FLS_ASSERT_NOT_NULL_POINTER(column_descriptor.children())

	for (n_t child_col_idx {0}; child_col_idx < column_descriptor.children()->size(); ++child_col_idx) {

		auto& child_column_descriptor = *(*column_descriptor.children())[static_cast<uint32_t>(child_col_idx)];
		children.emplace_back(column_span, child_column_descriptor, rowgroup_descriptor, base_offset, owner);
	}
}

SegmentView ColumnView::GetSegment(n_t segment_idx) const {
	FLS_ASSERT_L(segment_idx, column_descriptor.segment_descriptors()->size());

	return make_segment_view(column_span,
	                         *(*column_descriptor.segment_descriptors())[static_cast<uint32_t>(segment_idx)],
	                         base_offset,
	                         owner);
}

} // namespace fastlanes
