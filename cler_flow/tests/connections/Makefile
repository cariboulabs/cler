# Makefile for connection routing tests
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2

# List of test programs
TESTS = test_fillets \
        test_all_elbows \
        test_horizontal_routing \
        test_forward_polyline \
        test_elbows \
        test_elbows_with_nodes \
        test_vertical_fillet

# Build all tests
all: $(TESTS)

# Pattern rule for building tests
%: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Run all tests
test: $(TESTS)
	@echo "=== Running all connection routing tests ===" 
	@for test in $(TESTS); do \
		echo "\n--- Running $$test ---"; \
		./$$test; \
	done

# Run individual test
run-%: %
	./$<

# Clean up executables
clean:
	rm -f $(TESTS)

.PHONY: all test clean