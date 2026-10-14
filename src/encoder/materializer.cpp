// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/encoder/materializer.cpp
// ────────────────────────────────────────────────────────
#include "fls/encoder/materializer.hpp"
#include "fls/cfg/cfg.hpp"
#include "fls/common/alias.hpp"
#include "fls/common/assert.hpp"
#include "fls/common/common.hpp"
#include "fls/common/string.hpp"
#include "fls/expression/alp_expression.hpp"
#include "fls/expression/cross_rle_operator.hpp"
#include "fls/expression/decoding_operator.hpp"
#include "fls/expression/dict_expression.hpp"
#include "fls/expression/frequency_operator.hpp"
#include "fls/expression/fsst12_dict_operator.hpp"
#include "fls/expression/fsst12_expression.hpp"
#include "fls/expression/fsst_dict_operator.hpp"
#include "fls/expression/fsst_expression.hpp"
#include "fls/expression/null_operator.hpp"
#include "fls/expression/physical_expression.hpp"
#include "fls/expression/rle_expression.hpp"
#include "fls/expression/slpatch_operator.hpp"
#include "fls/expression/transpose_operator.hpp"
#include "fls/expression/validitymask_operator.hpp"
#include "fls/primitive/copy/fls_copy.hpp"
#include "fls/reader/segment.hpp"
#include "fls/table/rowgroup.hpp"
#include <cstdint>
#include <cstring>     // for std::memcpy
#include <type_traits> // for std::make_signed_t
#include <utility>
#include <variant>

namespace fastlanes {

Materializer::Materializer(Rowgroup& rowgroup)
    : rowgroup(rowgroup) {
}

namespace {

void MaterializeOperator(const physical_operator& opr, col_pt& col, n_t vec_idx);

[[noreturn]] void ThrowMaterializationMismatch(const physical_operator& opr, const col_pt& col) {
	const auto operator_type =
	    std::visit([](const auto& arg) { return type_name<std::decay_t<decltype(arg)>>(); }, opr);
	const auto column_type = std::visit([](const auto& arg) { return type_name<std::decay_t<decltype(arg)>>(); }, col);
	const auto msg = "[FLS] UNREACHABLE with unexpected types: " + operator_type + " and " + column_type + " at " +
	                 __FILE__ + ":" + std::to_string(__LINE__);
	FLS_ABORT(msg.c_str())
	throw std::runtime_error(msg);
}

struct material_visitor {
	explicit material_visitor(n_t vec_idx)
	    : vec_idx(vec_idx) {};

