// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// test/standalone/materializer_dispatch_test.cpp
// ────────────────────────────────────────────────────────
#include "fls/common/common.hpp"
#include "fls/encoder/materializer.hpp"
#include "fls/expression/decoding_operator.hpp"
#include "fls/expression/interpreter.hpp"
#include "fls/expression/physical_expression.hpp"
#include "fls/footer/rowgroup_descriptor_generated.h"
#include "fls/reader/column_view.hpp"
#include "fls/table/rowgroup.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace {
using namespace fastlanes;

void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Fixture {
	flatbuffers::FlatBufferBuilder builder;
	alignas(64) uint64_t storage[3 * CFG::VEC_SZ + 1] {};

	Fixture(const void* constant, size_t size) {
		const uint32_t segment_size = CFG::VEC_SZ * sizeof(uint64_t);
		std::memcpy(storage, &segment_size, sizeof(segment_size));
		RowgroupDescriptorT descriptor;
		auto                column = std::make_unique<ColumnDescriptorT>();
		column->max                = std::make_unique<BinaryValueT>();
		const auto* bytes          = static_cast<const uint8_t*>(constant);
		column->max->binary_data.assign(bytes, bytes + size);
		column->encoding_rpn                 = std::make_unique<RPNT>();
		column->encoding_rpn->operand_tokens = {0, 1, 2};
		for (n_t i = 0; i < 3; ++i) {
			auto segment             = std::make_unique<SegmentDescriptorT>();
			segment->entry_point_t   = EntryPointType::UINT32;
			segment->entrypoint_size = sizeof(uint32_t);
			segment->data_offset     = sizeof(uint64_t) + i * CFG::VEC_SZ * sizeof(uint64_t);
			segment->data_size       = CFG::VEC_SZ * sizeof(uint64_t);
			column->segment_descriptors.push_back(std::move(segment));
		}
		descriptor.m_column_descriptors.push_back(std::move(column));
		builder.Finish(RowgroupDescriptor::Pack(builder, &descriptor));
	}

	ColumnView View() {
		const auto& descriptor = *flatbuffers::GetRoot<RowgroupDescriptor>(builder.GetBufferPointer());
		return ColumnView({reinterpret_cast<std::byte*>(storage), sizeof(storage)},
		                  *descriptor.m_column_descriptors()->Get(0),
		                  descriptor,
		                  0);
	}
};

sp<PhysicalExpr> Expression(const physical_operator& opr) {
	auto expr = std::make_shared<PhysicalExpr>();
	expr->operators.push_back(opr);
	return expr;
}

void Materialize(const sp<PhysicalExpr>& expr, Rowgroup& rowgroup, n_t vec_idx = 0) {
	Materializer(rowgroup).Materialize({expr}, vec_idx);
}

template <typename PT>
void TestNumeric() {
	const PT expected = static_cast<PT>(std::is_signed_v<PT> ? -7 : 7);
	Fixture  fixture(&expected, sizeof(expected));
	auto     view     = fixture.View();
	auto     constant = std::make_shared<dec_constant_opr<PT>>(view);
	// Numeric nested expressions must select the last operator, at every depth.
	physical_operator input = constant;
	for (int depth = 0; depth < 3; ++depth) {
		Rowgroup rowgroup(RowgroupDescriptorT {}, 2 * CFG::VEC_SZ);
		rowgroup.internal_rowgroup.emplace_back(std::make_unique<TypedCol<PT>>());
		const auto expr = Expression(input);
		Materialize(expr, rowgroup);
		Materialize(expr, rowgroup, 1);
		const auto& data = std::get<up<TypedCol<PT>>>(rowgroup.internal_rowgroup[0])->data;
		Check(data.size() == 2 * CFG::VEC_SZ, "constant append size");
		for (const auto value : data) {
			Check(value == expected, "constant value / nested last selection");
		}
		auto nested = Expression(std::monostate {});
		nested->operators.push_back(input);
		input = nested;
	}

	// Exercise the raw-copy materializer through a real SegmentView.
	for (n_t i = 0; i < CFG::VEC_SZ; ++i) {
		std::memcpy(reinterpret_cast<uint8_t*>(fixture.storage + 1) + i * sizeof(PT), &expected, sizeof(PT));
	}
	auto     uncompressed = std::make_shared<dec_uncompressed_opr<PT>>(view, 0);
	Rowgroup rowgroup(RowgroupDescriptorT {}, CFG::VEC_SZ);
	rowgroup.internal_rowgroup.emplace_back(std::make_unique<TypedCol<PT>>());
	Materialize(Expression(uncompressed), rowgroup);
	const auto& data = std::get<up<TypedCol<PT>>>(rowgroup.internal_rowgroup[0])->data;
	Check(data.size() == CFG::VEC_SZ, "uncompressed size");
	for (const auto value : data) {
		Check(value == expected, "uncompressed value");
	}
}

