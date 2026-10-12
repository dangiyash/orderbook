CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude

.PHONY: all test bench sanitize clean

all: test bench

build/test_orderbook: tests/test_orderbook.cpp include/orderbook.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

build/bench: bench/bench.cpp include/orderbook.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -DNDEBUG $< -o $@

test: build/test_orderbook
	./build/test_orderbook

bench: build/bench
	./build/bench

# Run the tests under AddressSanitizer and UBSan.
sanitize:
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -Iinclude tests/test_orderbook.cpp -o build/test_asan
	./build/test_asan

clean:
	rm -rf build
