// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// test/standalone/variant_dispatch_test.cpp
// ────────────────────────────────────────────────────────
#include "fls/expression/analyze_operator.hpp"
#include "fls/expression/encoding_operator.hpp"
#include "fls/expression/interpreter.hpp"
#include "fls/expression/physical_expression.hpp"
#include "fls/expression/rsum_operator.hpp"
#include "fls/expression/slpatch_operator.hpp"
#include "fls/expression/transpose_operator.hpp"
#include "fls/footer/rowgroup_descriptor_generated.h"
#include "fls/reader/column_view.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
using namespace fastlanes;

void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

// Three real, aligned segment views, with a non-identity operand mapping. No
// generated datasets or fabricated operator objects are needed by these tests.
struct Fixture {
	flatbuffers::FlatBufferBuilder builder;
	alignas(64) uint64_t storage[3 * CFG::VEC_SZ + 1] {};

	Fixture() {
		const uint32_t segment_size = CFG::VEC_SZ * sizeof(uint64_t);
		std::memcpy(storage, &segment_size, sizeof(segment_size));
		RowgroupDescriptorT rowgroup;
		auto                column           = std::make_unique<ColumnDescriptorT>();
		column->encoding_rpn                 = std::make_unique<RPNT>();
		column->encoding_rpn->operand_tokens = {2, 0, 1};
		for (n_t i = 0; i < 3; ++i) {
			auto segment             = std::make_unique<SegmentDescriptorT>();
			segment->entry_point_t   = EntryPointType::UINT32;
			segment->entrypoint_size = sizeof(uint32_t);
			segment->data_offset     = sizeof(uint64_t) + i * CFG::VEC_SZ * sizeof(uint64_t);
			segment->data_size       = CFG::VEC_SZ * sizeof(uint64_t);
			column->segment_descriptors.push_back(std::move(segment));
		}
		rowgroup.m_column_descriptors.push_back(std::move(column));
		builder.Finish(RowgroupDescriptor::Pack(builder, &rowgroup));
	}

	ColumnView View() {
		const auto& rowgroup = *flatbuffers::GetRoot<RowgroupDescriptor>(builder.GetBufferPointer());
		return ColumnView({reinterpret_cast<std::byte*>(storage), sizeof(storage)},
		                  *rowgroup.m_column_descriptors()->Get(0),
		                  rowgroup,
		                  0);
	}

	void SetSegment(n_t index, const void* data, size_t size) {
		std::memcpy(storage + 1 + index * CFG::VEC_SZ, data, size);
	}
};

InterpreterState State(n_t operand = 2) {
	InterpreterState state;
	state.cur_operator = 0;
	state.cur_operand  = operand;
	return state;
}

