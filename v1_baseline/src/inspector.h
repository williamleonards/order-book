#pragma once

#include <order_book.h>

#include <format>
#include <iostream>

namespace order_book {
namespace baseline {

template <class InStream, class OutStream>
class OrderBookInspector {
   public:
	void print_buy_heap(const OrderBook<InStream, OutStream>& ob) {
		std::cout << "Printing the buy heap..." << std::endl;
		for (auto it = ob.buy_heap_.begin(); it != ob.buy_heap_.end(); ++it) {
			std::cout << std::format(
							 "id={}, type={}, price={}, amt={}", it->id,
							 it->type == common::Side::BUY ? "BUY" : "SELL",
							 it->price, it->amt)
					  << std::endl;
		}
	}

	void print_sell_heap(const OrderBook<InStream, OutStream>& ob) {
		std::cout << "Printing the sell heap..." << std::endl;
		for (auto it = ob.sell_heap_.begin(); it != ob.sell_heap_.end(); ++it) {
			std::cout << std::format(
							 "id={}, type={}, price={}, amt={}", it->id,
							 it->type == common::Side::BUY ? "BUY" : "SELL",
							 it->price, it->amt)
					  << std::endl;
		}
	}

	void print_orders(const OrderBook<InStream, OutStream>& ob) {
		std::cout << "Printing buy orders..." << std::endl;
		for (auto it = ob.buy_orders_.begin(); it != ob.buy_orders_.end();
			 ++it) {
			std::cout << std::format(
							 "id={}, type={}, price={}, amt={}", it->second.id,
							 it->second.type == common::Side::BUY ? "BUY"
																  : "SELL",
							 it->second.price, it->second.amt)
					  << std::endl;
		}

		std::cout << "Printing sell orders..." << std::endl;
		for (auto it = ob.sell_orders_.begin(); it != ob.sell_orders_.end();
			 ++it) {
			std::cout << std::format(
							 "id={}, type={}, price={}, amt={}", it->second.id,
							 it->second.type == common::Side::BUY ? "BUY"
																  : "SELL",
							 it->second.price, it->second.amt)
					  << std::endl;
		}
	}
};

}  // namespace baseline
}  // namespace order_book