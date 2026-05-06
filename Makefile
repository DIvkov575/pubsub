CXX := g++
CXXFLAGS := -std=c++23 -O0 -Wall -g

TESTS := arena pool pmr hugepage

.PHONY: clean $(TESTS:%=test-%)

clean:
	rm -rf *.dSYM *.out

test-%: ./test/%_test.cpp
	$(CXX) $(CXXFLAGS) -o $@.out $< && ./$@.out
