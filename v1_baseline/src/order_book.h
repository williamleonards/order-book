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
				out_.push_back(
					{Error{{ResponseType::ERROR, sizeof(Error), 0},
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
		auto order_it = sell_heap_.begin();
		while (order_it != sell_heap_.end() && rem_amt > 0 &&
			   (*order_it)->price <= price) {
			BookEntry* order = *order_it;
			std::uint64_t transact_amt = std::min(order->amt, amt);
			// add new trade to output stream
			out_.push_back(
				{Trade{{ResponseType::TRADE, sizeof(Trade), order_id},
					   order->price,
					   transact_amt}});
			// update amounts
			rem_amt -= transact_amt;
			order->amt -= transact_amt;
			// remove order if depleted
			if (order->amt == 0) {
				sell_heap_.erase(order_it);
				orders_.erase((*order_it)->id);
			}
			// update iterator
			order_it = sell_heap_.begin();
		}

		// Construct the remaining order in the hash map and insert to the
		// buy heap, if any
		if (rem_amt > 0) {
			orders_.emplace(std::make_pair(
				order_id, BookEntry(order_id, Side::BUY, rem_amt, price)));
			buy_heap_.insert(&orders_[order_id]);
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
		auto order_it = buy_heap_.begin();
		while (order_it != buy_heap_.end() && rem_amt > 0 &&
			   (*order_it)->price >= price) {
			BookEntry* order = *order_it;
			std::uint64_t transact_amt = std::min(order->amt, amt);
			// add new trade to output stream
			out_.push_back(
				{Trade{{ResponseType::TRADE, sizeof(Trade), order_id},
					   order->price,
					   transact_amt}});
			// update amounts
			rem_amt -= transact_amt;
			order->amt -= transact_amt;
			// remove order if depleted
			if (order->amt == 0) {
				buy_heap_.erase(order_it);
				orders_.erase((*order_it)->id);
			}
			// update iterator
			order_it = buy_heap_.begin();
		}

		// Put the remainder into the sell heap and orders map, if any
		if (rem_amt > 0) {
			orders_.emplace(std::make_pair(
				order_id, BookEntry(order_id, Side::SELL, rem_amt, price)));
			sell_heap_.insert(&orders_[order_id]);
			// add resultant order into the output stream
			out_.push_back({Ack{{ResponseType::ACK, sizeof(Ack), order_id},
								Side::SELL,
								price,
								rem_amt}});
		}
	}

	void partial_cancel(const PartialCancel& partial_cancel_req) {
		std::uint64_t id = partial_cancel_req.order_id;
		std::uint64_t amt = partial_cancel_req.amt;

		auto order_it = orders_.find(id);
		if (order_it == orders_.end()) {
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

		order_it->second.amt =
			std::max(order_it->second.amt - amt, (std::uint64_t)0);
		if (order_it->second.amt == 0) {
			erase_order(order_it);
			return;
		}
		out_.push_back(Cancellation{
			{ResponseType::CANCELLATION, sizeof(Cancellation), id},
			CancelStatus::SUCCESS});
		return;
	}

	void full_cancel(const FullCancel& full_cancel_req) {
		std::uint64_t id = full_cancel_req.order_id;

		auto order_it = orders_.find(id);
		if (order_it == orders_.end()) {
			out_.push_back(Cancellation{
				{ResponseType::CANCELLATION, sizeof(Cancellation), id},
				CancelStatus::ID_NOT_FOUND});
			return;
		}
		erase_order(order_it);

		// emit response
		out_.push_back(Cancellation{
			{ResponseType::CANCELLATION, sizeof(Cancellation), id},
			CancelStatus::SUCCESS});
	}

	void erase_order(
		std::unordered_map<std::uint64_t, BookEntry>::iterator order_it) {
		// check order type, then delete from heap
		if (order_it->second.type == Side::BUY) {
			buy_heap_.erase(&(order_it->second));
		} else {
			sell_heap_.erase(&(order_it->second));
		}

		// delete from map
		orders_.erase(order_it);
	}

   private:
	InStream& in_;
	OutStream& out_;
	// Buy "heap"
	std::set<BookEntry*, MaxPrice> buy_heap_;
	// Sell "heap"
	std::set<BookEntry*, MinPrice> sell_heap_;
	// Order ID --> order instance
	// The map owns the orders; the "heaps" just points to it
	std::unordered_map<std::uint64_t, BookEntry> orders_;

	friend class OrderBookInspector<InStream, OutStream>;
};

}  // namespace baseline
}  // namespace order_book
