////////////////
/* Test suite */
////////////////

#pragma once

#include <bench/average.h>
#include <order_book.h>
#include <stream/vector_stream.h>

namespace order_book {
namespace baseline {
namespace test {

static void test_all() {
	using namespace common;
	// Setup order book
	stream::VectorInStream vec_in(std::vector<Request>{
		{.order = {{RequestType::ORDER, sizeof(Order), 0}, Side::BUY, 7, 10}},
		{.order = {{RequestType::ORDER, sizeof(Order), 1}, Side::BUY, 8, 5}},
		{.order = {{RequestType::ORDER, sizeof(Order), 2}, Side::BUY, 9, 1}},
		{.order = {{RequestType::ORDER, sizeof(Order), 3}, Side::BUY, 9, 1}},
		{.order = {{RequestType::ORDER, sizeof(Order), 4}, Side::BUY, 5, 3}},
		{.order = {{RequestType::ORDER, sizeof(Order), 5}, Side::SELL, 6, 20}},
		{.partial_cancel = {{RequestType::PARTIAL_CANCEL, sizeof(PartialCancel),
							 5},
							Side::SELL,
							1}},
		{.order = {{RequestType::ORDER, sizeof(Order), 6}, Side::SELL, 5, 2}},
		{.full_cancel = {{RequestType::FULL_CANCEL, sizeof(FullCancel), 5},
						 Side::SELL}},
	});
	stream::VectorOutStream vec_out(25);
	OrderBook<stream::VectorInStream, stream::VectorOutStream> ob(vec_in,
																  vec_out);

	// Run benchmark
	bench::AverageBenchmark<OrderBook, stream::VectorInStream,
							stream::VectorOutStream>
		bench(ob, vec_in.num_reqs());
	bench.run();

	// Inspect the responses
	vec_out.print();
}

}  // namespace test
}  // namespace baseline
}  // namespace order_book