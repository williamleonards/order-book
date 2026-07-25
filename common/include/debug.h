#pragma once

#include <format>
#include <iostream>

namespace order_book {
namespace common {
namespace debug {

template <template <typename> class Container, typename T>
void print_container(Container<T> t) {
	for (auto it = t.begin(); it != t.end(); ++it) {
		std::cout << it->str() << std::endl;
	}
}

template <template <typename, typename> class Container, typename K, typename V>
void print_assoc_container(Container<K, V> t) {
	for (auto it = t.begin(); it != t.end(); ++it) {
		std::cout << std::format("{} -> {}", it->first.str(), it->second.str())
				  << std::endl;
	}
}

}  // namespace debug
}  // namespace common
}  // namespace order_book