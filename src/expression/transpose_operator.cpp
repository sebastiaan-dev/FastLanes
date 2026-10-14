// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/expression/transpose_operator.cpp
// ────────────────────────────────────────────────────────
#include "fls/expression/transpose_operator.hpp"
#include "fls/cfg/cfg.hpp"
#include "fls/common/alias.hpp"
#include "fls/common/assert.hpp"
#include "fls/common/common.hpp"
#include "fls/expression/data_type.hpp"
#include "fls/expression/encoding_operator.hpp"
#include "fls/expression/fsst12_expression.hpp"
#include "fls/expression/fsst_expression.hpp"
#include "fls/expression/interpreter.hpp"
#include "fls/expression/physical_expression.hpp"
#include "fls/expression/rle_expression.hpp"
#include "fls/expression/rsum_operator.hpp"
#include "fls/reader/segment.hpp"
#include "fls/std/variant.hpp"
#include "fls/table/rowgroup.hpp"
#include "fls_gen/transpose/transpose.hpp"
#include "fls_gen/untranspose/untranspose.hpp"
#include <variant>

namespace fastlanes {
/*--------------------------------------------------------------------------------------------------------------------*\
 * enc transpose opr
\*--------------------------------------------------------------------------------------------------------------------*/
template <typename PT>
enc_transpose_opr<PT>::enc_transpose_opr(const PhysicalExpr& expr,
                                         const col_pt&       col,
                                         ColumnDescriptorT&  column_descriptor,
                                         InterpreterState&   state) {

	const auto& input = expr.operators[state.cur_operator++];
	visit(overloaded {
	          [&](const sp<enc_uncompressed_opr<PT>>& opr) {
		          data = opr->data;
		          opr->segment->MakeTemporary();
	          },
	          [&](const sp<enc_scan_opr<PT>>& opr) { data = opr->data; },
	          [&](const sp<enc_fsst_opr>& opr) {
		          if constexpr (std::is_same_v<PT, ofs_t>) {
			          data = opr->fsst_encoded_offset_arr + 1;
			          opr->fsst_offset_segment->MakeTemporary();
			          column_descriptor.encoding_rpn->operand_tokens.pop_back();
			          state.cur_operand -= 1;
		          } else {
			          FLS_UNREACHABLE();
		          }
	          },
	          [&](const sp<enc_fsst12_opr>& opr) {
		          if constexpr (std::is_same_v<PT, ofs_t>) {
			          data = opr->fsst12_encoded_offset_arr + 1;
			          opr->fsst12_offset_segment->MakeTemporary();
			          column_descriptor.encoding_rpn->operand_tokens.pop_back();
			          state.cur_operand -= 1;
		          } else {
			          FLS_UNREACHABLE();
		          }
	          },
	          [&]<typename VALUE_PT>(const sp<enc_dict_map_opr<VALUE_PT, PT>>& opr) { data = opr->index_arr; },
	          [&]<typename VALUE_PT>(const sp<enc_rle_map_opr<VALUE_PT, PT>>& opr) { data = opr->rle_idxs; },
	          [&](std::monostate&) { FLS_UNREACHABLE(); },
	          [&](auto&) { ThrowUnexpectedPhysicalOperator(input, __FILE__, __LINE__); },
	      },
	      input);
}

template <typename PT>
void enc_transpose_opr<PT>::Transpose() {
	::generated::transpose::fallback::scalar::transpose_i(data, transposed_data);
}

template struct enc_transpose_opr<u08_pt>;
template struct enc_transpose_opr<u16_pt>;
template struct enc_transpose_opr<u32_pt>;
template struct enc_transpose_opr<u64_pt>;
template struct enc_transpose_opr<i08_pt>;
template struct enc_transpose_opr<i16_pt>;
template struct enc_transpose_opr<i32_pt>;
template struct enc_transpose_opr<i64_pt>;
/*--------------------------------------------------------------------------------------------------------------------*\
 * dec_transpose_opr
\*--------------------------------------------------------------------------------------------------------------------*/
template <typename PT>
dec_transpose_opr<PT>::dec_transpose_opr(PhysicalExpr&     physical_expr,
                                         const ColumnView& column_view,
                                         InterpreterState& state)
    : transposed_data(nullptr) {

	const auto& input = physical_expr.operators.back();
	if (const auto* rsum = std::get_if<sp<dec_rsum_opr<PT>>>(&input)) {
		transposed_data = (*rsum)->idxs;
		FLS_ASSERT_NOT_NULL_POINTER(transposed_data)
	} else {
		ThrowUnexpectedPhysicalOperator(input, __FILE__, __LINE__);
	}
}

template <typename PT>
void dec_transpose_opr<PT>::Materialize(n_t vec_idx, TypedCol<PT>& typed_col) {
	typed_col.data.resize(typed_col.data.size() + CFG::VEC_SZ);
	PT* untrasposed_data_p = typed_col.data.data() + (CFG::VEC_SZ * vec_idx);
	generated::untranspose::fallback::scalar::untranspose_i(transposed_data, untrasposed_data_p);
}

template struct dec_transpose_opr<u08_pt>;
template struct dec_transpose_opr<u16_pt>;
template struct dec_transpose_opr<u32_pt>;
template struct dec_transpose_opr<u64_pt>;
template struct dec_transpose_opr<i08_pt>;
template struct dec_transpose_opr<i16_pt>;
template struct dec_transpose_opr<i32_pt>;
template struct dec_transpose_opr<i64_pt>;
} // namespace fastlanes
