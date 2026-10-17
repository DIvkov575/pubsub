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

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) { return &(((struct sockaddr_in*)sa)->sin_addr); }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



void *poll_socket(void *arg) {
  int fd = (int)(intptr_t)arg;
  while (1) {
      char buf[50];
      ssize_t n = recv(fd, buf, sizeof buf, 0);
      if (n < 0) {
        printf("error lowk\n");
        return nullptr;
      } else if (n == 0) {
        printf("connection closed\n");
        return nullptr;
      } else {
        printf("%.*s\n", (int)n, buf);
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




    char s[INET6_ADDRSTRLEN];
    while(1) {  // main accept() loop
        socklen_t sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
            &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);


        pthread_t thread_id;
        pthread_create(&thread_id, nullptr, poll_socket, (void *)(intptr_t)new_fd);

        // char buf[50];
        // if (recv(new_fd, buf, sizeof buf, 0) == -1) {
        //   printf("error lowk\n");
        // } else {
        //   printf("%s\n", buf);
        // }
        // close(new_fd);  // parent doesn't need this
    }


}