template <typename PT>
void TestUnffor() {
	using UPT               = std::make_unsigned_t<PT>;
	const UPT        unused = 0;
	Fixture          fixture(&unused, sizeof(unused));
	auto             view = fixture.View();
	InterpreterState state;
	state.cur_operand = 2;
	auto     opr      = std::make_shared<dec_unffor_opr<UPT>>(view, state);
	Rowgroup rowgroup(RowgroupDescriptorT {}, 2 * CFG::VEC_SZ);
	rowgroup.internal_rowgroup.emplace_back(std::make_unique<TypedCol<PT>>());
	// Nested input avoids PointTo advancing the one-vector fixture at vec_idx=1.
	const auto expr = Expression(Expression(opr));
	for (n_t vec_idx = 0; vec_idx < 2; ++vec_idx) {
		for (n_t i = 0; i < CFG::VEC_SZ; ++i) {
			const PT value =
			    static_cast<PT>(static_cast<int>(i % 53) - (std::is_signed_v<PT> ? 26 : 0) + static_cast<int>(vec_idx));
			std::memcpy(&opr->unffored_data[i], &value, sizeof(PT));
		}
		Materialize(expr, rowgroup, vec_idx);
	}
	const auto& data = std::get<up<TypedCol<PT>>>(rowgroup.internal_rowgroup[0])->data;
	Check(data.size() == 2 * CFG::VEC_SZ, "unffor append size");
	for (n_t i = 0; i < data.size(); ++i) {
		const PT expected = static_cast<PT>(static_cast<int>((i % CFG::VEC_SZ) % 53) - (std::is_signed_v<PT> ? 26 : 0) +
		                                    static_cast<int>(i / CFG::VEC_SZ));
		Check(data[i] == expected, "signed/unsigned unffor value or append offset");
	}
}

void TestString() {
	const std::string expected("x\0y", 3);
	Fixture           fixture(expected.data(), expected.size());
	auto              view  = fixture.View();
	physical_operator input = std::make_shared<dec_constant_str_opr>(view);
	for (int depth = 0; depth < 3; ++depth) {
		Rowgroup rowgroup(RowgroupDescriptorT {}, 2 * CFG::VEC_SZ);
		rowgroup.internal_rowgroup.emplace_back(std::make_unique<FLSStrColumn>());
		const auto expr = Expression(input);
		Materialize(expr, rowgroup);
		Materialize(expr, rowgroup, 1);
		const auto& col = *std::get<up<FLSStrColumn>>(rowgroup.internal_rowgroup[0]);
		Check(col.length_arr.size() == 2 * CFG::VEC_SZ, "string append lengths");
		Check(col.byte_arr.size() == 2 * CFG::VEC_SZ * expected.size(), "string append bytes");
		for (n_t i = 0; i < col.length_arr.size(); ++i) {
			Check(col.length_arr[i] == expected.size(), "constant string length");
			Check(std::memcmp(col.byte_arr.data() + i * expected.size(), expected.data(), expected.size()) == 0,
			      "constant string bytes / nested first selection");
		}
		auto nested = Expression(input);
		nested->operators.emplace_back(std::monostate {});
		input = nested;
	}
}

// Invalid materialization aborts with assertions enabled. The --invalid mode
// lets a subprocess test runner exercise the exact same diagnostic checks.
void TestInvalid(const std::string& kind) {
	physical_operator input  = sp<dec_constant_opr<u32_pt>> {};
	col_pt            col    = std::make_unique<TypedCol<i32_pt>>();
	bool              nested = false;
	if (kind == "monostate-operator") {
		input  = std::monostate {};
		nested = true;
	} else if (kind == "unrelated") {
		input  = sp<enc_fsst_opr> {};
		nested = true;
	} else if (kind == "signed-unffor-width") {
		input  = sp<dec_unffor_opr<u16_pt>> {};
		nested = true;
	} else if (kind == "monostate-column") {
		col = std::monostate {};
	} else if (kind == "string-column") {
		col = std::make_unique<FLSStrColumn>();
	} else if (kind == "typed-string") {
		col    = std::make_unique<TypedCol<str_pt>>();
		nested = true;
	} else if (kind == "nested-list") {
		input = sp<PhysicalExpr> {};
		col   = std::make_unique<List>();
	} else if (kind == "nested-struct") {
		input = sp<PhysicalExpr> {};
		col   = std::make_unique<Struct>();
	} else {
		Check(kind == "signed-constant", "unknown invalid test");
	}
	const auto operator_type =
	    std::visit([](const auto& arg) { return type_name<std::decay_t<decltype(arg)>>(); }, input);
	const auto column_type = std::visit([](const auto& arg) { return type_name<std::decay_t<decltype(arg)>>(); }, col);
	Rowgroup   rowgroup(RowgroupDescriptorT {}, CFG::VEC_SZ);
	rowgroup.internal_rowgroup.push_back(std::move(col));
	const auto expr = nested ? Expression(Expression(input)) : Expression(input);
	try {
		Materialize(expr, rowgroup);
	} catch (const std::runtime_error& error) {
		const std::string message = error.what();
		Check(message.find("UNREACHABLE with unexpected types: " + operator_type + " and " + column_type) !=
		          std::string::npos,
		      "missing operator/column type diagnostic");
		Check(message.find("materializer.cpp:") != std::string::npos, "missing materializer callsite");
		return;
	}
	throw std::runtime_error("invalid materializer pair accepted");
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
		if (argc == 3 && std::string(argv[1]) == "--invalid") {
			TestInvalid(argv[2]);
			return 0;
		}
#define TEST_INTEGER(PT)                                                                                               \
	TestNumeric<PT>();                                                                                                 \
	TestUnffor<PT>();
		TEST_INTEGER(i08_pt)
		TEST_INTEGER(i16_pt)
		TEST_INTEGER(i32_pt)
		TEST_INTEGER(i64_pt)
		TEST_INTEGER(u08_pt)
		TEST_INTEGER(u16_pt)
		TEST_INTEGER(u32_pt)
		TEST_INTEGER(u64_pt)
#undef TEST_INTEGER
		TestNumeric<flt_pt>();
		TestNumeric<dbl_pt>();
		TestString();
#ifdef NDEBUG
		for (const auto* kind : {"signed-constant",
		                         "monostate-operator",
		                         "unrelated",
		                         "signed-unffor-width",
		                         "monostate-column",
		                         "string-column",
		                         "typed-string",
		                         "nested-list",
		                         "nested-struct"}) {
			TestInvalid(kind);
		}
#endif
		std::cout << "Materializer dispatch checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
