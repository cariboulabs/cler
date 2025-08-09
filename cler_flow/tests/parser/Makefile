CXX = g++
CXXFLAGS = -std=c++17 -I/usr/lib/llvm-18/include -I../.. -g
LDFLAGS = -L/usr/lib/llvm-18/lib -lclang

test_block_parser: test_block_parser.cpp ../../src/block_parser.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test_library: test_library.cpp ../../src/block_library.cpp ../../src/block_spec.cpp ../../src/block_parser.cpp
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test: test_block_parser
	./test_block_parser

test_single: test_block_parser
	./test_block_parser /home/alon/repos/cler/desktop_blocks/math/add.hpp

clean:
	rm -f test_block_parser

.PHONY: test test_single clean