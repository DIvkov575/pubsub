#include<arpa/inet.h>
#include<netdb.h>
#include<unistd.h>
#include<poll.h>
#include<iostream>
#include<span>
#include<thread>
#include<chrono>
#include<string>

using namespace std::chrono_literals;


#define MYPORT "3490"  
#define BACKLOG 10    

class Client {
  // struct sockaddr_storage their_addr;
  // socklen_t addr_size;
  struct addrinfo hints, *res;
  int sockfd;



  public:
  Client() {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; // tcp
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, MYPORT, &hints, &res);
  }

  int connect() {
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    return ::connect(sockfd, res->ai_addr, res->ai_addrlen);
  }

  int sendb(std::span<const std::byte> data) {
    return send(sockfd, data.data(), data.size(), 0);
  }
  void sendb(std::span<const std::byte> data, int n) {
    for (int i = 0; i < n; ++i)  {
      sendb(data);
      std::this_thread::sleep_for(1s);
    }
  }



  ~Client() {
    freeaddrinfo(res);
    close(sockfd);

  }


};


int main(void) {
  std::vector<std::jthread> threads;

  for (int i = 0; i < 5; ++i) {
    threads.emplace_back(
      [i](){
        Client client;
        client.connect();
        std::string s = std::format("hello from thread {}", i);
        client.sendb(std::as_bytes(std::span{s}), 5);
      });
  }



}
