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
	// Take inputs from a file
	VectorInStream(const std::string& fname) {
		std::ifstream input_file(fname, std::ios::in | std::ios::binary);
		for (std::array<char, sizeof(Request)> buf;
			 input_file.getline(&buf[0], sizeof(Request));) {
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

   private:
	std::vector<Request> data_;
	std::size_t idx_;
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