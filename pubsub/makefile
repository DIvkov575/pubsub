CXX      := g++
CXXFLAGS := -std=c++26 -fexperimental-library -O0 -Wall -g



.PHONY: main client both

# *.o:
# 	$(CXX) $(CXXFLAGS) @

both: main client


clean:
	rm -rf *.dSYM *.out

main: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o main.out && ./main.out
client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client.out && ./client.out



