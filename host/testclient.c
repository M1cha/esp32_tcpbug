/* 
 * tcpclient.c - A simple TCP client
 * usage: tcpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 100

/* 
 * error - wrapper for perror
 */
#define error(msg) ({\
    perror(msg); \
    close(sockfd); \
    continue; })

int main(int argc, char **argv) {
    int sockfd, portno, n;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    bzero(buf, BUFSIZE);

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    for(;;) {
        /* socket: create the socket */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) 
            error("ERROR opening socket");

        /* gethostbyname: get the server's DNS entry */
        server = gethostbyname(hostname);
        if (server == NULL) {
            fprintf(stderr,"ERROR, no such host as %s\n", hostname);
            exit(0);
        }

        /* build the server's Internet address */
        bzero(&serveraddr, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        bcopy(server->h_addr, &serveraddr.sin_addr.s_addr, server->h_length);
        serveraddr.sin_port = htons(portno);

        /* connect: create a connection with the server */
        if (connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) 
          error("ERROR connecting");

        /* send the message to the server */
        n = write(sockfd, buf, sizeof(buf));
        if (n <= 0) 
          error("can't write");

        /* read back answer from the server */
        n = read(sockfd, buf, sizeof(buf));
        if (n <= 0) 
          error("can't read");

        /* shutdown write side */
        n = shutdown(sockfd, SHUT_WR);
        if (n < 0)
          error("can't shutdown-WR");

        /* wait for server to close it's write side */
        n = read(sockfd, buf, sizeof(buf));
        if (n != 0) 
          error("unexpected read after shutdown-WR");

        /* shutdown everything */
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }
    return 0;
}
