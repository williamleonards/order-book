//////////////////////////////////
/* Request & Response Interface */
//////////////////////////////////

#pragma once

#include <domain.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace order_book {
namespace common {

enum struct RequestType {
	ORDER,
	FULL_CANCEL,
	PARTIAL_CANCEL,
};

// The order ID has been issued by the exchange and is guaranteed to be unique
struct RequestHeader {
	RequestType type;
	std::uint64_t len;
	std::uint64_t order_id;
};

struct Order : RequestHeader {
	Side side;
	std::uint64_t price;
	std::uint64_t amt;
};

struct FullCancel : RequestHeader {
	Side side;
};

// Cancel an amt of a pending order
// If the remainder is less than the argument, the order is depleted
struct PartialCancel : RequestHeader {
	Side side;
	std::uint64_t amt;
};

/*
 * All members inherit from RequestHeader
 * Add RequestHeader itself as a member for Union-based type-punning
 * Which is illegal in ISO C++ but is supported in GCC/Clang:
 * https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#Type-punning
 */
union Request {
	RequestHeader header;
	Order order;
	FullCancel full_cancel;
	PartialCancel partial_cancel;

	std::string str() {
		RequestType type;
		// destination before source
		std::memcpy(&type, this, sizeof(RequestType));

		switch (type) {
			case RequestType::ORDER:
				return std::format(
					"Order {{ order_id={}, side={}, price={}, amt={} }}",
					order.order_id, order.side == Side::BUY ? "BUY" : "SELL",
					order.price, order.amt);
			case RequestType::PARTIAL_CANCEL:
				return std::format("PartialCancel {{ order_id={}, amt={} }}",
								   partial_cancel.order_id, partial_cancel.amt);
			case RequestType::FULL_CANCEL:
				return std::format("FullCancel {{ order_id={} }}",
								   full_cancel.order_id);
		}

		return "";
	}
};

enum struct ResponseType {
	TRADE,
	ACK,
	CANCELLATION,
	ERROR,
};

struct ResponseHeader {
	ResponseType type;
	std::uint64_t len;
	std::uint64_t
		order_id;  // for trades, the order_id who actualizes the trade
};

// Don't care who the counterparty is
struct Trade : ResponseHeader {
	std::uint64_t price;
	std::uint64_t amt;

	std::string str() {
		return std::format("Trade {{ order_id={}, price={}, amt={} }}",
						   order_id, price, amt);
	}
};

struct Ack : ResponseHeader {
	Side side;
	std::uint64_t price;
	std::uint64_t amt;

	std::string side_str() { return side == Side::BUY ? "BUY" : "SELL"; }

	std::string str() {
		return std::format("Ack {{ order_id={}, side={}, price={}, amt={} }}",
						   order_id, side_str(), price, amt);
	}
};

enum struct CancelStatus {
	SUCCESS = 0,
	INVALID_AMT = 1,
	ID_NOT_FOUND = 2,
};

struct Cancellation : ResponseHeader {
	CancelStatus status;

	std::string status_str() {
		switch (status) {
			case CancelStatus::SUCCESS:
				return "SUCCESS";
			case CancelStatus::INVALID_AMT:
				return "INVALID_AMT";
			case CancelStatus::ID_NOT_FOUND:
				return "ID_NOT_FOUND";
		}
		return "";
	}

	std::string str() {
		return std::format("Cancellation {{ order_id={}, status={} }}",
						   order_id, status_str());
	}
};

enum struct ErrorCode {
	MALFORMED_REQUEST = 1,
};

struct Error : ResponseHeader {
	ErrorCode error_code;

	std::string err_str() {
		switch (error_code) {
			case ErrorCode::MALFORMED_REQUEST:
				return "MALFORMED_REQUEST";
		}
		return "";
	}

	std::string str() {
		return std::format("Error {{ error_code={} }}", err_str());
	}
};

union Response {
	Trade trade;
	Ack ack;
	Cancellation cancellation;
	Error error;

	// Inheritances makes this non-aggregate and disables designated
	// initializers
	Response(const Trade& trade) : trade{trade} {}
	Response(const Ack& ack) : ack{ack} {}
	Response(const Cancellation& cancellation) : cancellation{cancellation} {}
	Response(const Error& error) : error{error} {}

	std::string str() {
		ResponseType type;
		// destination before source
		std::memcpy(&type, this, sizeof(ResponseType));

		switch (type) {
			case ResponseType::TRADE:
				return trade.str();
			case ResponseType::ACK:
				return ack.str();
			case ResponseType::CANCELLATION:
				return cancellation.str();
			case ResponseType::ERROR:
				return error.str();
		}

		return "";
	}
};

}  // namespace common
}  // namespace order_book