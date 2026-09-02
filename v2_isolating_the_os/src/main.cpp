/*
 * usage: ./v2_isolating_the_os [INPUT_MODE] [OUTPUT_MODE] [BENCH_TYPE]
 * DATA_FILE INPUT_MODE: how the inputs are served choice of {vector, queue
 * (locked ring-buffer), TBD} OUTPUT_MODE: how the order book emits the outputs
 * 			    choice of {vector, queue, TBD}
 * BENCH_TYPE: how to benchmark this run
 * 			   choice of {average, percentile, TBD}
 * DATA_FILE: file specifying the inputs
 */

#include <bench/average.h>
#include <bench/percentile.h>
#include <malloc.h>
#include <order_book.h>
#include <stream/vector_stream.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
	// Lock memory pages into RAM
	mlockall(MCL_CURRENT | MCL_FUTURE);
	// Prevent a large enough free block on top of the heap to be released back
	// via sbrk()
	mallopt(M_TRIM_THRESHOLD, -1);
	// Prevent large allocation requests to be routed to mmap()
	mallopt(M_MMAP_MAX, 0);

	// Instantly convert the raw pointers into a safe modern vector
	std::vector<std::string> args(argv, argv + argc);

	if (argc < 2) {
		std::cerr << "No input file specified." << std::endl;
		return 1;
	}

	// TODO: parametrize the input/output stream and benchmark class
	if (argc > 2) {
		std::cerr << "Not yet implemented." << std::endl;
		return 1;
	}

	// Default to vector inputs/outputs and average benchmark
	using namespace order_book::common;
	// Setup order book
	stream::VectorInStream vec_in(argv[1]);

	// The number of responses is bounded by twice the number of requests
	// in the current schema
	stream::VectorOutStream vec_out(2 * vec_in.num_reqs());
	order_book::baseline::OrderBook<stream::VectorInStream,
									stream::VectorOutStream>
		ob(vec_in, vec_out);

	bench::PercentileBenchmark<order_book::baseline::OrderBook,
							   stream::VectorInStream, stream::VectorOutStream>
		bench(ob, vec_in.num_reqs());

	ob.warmup(10000);

	std::cout << "Setup and warmup complete. PID: " << getpid() << "\n";
	std::cout << "Press Enter to begin 1 million hot path requests...";
	std::cin.get();	 // Block until perf is attached

	// Run benchmark
	bench.run();

	// Inspect the responses
	// vec_out.print();
	return 0;
}