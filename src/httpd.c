/* A UDP echo server with timeouts.
 *
 * Note that you will not need to use select and the timeout for a
 * tftp server. However, select is also useful if you want to receive
 * from multiple sockets at the same time. Read the documentation for
 * select on how to do this (Hint: Iterate with FD_ISSET()).
 */

#include <assert.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>

#define REQUEST_METHOD_LENGTH 6

void getRequestMethod(char message[], char requestMethod[]) {
    memset(requestMethod, 0, REQUEST_METHOD_LENGTH);

    int i = 0;
    while(message[i] != ' ') {
        strcat(requestMethod, (char*)message[i]);
        i++;
    }
    
    fprintf(stdout, "%s", requestMethod);
    fflush(stdout);
}

int main(int argc, char **argv) {
    FILE *fp;

    fprintf(stdout, "%d \n", argc);
    fflush(stdout);

    int sockfd;
    struct sockaddr_in server, client;
    char message[512];

    /* Create and bind a UDP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    /* Network functions need arguments in network byte order instead of
       host byte order. The macros htonl, htons convert the values, */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(atoi(argv[1]));
    bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

    /* Before we can accept messages, we have to listen to the port. We allow one
     * 1 connection to queue for simplicity.
     */
    listen(sockfd, 1);


    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int retval;

        /* Check whether there is data on the socket fd. */
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        /* Wait for five seconds. */
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        retval = select(sockfd + 1, &rfds, NULL, NULL, &tv);

        if (retval == -1) {
            perror("select()");
        } else if (retval > 0) {
            fp = fopen("src/httpd.log", "a+");
            
            /* Data is available, receive it. */
            assert(FD_ISSET(sockfd, &rfds));

            /* Copy to len, since recvfrom may change it. */
            socklen_t len = (socklen_t) sizeof(client);

            /* For TCP connectios, we first have to accept. */
            int connfd;
            connfd = accept(sockfd, (struct sockaddr *) &client,
                    &len);

            /* Receive one byte less than declared,
               because it will be zero-termianted
               below. */
            ssize_t n = read(connfd, message, sizeof(message) - 1);

            /* Send the message back. */
            write(connfd, message, (size_t) n);

            /* We should close the connection. */
            shutdown(connfd, SHUT_RDWR);
            close(connfd);

            /* Zero terminate the message, otherwise
               printf may access memory outside of the
               string. */
            message[n] = '\0';
            /* Print the message to stdout and flush. */
            fprintf(stdout, "Received:\n%s\n", message);
            fflush(stdout);

            /* TEST */
            char requestMethod[REQUEST_METHOD_LENGTH];
            time_t now;
            time(&now);
            char buf[sizeof "2011-10-08T07:07:09Z"];
            strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));
            
            fprintf(fp, "PORT: %d \n", client.sin_port);
            fprintf(fp, "IP: %s \n", inet_ntoa(client.sin_addr));
            fprintf(fp, "TIME %s \n", buf);
            fprintf(fp, "---------------------------\n");
            fflush(fp);
            getRequestMethod(message, requestMethod);

            //Close log file
            fclose(fp);
            /* END OF TEST */
        } else {
            fprintf(stdout, "No message in five seconds.\n");
            fflush(stdout);
        }
    }
}