	template <typename PT>
	void operator()(const sp<dec_uncompressed_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			// fixme (reserve and memcopy);
			typed_col->data.push_back(opr->Data()[idx]);
		}
	}
	template <typename PT>
	void operator()(const sp<dec_unffor_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		typed_col->data.resize(typed_col->data.size() + CFG::VEC_SZ);
		copy<PT>(opr->Data(), typed_col->data.data() + vec_idx * CFG::VEC_SZ);
	}
	template <typename PT>
	void operator()(const sp<dec_unffor_opr<PT>>& opr, up<TypedCol<std::make_signed_t<PT>>>& typed_col) const {
		typed_col->data.resize(typed_col->data.size() + CFG::VEC_SZ);
		copy<PT>(opr->Data(), reinterpret_cast<PT*>(typed_col->data.data()) + vec_idx * CFG::VEC_SZ);
	}
	template <typename PT>
	void operator()(const sp<dec_alp_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			typed_col->data.push_back(opr->decoded_arr[idx]);
		}
	}
	template <typename PT>
	void operator()(const sp<dec_alp_rd_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		typed_col->data.resize(typed_col->data.size() + CFG::VEC_SZ);
		copy<PT>(opr->glue_arr, typed_col->data.data() + vec_idx * CFG::VEC_SZ);
	}
	template <typename PT>
	void operator()(const sp<dec_constant_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		// fixme (reserve and set);
		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			typed_col->data.push_back(opr->value);
		}
	}
	void operator()(const sp<dec_constant_str_opr>& opr, up<FLSStrColumn>& str_col) const {
		const auto constant_value_size = static_cast<len_t>(opr->bytes.size());

		for (n_t idx = 0; idx < CFG::VEC_SZ; ++idx) {
			str_col->length_arr.push_back(constant_value_size);
		}

		// optimize
		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			str_col->byte_arr.insert(str_col->byte_arr.end(), opr->bytes.begin(), opr->bytes.end());
		}
	}
	template <typename PT>
	static const physical_operator& NestedOperator(const sp<PhysicalExpr>& expr, const up<TypedCol<PT>>&) {
		return expr->operators[expr->operators.size() - 1];
	}
	static const physical_operator& NestedOperator(const sp<PhysicalExpr>& expr, const up<FLSStrColumn>&) {
		return expr->operators[0];
	}
	void operator()(const sp<dec_struct_opr>& struct_expr, up<Struct>& struct_col) const {
		for (n_t expr_idx {0}; expr_idx < struct_expr->internal_exprs.size(); ++expr_idx) {
			MaterializeOperator(struct_expr->internal_exprs[expr_idx]
			                        ->operators[struct_expr->internal_exprs[expr_idx]->operators.size() - 1],
			                    struct_col->internal_rowgroup[expr_idx],
			                    vec_idx);
		}
	}
	void operator()(const sp<dec_fls_str_uncompressed_opr>& opr, up<FLSStrColumn>& str_col) const {
		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			str_col->length_arr.push_back(opr->Length()[idx]);
		}
		const n_t old_size = str_col->byte_arr.size(); // Save the current size
		const n_t new_size = opr->byte_arr_segment.Size();
		str_col->byte_arr.resize(old_size + new_size);
		std::memcpy(str_col->byte_arr.data() + old_size, opr->Data(), new_size);
	}
	void operator()(const sp<dec_fsst_opr>& opr, up<FLSStrColumn>& str_col) const {
		opr->Decode(str_col->byte_arr, str_col->length_arr);
	}
	void operator()(const sp<dec_fsst12_opr>& opr, up<FLSStrColumn>& str_col) const {
		opr->Decode(str_col->byte_arr, str_col->length_arr);
	}
	// DICT
	template <typename KEY_PT, typename INDEX_PT>
	void operator()(const sp<dec_dict_opr<KEY_PT, INDEX_PT>>& dict_expr, up<TypedCol<KEY_PT>>& typed_col) const {
		const auto* key_p   = dict_expr->Keys();
		const auto* index_p = dict_expr->Index();

		for (n_t idx {0}; idx < CFG::VEC_SZ; ++idx) {
			typed_col->data.push_back(key_p[index_p[idx]]);
		}
	}
	template <typename INDEX_PT>
	void operator()(const sp<dec_dict_opr<fls_string_t, INDEX_PT>>& opr, up<FLSStrColumn>& str_col) const {
		opr->Decode(str_col->byte_arr, str_col->length_arr);
	}
	template <typename INDEX_PT>
	void operator()(const sp<dec_fsst_dict_opr<INDEX_PT>>& opr, up<FLSStrColumn>& str_col) const {
		opr->Decode(str_col->byte_arr, str_col->length_arr);
	}
	template <typename INDEX_PT>
	void operator()(const sp<dec_fsst12_dict_opr<INDEX_PT>>& opr, up<FLSStrColumn>& str_col) const {
		opr->Decode(str_col->byte_arr, str_col->length_arr);
	}
	template <typename KEY_PT, typename INDEX_PT>
	void operator()(const sp<dec_rle_map_opr<KEY_PT, INDEX_PT>>& opr, up<TypedCol<KEY_PT>>& typed_col) const {
		opr->Decode(vec_idx, typed_col->data);
	}
	template <typename INDEX_PT>
	void operator()(const sp<dec_rle_map_opr<fls_string_t, INDEX_PT>>& opr, up<FLSStrColumn>& typed_col) const {
		opr->Decode(vec_idx, typed_col->byte_arr, typed_col->length_arr);
	}

	template <typename PT>
	void operator()(const sp<dec_null_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	template <typename PT>
	void operator()(const sp<dec_transpose_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	template <typename PT>
	void operator()(const sp<dec_slpatch_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	template <typename PT>
	void operator()(const sp<dec_frequency_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	void operator()(const sp<dec_frequency_str_opr>& opr, up<FLSStrColumn>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	template <typename PT>
	void operator()(const sp<dec_cross_rle_opr<PT>>& opr, up<TypedCol<PT>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	void operator()(const sp<dec_cross_rle_opr<fls_string_t>>& opr, up<FLSStrColumn>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	void operator()(const sp<dec_validitymask_opr>& opr, up<TypedCol<uint8_t>>& typed_col) const {
		opr->Materialize(vec_idx, *typed_col);
	}
	// There is intentionally no catch-all materialization overload: invocability
	// below is the single source of truth for accepted operator/column pairs.
	n_t vec_idx;
};

template <typename Col, typename Opr>
bool TryMaterialize(const material_visitor& visitor, const Opr& opr, col_pt& col) {
	if constexpr (std::is_invocable_v<const material_visitor&, const Opr&, Col&>) {
		if (auto* typed_col = std::get_if<Col>(&col)) {
			visitor(opr, *typed_col);
			return true;
		}
	}
	return false;
}

template <typename Opr, size_t... I>
bool TryMaterialize(const material_visitor& visitor, const Opr& opr, col_pt& col, std::index_sequence<I...>) {
	// The fold is a compile-time filter, not a runtime scan of physical_operator.
	// Ordinary operators emit one column check; unffor emits unsigned and signed
	// checks using exactly the same overload resolution as the original visitor.
	return (TryMaterialize<std::variant_alternative_t<I, col_pt>>(visitor, opr, col) || ...);
}

void MaterializeOperator(const physical_operator& opr, col_pt& col, n_t vec_idx) {
	// get_if alone would treat a valueless column as a mismatch. Preserve the
	// bad_variant_access that the original two-variant visit would throw instead.
	if (col.valueless_by_exception()) {
		throw std::bad_variant_access();
	}
	if (const auto* expr = std::get_if<sp<PhysicalExpr>>(&opr)) {
		const auto& nested = std::visit(
		    [&](const auto& typed_col) -> const physical_operator& {
			    if constexpr (requires { material_visitor::NestedOperator(*expr, typed_col); }) {
				    return material_visitor::NestedOperator(*expr, typed_col);
			    } else {
				    ThrowMaterializationMismatch(opr, col);
			    }
		    },
		    col);
		MaterializeOperator(nested, col, vec_idx);
		return;
	}
	const bool matched = std::visit(
	    [&](const auto& typed_opr) {
		    return TryMaterialize(
		        material_visitor {vec_idx}, typed_opr, col, std::make_index_sequence<std::variant_size_v<col_pt>> {});
	    },
	    opr);
	if (!matched) {
		ThrowMaterializationMismatch(opr, col);
	}
}

} // namespace

void Materializer::Materialize(const vector<sp<PhysicalExpr>>& expressions, n_t vec_idx) const {
	FLS_ASSERT_E(expressions.size(), rowgroup.internal_rowgroup.size());

	for (n_t col_idx {0}; col_idx < expressions.size(); ++col_idx) {
		const auto& expr = expressions[col_idx];
		auto&       col  = rowgroup.internal_rowgroup[col_idx];

		FLS_ASSERT_NOT_EMPTY_VEC(expr->operators);

		expr->PointTo(vec_idx);
		MaterializeOperator(expr->operators[expr->operators.size() - 1], col, vec_idx);
	}

	// rowgroup.n_tup = rowgroup.n_tup + CFG::VEC_SZ;
}

} // namespace fastlanes
