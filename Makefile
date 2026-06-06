CXX      := g++
CXXFLAGS := -std=c++26 -O0 -Wall -g

TESTS := arena pool pmr hugepage spsc mpmc

.PHONY: clean test FORCE

test: $(TESTS:%=test-%)

clean:
	rm -rf *.dSYM *.out

test-%: FORCE
	$(CXX) $(CXXFLAGS) -o $@.out ./test/$*_test.cpp && ./$@.out

vtest-%: FORCE
	$(CXX) $(CXXFLAGS) -DVERBOSE_TEST=1 -o $@.out ./test/$*_test.cpp && ./$@.out

FORCE:



