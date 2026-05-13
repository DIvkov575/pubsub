CXX      := g++
CXXFLAGS := -std=c++23 -O0 -Wall -g

TESTS := arena pool pmr hugepage spsc mpmc

.PHONY: clean test FORCE

test: $(TESTS:%=test-%)

clean:
	rm -rf *.dSYM *.out

test-%: FORCE
	$(CXX) $(CXXFLAGS) -o $@.out ./test/$*_test.cpp && ./$@.out

FORCE:
