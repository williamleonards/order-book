#pragma once

#include <debug.h>
#include <schema.h>

#include <array>
#include <cstdint>

namespace order_book {
namespace common {
namespace stream {

class VectorInStream {
   public:
	VectorInStream(std::vector<Request> reqs)
		: data_(std::move(reqs)), idx_(0) {}

	// Take inputs from a file of fixed-size binary Request records (native
	// layout, no delimiters). read() pulls exactly one sizeof(Request) chunk
	// per iteration and fails cleanly at EOF; getline() cannot be used here
	// because Request is a raw binary blob that may contain '\n' bytes and is
	// larger than getline's (n - 1) store limit.
	VectorInStream(const std::string& fname) {
		std::ifstream input_file(fname, std::ios::in | std::ios::binary);
		for (std::array<char, sizeof(Request)> buf;
			 input_file.read(&buf[0], sizeof(Request));) {
			Request req;
			std::memcpy(&req, &buf, sizeof(Request));
			data_.push_back(std::move(req));
		}
	}

	RequestHeader* pop() {
		if (idx_ == data_.size()) return NULL;
		// Retain the context that this memory belongs to a union
		return &(data_[idx_++].header);
	}

	std::size_t num_reqs() { return data_.size(); }

	void print() { debug::print_container(data_); }

   private:
	std::vector<Request> data_;
	// Default member initializer so every constructor starts at 0. The file
	// constructor used to leave this uninitialized -> pop() indexed out of
	// bounds and returned a wild pointer.
	std::size_t idx_ = 0;
};

class VectorOutStream {
   public:
	VectorOutStream(std::uint64_t n) { buf_.reserve(n); }
	bool push_back(const Response& res) {
		if (buf_.size() >= buf_.capacity()) return false;
		buf_.push_back(res);
		return true;
	}

	void print() { debug::print_container(buf_); }

   private:
	std::vector<Response> buf_;
};

}  // namespace stream
}  // namespace common
}  // namespace order_book