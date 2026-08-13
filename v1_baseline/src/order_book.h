#pragma once

#include <schema.h>
#include <types.h>

#include <set>
#include <unordered_map>

namespace order_book {
namespace baseline {

// The interface (request/response) types dominate this translation unit, so we
// let them own the short, unqualified names. The internal book-entry type is
// named BookEntry precisely so it doesn't collide with common::Order.
using namespace order_book::common;

// Forward declaration so it can be befriended below.
template <class InStream, class OutStream>
class OrderBookInspector;

template <class InStream, class OutStream>
class OrderBook {
   public:
	OrderBook(InStream& in, OutStream& out) : in_(in), out_(out) {}
	// returns whether an incoming request was processed
	bool try_process() {
		RequestHeader* req_header = in_.pop();
		// May return NULL if no more requests are available
		if (!req_header) return false;

		switch (req_header->type) {
			case RequestType::ORDER: {
				Order* order_req = static_cast<Order*>(req_header);
				switch (order_req->side) {
					case Side::BUY:
						buy(*order_req);
						break;
					case Side::SELL:
						sell(*order_req);
						break;
				}
				break;
			}
			case RequestType::FULL_CANCEL: {
				FullCancel* full_cancel_req =
					static_cast<FullCancel*>(req_header);
				full_cancel(*full_cancel_req);
				break;
			}
			case RequestType::PARTIAL_CANCEL: {
				PartialCancel* partial_cancel_req =
					static_cast<PartialCancel*>(req_header);
				partial_cancel(*partial_cancel_req);
				break;
			}
			default: {
				out_.push_back({Error{{ResponseType::ERROR, sizeof(Error), 0},
									  ErrorCode::MALFORMED_REQUEST}});
			}
		}
		return true;
	}

   private:
	void buy(const Order& buy_req) {
		std::uint64_t price = buy_req.price;
		std::uint64_t amt = buy_req.amt;
		std::uint64_t order_id = buy_req.order_id;

		// Track remaining amounts
		std::uint64_t rem_amt = amt;

		// Match while sell heap isn't empty
		auto resting_order_it = sell_heap_.begin();
		while (resting_order_it != sell_heap_.end() && rem_amt > 0 &&
			   resting_order_it->price <= price) {
			BookEntry resting_order = *resting_order_it;
			// Use remaining instead of original amount
			std::uint64_t transact_amt = std::min(resting_order.amt, rem_amt);
			// add new trade to output stream
			out_.push_back(
				{Trade{{ResponseType::TRADE, sizeof(Trade), order_id},
					   resting_order.price,
					   transact_amt}});
			// update amounts
			rem_amt -= transact_amt;
			resting_order.amt -= transact_amt;
			// remove resting_order if depleted
			if (resting_order.amt == 0) {
				// Erase from hashmap before the resting_order is deleted
				sell_orders_.erase(resting_order_it->id);
				sell_heap_.erase(resting_order_it);
			}
			// update iterator
			resting_order_it = sell_heap_.begin();
		}

		// Construct the remaining order in the hash map and insert to the
		// buy heap, if any
		if (rem_amt > 0) {
			// Assume order_id is unique
			auto [it, _] =
				buy_heap_.emplace(order_id, Side::BUY, rem_amt, price);
			// std::set is iterator-stable
			buy_orders_.emplace(order_id, it);
			// add the resultant order to the output stream
			out_.push_back({Ack{{ResponseType::ACK, sizeof(Ack), order_id},
								Side::BUY,
								price,
								rem_amt}});
		}
	}