template <typename PT>
void TestAccepted() {
	using UPT = std::make_unsigned_t<PT>;
	Fixture           fixture;
	auto              view = fixture.View();
	PhysicalExpr      expr;
	ColumnDescriptorT descriptor;
	descriptor.encoding_rpn = std::make_unique<RPNT>();
	col_pt column           = std::make_unique<TypedCol<PT>>();
	auto   state            = State();
	auto   scan             = std::make_shared<enc_scan_opr<PT>>(expr, column, descriptor, state);
	for (n_t i = 0; i < CFG::VEC_SZ; ++i) {
		scan->data[i] = static_cast<PT>(static_cast<int>(i % 61) - (std::is_signed_v<PT> ? 30 : 0));
	}
	expr.operators.emplace_back(scan);
	// Encoding rsum selects cur_operator, not back().
	expr.operators.emplace_back(std::monostate {});
	auto rsum = std::make_shared<enc_rsum_opr<PT>>(expr, column, descriptor, state);
	Check(rsum->data == scan->data, "rsum scan input");
	Check(state.cur_operator == 1 && state.cur_operand == 3, "encoding rsum cursors");
	Check(descriptor.encoding_rpn->operand_tokens.back() == 2, "encoding rsum operand");
	expr.operators.pop_back();
	state          = State();
	auto transpose = std::make_shared<enc_transpose_opr<PT>>(expr, column, descriptor, state);
	transpose->Transpose();
	expr.operators       = {transpose};
	state                = State();
	auto transposed_rsum = std::make_shared<enc_rsum_opr<PT>>(expr, column, descriptor, state);
	Check(transposed_rsum->data == transpose->transposed_data, "rsum transpose input");
	transposed_rsum->Rsum();

	// Slpatch encoding consumes the last operator, without advancing cur_operator.
	expr.operators = {scan};
	auto analyze   = std::make_shared<enc_analyze_opr<PT, true>>(expr, column, descriptor, state);
	expr.operators.emplace_back(analyze);
	state = State(5);
	enc_slpatch_opr<PT> enc_patch(expr, column, descriptor, state);
	Check(enc_patch.n_exceptions_p == &analyze->n_exceptions && enc_patch.exceptions == analyze->exceptions &&
	          enc_patch.exception_pos_arr == analyze->exception_pos_arr,
	      "slpatch analyze input");
	Check(state.cur_operator == 0 && state.cur_operand == 8, "encoding slpatch cursors");
	const auto& tokens = descriptor.encoding_rpn->operand_tokens;
	Check(tokens[tokens.size() - 3] == 5 && tokens[tokens.size() - 2] == 6 && tokens.back() == 7,
	      "encoding slpatch operands");

	state          = State();
	auto unffor    = std::make_shared<dec_unffor_opr<UPT>>(view, state);
	expr.operators = {std::monostate {}, unffor};
	state          = State();
	auto patch     = std::make_shared<dec_slpatch_opr<PT>>(expr, view, state);
	Check(patch->data == reinterpret_cast<PT*>(unffor->unffored_data), "slpatch unffor input");
	Check(state.cur_operator == 0 && state.cur_operand == 2 - n_t(3), "decoding slpatch cursors");
	const PT       exception = static_cast<PT>(std::is_signed_v<PT> ? -42 : 42);
	const uint16_t position  = 7;
	const uint16_t count     = 1;
	fixture.SetSegment(2, &exception, sizeof(exception));
	fixture.SetSegment(0, &position, sizeof(position));
	fixture.SetSegment(1, &count, sizeof(count));
	patch->Patch(0);
	Check(patch->data[position] == exception, "slpatch segment mapping and patch");

	// Both direct decode inputs, and nested expressions selecting operators[0].
	state               = State();
	auto unsigned_patch = std::make_shared<dec_slpatch_opr<UPT>>(expr, view, state);
	for (bool patched : {false, true}) {
		physical_operator input = patched ? physical_operator(unsigned_patch) : physical_operator(unffor);
		for (int depth = 0; depth < 3; ++depth) {
			expr.operators = {std::monostate {}, input};
			state          = State();
			auto decoded   = std::make_shared<dec_rsum_opr<PT>>(expr, view, state);
			Check(decoded->deltas == reinterpret_cast<const PT*>(unffor->unffored_data), "decoding rsum input");
			Check(state.cur_operator == 1 && state.cur_operand == 1, "decoding rsum cursors");
			std::memcpy(unffor->unffored_data, transposed_rsum->deltas, sizeof(transposed_rsum->deltas));
			fixture.SetSegment(2, transpose->transposed_data, CFG::UNIFIED_TRANSPOSED::BASES_SIZE);
			decoded->Unrsum(0);
			PhysicalExpr transpose_expr;
			transpose_expr.operators = {std::monostate {}, decoded};
			dec_transpose_opr<PT> decoded_transpose(transpose_expr, view, state);
			Check(decoded_transpose.transposed_data == decoded->idxs, "decoding transpose input");
			Check(state.cur_operator == 1 && state.cur_operand == 1, "decoding transpose cursors");
			TypedCol<PT> materialized;
			decoded_transpose.Materialize(0, materialized);
			for (n_t i = 0; i < CFG::VEC_SZ; ++i) {
				Check(materialized.data[i] == scan->data[i], "transpose/rsum round trip");
			}
			auto nested       = std::make_shared<PhysicalExpr>();
			nested->operators = {input, std::monostate {}};
			input             = nested;
		}
	}
}

