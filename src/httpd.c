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
#include <glib.h>
#include <stdlib.h>

#define REQUEST_METHOD_LENGTH 8
#define REQUEST_URL_LENGTH 100
#define MAX_TOKENS -1
#define MAX_HTML_SIZE 1000
#define PORT_LENGTH 6
#define CONTENT_LENGTH 1000
#define HEAD_LENGTH 1000

/*
 *
 */
void getRequestMethod(char message[], char requestMethod[]) {
    gchar** splitMessage = g_strsplit(message, " ", MAX_TOKENS);
    strcpy(requestMethod, (char*)splitMessage[0]);
    g_strfreev(splitMessage);
}

/*
 *
 */
void getRequestURL(char message[], char requestURL[]) {
    gchar** splitMessage = g_strsplit(message, " ", MAX_TOKENS);
    strcat(requestURL, splitMessage[1]);
    g_strfreev(splitMessage);
}

/*
 *
 */
void getContent(char message[], char content[]) {
    gchar** splitMessage = g_strsplit(message, "\r\n\r\n", MAX_TOKENS); 
    strcat(content, splitMessage[1]);
    g_strfreev(splitMessage);
}

/*
 *
 */
void getHead(char message[], char head[]) {	
    gchar** splitMessage = g_strsplit(message, "\r\n\r\n", MAX_TOKENS); 
    strcat(head, splitMessage[0]);
    g_strfreev(splitMessage);
}

/*
 *
 */
void handleGET(int connfd, char requestURL[], char ip_address[], int port) {
    char body[MAX_HTML_SIZE];
    memset(body, 0, MAX_HTML_SIZE);
    strcpy(body, "<!DOCTYPE>\n<html>\n<head></head>\n<body>\n");
    strcat(body, requestURL);
    strcat(body, "\n");
    strcat(body, ip_address);
    strcat(body, "\n");
    char s_port[PORT_LENGTH];
    memset(s_port, 0, PORT_LENGTH);
    snprintf(s_port, PORT_LENGTH, "%d", port);
    strcat(body, s_port);
    strcat(body, "\n");
    strcat(body, "</body>\n</html>\n");
    ssize_t n =  sizeof(body) ;
    write(connfd, body, (size_t) n);
}

/*
 *
 */
void handlePOST(int connfd, char requestURL[], char ip_address[], int port, char content[]) {
    char body[MAX_HTML_SIZE];
    memset(body, 0, MAX_HTML_SIZE);
    strcpy(body, "<!DOCTYPE>\n<html>\n<head></head>\n<body>\n");
    strcat(body, requestURL);
    strcat(body, "\n");
    strcat(body, ip_address);
    strcat(body, "\n");
    char s_port[PORT_LENGTH];
    memset(s_port, 0, PORT_LENGTH);
    snprintf(s_port, PORT_LENGTH, "%d", port);
    strcat(body, s_port);
    strcat(body, "\n");
    strcat(body, content);
    strcat(body, "\n");
    strcat(body, "</body>\n</html>\n");
    ssize_t n =  sizeof(body) ;
    write(connfd, body, (size_t) n);
}

/*
 *
 */
void handleHEAD(int connfd, char head[]) {
    ssize_t n = HEAD_LENGTH;
    write(connfd, head, (size_t) n);
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
            /* Open file. */
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
            //write(connfd, message, (size_t) n);

            /* Zero terminate the message, otherwise
               printf may access memory outside of the
               string. */
            message[n] = '\0';
            /* Print the message to stdout and flush. */
            //fprintf(stdout, "Received:\n%s\n", message);
            //fflush(stdout);


            char requestMethod[REQUEST_METHOD_LENGTH];
            char requestURL[REQUEST_URL_LENGTH];
            char content[CONTENT_LENGTH];
            char head[HEAD_LENGTH];
            memset(requestMethod, 0, REQUEST_METHOD_LENGTH);
            memset(requestURL, 0, REQUEST_URL_LENGTH);
            memset(content, 0, CONTENT_LENGTH);
            memset(head, 0, HEAD_LENGTH);

            strcpy(requestURL, "http://localhost/");
            strcat(requestURL, argv[1]);
            time_t now;
            time(&now);
            char buf[sizeof "2011-10-08T07:07:09Z"];
            strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));

            getRequestMethod(message, requestMethod);
            getRequestURL(message, requestURL);
            

            /* GET. */ 
            if(strcmp(requestMethod, "GET") == 0) {
                handleGET(connfd, requestURL, inet_ntoa(client.sin_addr), client.sin_port);
            }
            /* POST. */
            else if(strcmp(requestMethod, "POST") == 0) {
                getContent(message, content);
                handlePOST(connfd, requestURL, inet_ntoa(client.sin_addr), client.sin_port, content);
            }
            /* HEAD. */
            else if(strcmp(requestMethod, "HEAD") == 0) {
                fprintf(stdout, "%d", (ssize_t)sizeof(head));
                getHead(message, head);
                handleHEAD(connfd, head);
            }
            /* Error. */
            else {
            }

            /* Write info to screen. */
            fprintf(stdout, "%s : %s:%d %s\n%s : %d\n", buf, inet_ntoa(client.sin_addr), client.sin_port, requestMethod, requestURL, 200);
            fflush(stdout);
            /* Write info to file. */
            fprintf(fp, "%s : %s:%d %s\n%s : %d\n", buf, inet_ntoa(client.sin_addr), client.sin_port, requestMethod, requestURL, 200);
            fflush(fp);

            /* We should close the connection. */
            shutdown(connfd, SHUT_RDWR);
            close(connfd);
            /* Close log file. */
            fclose(fp);
        } else {
            fprintf(stdout, "No message in five seconds.\n");
            fflush(stdout);
        }
    }
}
