#include "../queues/spsc.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>

int main() {
  std::cout << alignof(size_t) << sizeof(size_t) << "\n";

  SPSCQueue<size_t, 32> queue;

  // tmpty
  assert(queue.empty());

  // push/pop
  for (size_t i = 0; i < 32; ++i) {
    assert(queue.try_push(i));
  }
  assert(!queue.try_push(33));

  for (size_t i = 0; i < 32; ++i) {
    size_t j;
    assert(queue.try_pop(j));
    assert(i == j);
  }
  size_t j;
  assert(!queue.try_pop(j));


  // todo - test complex shi w overwrite

  

  
} 
