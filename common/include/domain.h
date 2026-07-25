//////////////////////////////
/* Shared domain primitives */
//////////////////////////////

// Intrinsic trading-domain vocabulary depended on by BOTH the transport schema
// (schema.h) and the engine internals (baseline/types.h). Neither of those
// layers depends on the other; they meet here. Keep this header
// dependency-light: no transport, formatting, or container includes.

#pragma once

namespace order_book {
namespace common {

enum struct Side {
	BUY,
	SELL,
};

}  // namespace common
}  // namespace order_book
