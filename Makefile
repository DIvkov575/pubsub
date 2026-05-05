
clean:
	rm *.dSYM *.out


test-arena: ./allocators/arena.hpp ./test/arena_test.cpp
	g++ -std=c++23 -g -o0 -o test-arena ./test/arena_test.cpp && ./test-arena


test-pool: ./allocators/pool.hpp ./test/pool_test.cpp
	g++ -std=c++23 -g -o0 -o test-pool ./test/pool_test.cpp && ./test-pool
