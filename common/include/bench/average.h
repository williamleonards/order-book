#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>

namespace order_book {
namespace common {
namespace bench {

// time all requests together
template <template <typename, typename> class OrderBook, class InStream,
		  class OutStream>
class AverageBenchmark {
   public:
	AverageBenchmark(OrderBook<InStream, OutStream>& ob, std::uint64_t num_reqs)
		: ob_(ob), n(num_reqs) {}

	void run() {
		auto start = std::chrono::steady_clock::now();
		for (std::uint64_t i = 0; i < n; ++i) {
			while (!ob_.try_process()) {
			}
		}
		auto end = std::chrono::steady_clock::now();

		auto elapsed_ns =
			std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
		std::cout
			<< std::format(
				   "Test took {} ns across {} queries for an average of {} ns",
				   elapsed_ns.count(), n, elapsed_ns.count() / n)
			<< std::endl;
	}

   private:
	OrderBook<InStream, OutStream>& ob_;
	std::uint64_t n;
};

}  // namespace bench
}  // namespace common
}  // namespace order_book