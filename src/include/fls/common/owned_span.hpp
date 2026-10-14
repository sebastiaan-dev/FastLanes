// ────────────────────────────────────────────────────────
// |                      FastLanes                       |
// ────────────────────────────────────────────────────────
// src/include/fls/common/owned_span.hpp
// ────────────────────────────────────────────────────────
#pragma once

#include "fls/std/span.hpp"
#include <memory>

namespace fastlanes {

template <typename T>
struct OwnedSpan {
	OwnedSpan() = default;
	OwnedSpan(span<T> span_p, std::shared_ptr<void> owner_p)
	    : span(span_p)
	    , owner(std::move(owner_p)) {
	}

	span<T>               span;
	std::shared_ptr<void> owner;
};

} // namespace fastlanes
