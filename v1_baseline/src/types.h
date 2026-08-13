////////////////////
/* Internal types */
////////////////////

#pragma once

#include <domain.h>

#include <cstdint>

namespace order_book {
namespace baseline {

// A resting order stored in the book, awaiting a match.
struct BookEntry {
	std::uint64_t id;
	common::Side type;
	mutable std::uint64_t amt;	// does not affect ordering
	std::uint64_t price;
};

// Price descending, orderID ascending
struct MaxPrice {
	bool operator()(BookEntry o1, BookEntry o2) const {
		if (o1.price == o2.price) return o1.id < o2.id;
		return o1.price > o2.price;
	}
};

// Price ascending, orderID ascending
struct MinPrice {
	bool operator()(BookEntry o1, BookEntry o2) const {
		if (o1.price == o2.price) return o1.id < o2.id;
		return o1.price < o2.price;
	}
};

}  // namespace baseline
}  // namespace order_book
