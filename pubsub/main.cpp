#include<arpa/inet.h>    // inet_pton, inet_ntop, htons, ntohs
#include<netdb.h>        // getaddrinfo, addrinfo
#include<unistd.h>       // close, read, write
#include<poll.h>         // poll
#include<iostream>
#include <netinet/in.h>

#include "mpmc.hpp"

// v1
// - any consumer
// - network only
// - one topic

// v2
// multiple topics
// broadcast/any modes
// network/socket

// theo
// network consumer - a thread/unit - calls increment on pubsub to process next elem
// needs dispatch threads that write into network socket

// listen to incoming connections
// create sockets accordingly

// poll all sockets
// write to queue single thread spmc


#define MYPORT "3490"  
#define BACKLOG 10    
#define MAXMESSAGESIZE 128

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) { return &(((struct sockaddr_in*)sa)->sin_addr); }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}




class Queue {
  public:
    virtual publish(std::span<const std::byte> msg);
    virtual consume();
}

class Pubsub {
  Queue q;
  

  public:
  Pubsub() {

  }

  void process_socket(int fd) {
    char buf[128];
    if (recv(fd, buf, sizeof buf, 0) == -1) {
      printf("error with processing socket %d\n", fd);
    } else {
      printf("%s\n", buf);


      q.publish(std::as_bytes(buf));
    }

  }

}



int main(void) {
    struct sockaddr_storage their_addr{};
    socklen_t addr_size;
    struct addrinfo hints, *res;
    int sockfd, new_fd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // fill in my own ip
    getaddrinfo(NULL, MYPORT, &hints, &res);


    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bind(sockfd, res->ai_addr, res->ai_addrlen);
    listen(sockfd, BACKLOG);



    std::vector<pollfd> fds;
    fds.push_back({sockfd, POLLIN});


    while (1) {
      int ready = poll(fds.data(), fds.size(), 1000);

      for (size_t i = 1; i < fds.size() && read > 0; ++i) {
        if (fds[i].revents == 0) continue;
        --ready;

        void process_socket(fds[i].fd);

      }


      if (ready[0] == POLLIN) {
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("error in accepting new connection");
            continue;
        }
        fds.push_back({new_fd, POLLIN})
      }

    }



    // char s[INET6_ADDRSTRLEN];
    // while(1) {  // main accept() loop
    //     socklen_t sin_size = sizeof their_addr;
    //     new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
    //         &sin_size);
    //     if (new_fd == -1) {
    //         perror("accept");
    //         continue;
    //     }
    //
    //     inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    //     printf("server: got connection from %s\n", s);
    //
    //
    //     pthread_t thread_id;
    //     pthread_create(&thread_id, nullptr, poll_socket, (void *)(intptr_t)new_fd);
    //
    //     // char buf[50];
    //     // if (recv(new_fd, buf, sizeof buf, 0) == -1) {
    //     //   printf("error lowk\n");
    //     // } else {
    //     //   printf("%s\n", buf);
    //     // }
    //     // close(new_fd);  // parent doesn't need this
    // }


}
