#pragma once

#include <x86intrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <thread>

// Prevents the compiler from reordering instructions across this macro
#define COMPILER_BARRIER() asm volatile("" ::: "memory")

namespace order_book {
namespace common {
namespace bench {

/*
 * Time each individual request and analyze their distribution.
 * Use rdtsc() and rdtscp() to measure timestamps in cycles, whose baseline
 * frequency is pegged and calibrated by the OS at boot time.
 *
 * Additionally, prevent the compiler and CPU from reordering instructions
 * when reading timestamps.
 */
template <template <typename, typename> class OrderBook, class InStream,
		  class OutStream>
class PercentileBenchmark {
   public:
	PercentileBenchmark(OrderBook<InStream, OutStream>& ob,
						std::uint64_t num_reqs)
		: ob_(ob), n_(num_reqs) {
		lat_cycles_.reserve(n_);
	}

	void run() {
		// Calibrate invariant_tsc frequency
		double cycles_per_ns = calibrate_tsc_freq();

		// Execute the order book requests
		for (std::uint64_t i = 0; i < n_; ++i) {
			std::uint64_t start = capture_start();

			while (!ob_.try_process()) {
			}

			std::uint64_t end = capture_end();

			lat_cycles_.push_back(end - start);
		}

		std::sort(lat_cycles_.begin(), lat_cycles_.end());

		// Report tail latencies
		std::cout << "Latency distributions:" << std::endl;
		std::cout << std::format(
			"P50 = {} ns\n",
			static_cast<double>(get_percentile(50)) / cycles_per_ns);
		std::cout << std::format(
			"P90 = {} ns\n",
			static_cast<double>(get_percentile(90)) / cycles_per_ns);
		std::cout << std::format(
			"P99 = {} ns\n",
			static_cast<double>(get_percentile(99)) / cycles_per_ns);
		std::cout << std::format(
			"P99.9 = {} ns\n",
			static_cast<double>(get_percentile(99.9)) / cycles_per_ns);
		std::cout << std::format(
			"P99.99 = {} ns\n",
			static_cast<double>(get_percentile(99.99)) / cycles_per_ns);
		std::cout << std::format(
			"MAX = {} ns\n",
			static_cast<double>(get_percentile(100)) / cycles_per_ns);
		std::cout << std::format(
						 "Calibrated tsc frequency was {} cycles per ns",
						 cycles_per_ns)
				  << std::endl;
	}

   private:
	inline std::uint64_t capture_start() {
		COMPILER_BARRIER();

		// CPU barrier: wait for all previous instructions to retire
		_mm_lfence();
		std::uint64_t cycles = __rdtsc();

		COMPILER_BARRIER();
		return cycles;
	}

	inline std::uint64_t capture_end() {
		COMPILER_BARRIER();

		// rdtscp waits for all previous instructions to retire
		// intrinsic requires us to capture the CPU core ID
		unsigned int aux;
		std::uint64_t cycles = __rdtscp(&aux);

		// CPU barrier: prevent subsequent instructions from starting before it
		_mm_lfence();

		COMPILER_BARRIER();
		return cycles;
	}

	// Return the number of cycles per nanosecond
	double calibrate_tsc_freq() {
		// Warm up the CPU and thread to avoid initial wake-up lag
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		// Capture baseline wall-clock and cycles
		auto time_start = std::chrono::steady_clock::now();
		uint64_t cycles_start = capture_start();

		// Sleep for a known, substantial duration
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		// Capture ending wall-clock and cycles
		uint64_t cycles_end = capture_end();
		auto time_end = std::chrono::steady_clock::now();

		// Calculate the ratio
		auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
							  time_end - time_start)
							  .count();
		uint64_t elapsed_cycles = cycles_end - cycles_start;

		return static_cast<double>(elapsed_cycles) /
			   static_cast<double>(elapsed_ns);
	}

	std::uint64_t get_percentile(double percentile) {
		int pos = static_cast<int>(std::ceil(
					  (static_cast<double>(n_) * percentile) / 100.0)) -
				  1;
		if (pos < 0) pos = 0;
		return lat_cycles_[static_cast<std::size_t>(pos)];
	}

   private:
	OrderBook<InStream, OutStream>& ob_;
	std::uint64_t n_;
	std::vector<std::uint64_t> lat_cycles_;
};

}  // namespace bench
}  // namespace common
}  // namespace order_book