	void sell(const Order& sell_req) {
		std::uint64_t price = sell_req.price;
		std::uint64_t amt = sell_req.amt;
		std::uint64_t order_id = sell_req.order_id;

		// Track remaining amounts
		std::uint64_t rem_amt = amt;

		// Match while buy heap isn't empty
		auto resting_order_it = buy_heap_.begin();
		while (resting_order_it != buy_heap_.end() && rem_amt > 0 &&
			   resting_order_it->price >= price) {
			BookEntry resting_order = *resting_order_it;
			// Use remaining instead of original amount
			std::uint64_t transact_amt = std::min(resting_order.amt, rem_amt);
			// add new trade to output stream
			out_.push_back(
				{Trade{{ResponseType::TRADE, sizeof(Trade), order_id},
					   resting_order.price,
					   transact_amt}});
			// update amounts
			rem_amt -= transact_amt;
			resting_order.amt -= transact_amt;
			// remove resting_order if depleted
			if (resting_order.amt == 0) {
				// Erase from hashmap before the resting_order is deleted
				buy_orders_.erase(resting_order_it->id);
				buy_heap_.erase(resting_order_it);
			}
			// update iterator
			resting_order_it = buy_heap_.begin();
		}

		// Put the remainder into the sell heap and orders map, if any
		if (rem_amt > 0) {
			// Assume order_id is unique
			auto [it, _] =
				sell_heap_.emplace(order_id, Side::SELL, rem_amt, price);
			// std::set is iterator-stable
			sell_orders_.emplace(order_id, it);
			// add resultant order into the output stream
			out_.push_back({Ack{{ResponseType::ACK, sizeof(Ack), order_id},
								Side::SELL,
								price,
								rem_amt}});
		}
	}

	void partial_cancel(const PartialCancel& partial_cancel_req) {
		Side side = partial_cancel_req.side;
		std::uint64_t id = partial_cancel_req.order_id;
		std::uint64_t amt = partial_cancel_req.amt;

		if (side == Side::BUY) {
			auto buy_orders_it = buy_orders_.find(id);
			if (buy_orders_it == buy_orders_.end()) {
				out_.push_back({Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::ID_NOT_FOUND}});
				return;
			}

			if (amt == 0) {
				out_.push_back({Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::INVALID_AMT}});
				return;
			}

			// Mutate the amount through the iterator
			auto order_it = buy_orders_it->second;
			order_it->amt = std::max(order_it->amt - amt, (std::uint64_t)0);

			// Erase order if depleted
			if (order_it->amt == 0) {
				buy_orders_.erase(buy_orders_it);
				buy_heap_.erase(order_it);
				return;
			}
		} else {
			auto sell_orders_it = sell_orders_.find(id);
			if (sell_orders_it == sell_orders_.end()) {
				out_.push_back({Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::ID_NOT_FOUND}});
				return;
			}

			if (amt == 0) {
				out_.push_back({Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::INVALID_AMT}});
				return;
			}

			// Mutate the amount through the iterator
			auto order_it = sell_orders_it->second;
			order_it->amt = std::max(order_it->amt - amt, (std::uint64_t)0);

			// Erase order if depleted
			if (order_it->amt == 0) {
				sell_orders_.erase(sell_orders_it);
				sell_heap_.erase(order_it);
				return;
			}
		}

		out_.push_back(
			Cancellation{{ResponseType::CANCELLATION, sizeof(Cancellation), id},
						 CancelStatus::SUCCESS});
	}

	void full_cancel(const FullCancel& full_cancel_req) {
		Side side = full_cancel_req.side;
		std::uint64_t id = full_cancel_req.order_id;

		if (side == Side::BUY) {
			auto buy_orders_it = buy_orders_.find(id);
			if (buy_orders_it == buy_orders_.end()) {
				out_.push_back(Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::ID_NOT_FOUND});
				return;
			}
			buy_heap_.erase(buy_orders_it->second);
			buy_orders_.erase(buy_orders_it);
		} else {
			auto sell_orders_it = sell_orders_.find(id);
			if (sell_orders_it == sell_orders_.end()) {
				out_.push_back(Cancellation{
					{ResponseType::CANCELLATION, sizeof(Cancellation), id},
					CancelStatus::ID_NOT_FOUND});
				return;
			}
			sell_heap_.erase(sell_orders_it->second);
			sell_orders_.erase(sell_orders_it);
		}

		// emit response
		out_.push_back(
			Cancellation{{ResponseType::CANCELLATION, sizeof(Cancellation), id},
						 CancelStatus::SUCCESS});
	}

   private:
	InStream& in_;
	OutStream& out_;
	// Buy "heap"
	std::set<BookEntry, MaxPrice> buy_heap_;
	// Sell "heap"
	std::set<BookEntry, MinPrice> sell_heap_;
	// Store the iterators by order_id
	std::unordered_map<std::uint64_t, std::set<BookEntry, MaxPrice>::iterator>
		buy_orders_;
	std::unordered_map<std::uint64_t, std::set<BookEntry, MinPrice>::iterator>
		sell_orders_;

	friend class OrderBookInspector<InStream, OutStream>;
};

}  // namespace baseline
}  // namespace order_book