template <typename PT>
void TestInvalid(const std::string& operation, const std::string& alternative) {
	Fixture           fixture;
	auto              view = fixture.View();
	PhysicalExpr      expr;
	ColumnDescriptorT descriptor;
	descriptor.encoding_rpn = std::make_unique<RPNT>();
	col_pt column           = std::make_unique<TypedCol<PT>>();
	// An unrelated null smart pointer must be diagnosed by type, not dereferenced.
	physical_operator invalid;
	std::string       expected_type = "monostate";
	if (alternative == "unrelated") {
		if (operation == "enc_transpose") {
			invalid       = sp<dec_fsst_opr> {};
			expected_type = "dec_fsst_opr";
		} else {
			invalid       = sp<enc_fsst_opr> {};
			expected_type = "enc_fsst_opr";
		}
	} else if (alternative == "mismatch") {
		using OtherPT = std::conditional_t<sizeof(PT) == 1, u16_pt, u08_pt>;
		if (operation == "enc_rsum" || operation == "enc_transpose") {
			invalid       = sp<enc_scan_opr<OtherPT>> {};
			expected_type = "enc_scan_opr";
		} else if (operation == "enc_slpatch") {
			invalid       = sp<enc_analyze_opr<PT, false>> {};
			expected_type = "enc_analyze_opr";
		} else if (operation == "dec_slpatch") {
			invalid       = sp<dec_unffor_opr<OtherPT>> {};
			expected_type = "dec_unffor_opr";
		} else if (operation == "dec_transpose") {
			invalid       = sp<dec_rsum_opr<OtherPT>> {};
			expected_type = "dec_rsum_opr";
		} else {
			// Rsum accepts unsigned patch buffers, even for a signed result.
			invalid       = sp<dec_slpatch_opr<std::make_signed_t<PT>>> {};
			expected_type = "dec_slpatch_opr";
		}
	}
	expr.operators = {invalid};
	if (operation == "nested_rsum") {
		auto nested       = std::make_shared<PhysicalExpr>();
		nested->operators = {invalid};
		expr.operators    = {nested};
	}
	auto state = State();
	try {
		if (operation == "enc_rsum") {
			enc_rsum_opr<PT> opr(expr, column, descriptor, state);
		} else if (operation == "enc_transpose") {
			enc_transpose_opr<PT> opr(expr, column, descriptor, state);
		} else if (operation == "enc_slpatch") {
			enc_slpatch_opr<PT> opr(expr, column, descriptor, state);
		} else if (operation == "dec_rsum" || operation == "nested_rsum") {
			dec_rsum_opr<PT> opr(expr, view, state);
		} else if (operation == "dec_slpatch") {
			dec_slpatch_opr<PT> opr(expr, view, state);
		} else if (operation == "dec_transpose") {
			dec_transpose_opr<PT> opr(expr, view, state);
		} else {
			throw std::invalid_argument("Unknown test operation");
		}
	} catch (const std::runtime_error& error) {
		const std::string message = error.what();
		Check(message.find("UNREACHABLE") != std::string::npos, "missing invalid-state diagnostic");
		const auto file = operation.find("slpatch") != std::string::npos     ? "slpatch_operator.cpp"
		                  : operation.find("transpose") != std::string::npos ? "transpose_operator.cpp"
		                                                                     : "rsum_operator.cpp";
		Check(message.find(file) != std::string::npos, "missing callsite diagnostic");
		Check(message.find(expected_type) != std::string::npos, "missing unexpected type diagnostic");
		Check(state.cur_operator == ((operation == "enc_rsum" || operation == "enc_transpose") ? 1U : 0U),
		      "invalid-state operator cursor");
		Check(state.cur_operand == (operation == "dec_slpatch" ? 2 - n_t(3) : 2), "invalid-state operand cursor");
		Check(descriptor.encoding_rpn->operand_tokens.empty(), "invalid encoding mutated operand tokens");
		return;
	}
	throw std::runtime_error("Invalid alternative was accepted");
}

template <typename PT>
void Run(const std::string& operation, const std::string& alternative) {
	if (operation.empty()) {
		TestAccepted<PT>();
	} else {
		TestInvalid<PT>(operation, alternative);
	}
}
} // namespace

int main(int argc, char** argv) {
	if (argc == 2 && std::string(argv[1]) == "--assertions") {
#ifdef NDEBUG
		std::cout << "disabled";
#else
		std::cout << "enabled";
#endif
		return 0;
	}
	try {
		const std::string operation   = argc > 1 ? argv[1] : "";
		const std::string alternative = argc > 2 ? argv[2] : "monostate";
		const std::string type        = argc > 3 ? argv[3] : "all";
#define RUN_TYPE(PT)                                                                                                   \
	if (type == "all" || type == #PT) {                                                                                \
		Run<PT>(operation, alternative);                                                                               \
	}
		RUN_TYPE(i08_pt)
		RUN_TYPE(i16_pt)
		RUN_TYPE(i32_pt)
		RUN_TYPE(i64_pt)
		RUN_TYPE(u08_pt)
		RUN_TYPE(u16_pt)
		RUN_TYPE(u32_pt)
		RUN_TYPE(u64_pt)
#undef RUN_TYPE
		std::cout << "Variant dispatch checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
