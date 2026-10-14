// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/reader/rowgroup_view.cpp
// ────────────────────────────────────────────────────────
#include "fls/reader/rowgroup_view.hpp"
#include "fls/common/alias.hpp"
#include "fls/common/assert.hpp"
#include "fls/footer/rowgroup_descriptor_generated.h"
#include "fls/reader/column_view.hpp"
#include "fls/std/span.hpp"
#include <cstddef>  // for std::byte
#include <optional> // for std::optional

namespace fastlanes {

RowgroupView::RowgroupView(const std::vector<std::optional<ColumnBufferReference>>& cols_by_id,
                           const RowgroupDescriptor&                                footer) {
	columns.resize(cols_by_id.size());

	for (idx_t id = 0; id < static_cast<idx_t>(cols_by_id.size()); ++id) {
		if (!cols_by_id[id])
			continue;

		const auto& [data, owner] = *cols_by_id[id];
		const auto& cd            = *footer.m_column_descriptors()->Get(id);

		columns[id] = std::make_unique<ColumnView>(data, cd, footer, cd.column_offset(), owner);
	}
}

ColumnView& RowgroupView::operator[](const n_t col_idx) {
	FLS_ASSERT_NOT_EMPTY_VEC(columns)

	return *columns[col_idx];
}

const ColumnView& RowgroupView::operator[](const n_t col_idx) const {
	FLS_ASSERT_NOT_EMPTY_VEC(columns)

	return *columns[col_idx];
}
} // namespace fastlanes
