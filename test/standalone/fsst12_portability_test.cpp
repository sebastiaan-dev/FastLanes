// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// test/standalone/fsst12_portability_test.cpp
// ────────────────────────────────────────────────────────
#include "fls/cor/prm/fsst12/libfsst12.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void TestCounterWordWidth() {
	// This object is too large for a Windows thread stack.
	auto counters = std::make_unique<Counters12>();
	for (uint32_t position = 0; position < 8; ++position) {
		std::fill_n(counters->count1High, FSST12_CODE_MAX, 0);
		std::fill_n(counters->count1Low, FSST12_CODE_MAX, 0);
		counters->count1Inc(position);
		uint32_t next = 0;
		Check(counters->count1GetNext(next) == 1 && next == position,
		      "FSST12 must scan all eight bytes of a counter word");
	}
	for (uint32_t start = 0; start < 2; ++start) {
		for (uint32_t position = start; position < 16; ++position) {
			std::fill_n(counters->count2High[0], FSST12_CODE_MAX / 2, 0);
			std::fill_n(counters->count2Low[0], FSST12_CODE_MAX, 0);
			counters->count2Inc(0, position);
			uint32_t next = start;
			Check(counters->count2GetNext(0, next) == 1 && next == position,
			      "FSST12 must scan all sixteen nibbles of a counter word");
		}
	}
}

void TestDecoderSymbolWidth() {
	auto decoder    = std::make_unique<fsst12_decoder_t>();
	decoder->len[3] = decoder->len[4095] = 8;
	std::memcpy(&decoder->symbol[3], "ABCDEFGH", 8);
	std::memcpy(&decoder->symbol[4095], "ijklmnop", 8);
	// Three 12-bit codes (3, 4095, 3): exercise both pair and tail paths.
	std::array<unsigned char, 5> encoded {0x03, 0xf0, 0xff, 0x03, 0x00};
	constexpr char               expected[] = "ABCDEFGHijklmnopABCDEFGH";
	for (unsigned long capacity : {0UL, 1UL, 7UL, 8UL, 9UL, 15UL, 16UL, 23UL, 24UL, 32UL}) {
		std::array<unsigned char, 32> output;
		output.fill(0xa5);
		const auto size = fsst12_decompress(decoder.get(), encoded.size(), encoded.data(), capacity, output.data());
		Check(size == 24, "FSST12 returned an incorrect decoded length");
		const auto written = std::min(capacity, 24UL);
		Check(std::equal(output.begin(), output.begin() + written, expected),
		      "FSST12 symbols must have an eight-byte stride on LLP64 platforms");
		Check(std::all_of(output.begin() + written, output.end(), [](unsigned char byte) { return byte == 0xa5; }),
		      "FSST12 wrote past the output capacity");
	}
}
} // namespace

int main() {
	try {
		TestCounterWordWidth();
		TestDecoderSymbolWidth();
		std::cout << "FSST12 counter and symbol width regressions passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
