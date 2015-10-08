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
#define MAX_HTML_SIZE 99999
#define PORT_LENGTH 6
#define CONTENT_LENGTH 6000
#define HEAD_LENGTH 1000
#define CONNECTION_TIME 10
#define MESSAGE_LENGTH 512
#define COOKIE_LENGTH 1000
#define MAX_NUMBER_OF_QUERIES 100
#define MAX_QUERY_LENGTH 100
#define NUMBER_OF_CONNECTIONS 5

/*
 *
 */
struct connection {
    int connfd;
    int keepAlive;
    time_t startTime;
};

/* A method that gets the first string from the request from
 * the client, which is the method of the request (i.e. GET, POST, HEAD, etc.)
 */
void getRequestMethod(char message[], char requestMethod[]) {
    gchar** splitMessage = g_strsplit(message, " ", MAX_TOKENS);
    strcpy(requestMethod, (char*)splitMessage[0]);
    g_strfreev(splitMessage);
}

/* A method that gets the second string from the request from
 * the client, which is the requested URL (i.e. "/color")
 */
void getRequestURL(char message[], char requestURL[]) {
    gchar** splitMessage = g_strsplit(message, " ", MAX_TOKENS);
    strcat(requestURL, splitMessage[1]);
    g_strfreev(splitMessage);
}

/* A method that is only called when the requested URL from the client
 * includes "?", which marks the beginning of a query (i.e. "?bg=red"). 
 * This method gets the query string that comes after the question mark.
 */
void getQuery(char requestURL[], char query[]) { 
    gchar** splitMessage = g_strsplit(requestURL, "?", MAX_TOKENS);
    strcpy(query, splitMessage[1]);
    g_strfreev(splitMessage);
}

/* A method that is only called when the requested URL includes a query.
 * This method gets the query parameters and puts it in an array containing all parameters
 * such that values at even positions in the array are values located left of the equation
 * mark in the query and values at odd positions in the array are values located right of the 
 * equation mark in the query.
 * Example: If query is "?bg=red&ex=ample" then "bg" is at position 0 in the array,
 * "red" is at position 1, "ex" at position 2 and so on.
 */
void getParam(char query[], char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH]) {
    gchar** splitMessage = g_strsplit(query, "&", MAX_TOKENS);
    int i = 0, j = 0;
    while(splitMessage[i] != NULL) {
        gchar** splitQuery = g_strsplit(splitMessage[i], "=", MAX_TOKENS);
        strcpy(allQueries[j], splitQuery[0]);
        //If we have a weird query with no equal sign
        //we don't try to access the value to the right of it.
        if(splitQuery[1] != NULL) {
            strcpy(allQueries[j+1], splitQuery[1]);
        }
        i += 1;
        j += 2; 
        g_strfreev(splitQuery);
    }
    g_strfreev(splitMessage);
}


/* A method that is only called when the requested method from the client
 * is a POST method. This method gets the string content that the user
 * is posting to us which will be shown in the body of the requested site. 
 */
void getContent(char message[], char content[]) {
    gchar** splitMessage = g_strsplit(message, "\r\n\r\n", MAX_TOKENS); 
    strcat(content, splitMessage[1]);
    g_strfreev(splitMessage);
}

/* A method that gets the header of a client request, i.e. everything before
 * the double line break in the request that seperates the header lines
 * and the actual content.
 */
void getHead(char message[], char head[]) {	
    gchar** splitMessage = g_strsplit(message, "\r\n\r\n", MAX_TOKENS); 
    strcat(head, splitMessage[0]);
    g_strfreev(splitMessage);
}

/* A method that gets the cookie from the header in the client request,
 * given that one exists.
 */
void getCookie(char message[], char cookie[]) {
    gchar** splitMessage = g_strsplit(message, "Cookie: ", MAX_TOKENS);
    
    if(splitMessage[1] != NULL) {
        strcat(cookie, splitMessage[1]);
    }
    else {
       cookie = NULL; 
    }
    g_strfreev(splitMessage);
}

/*
 *
 */
int getPersistence(char message[]) {
    char head[HEAD_LENGTH];
    gchar** splitMessage;
    gchar** tmpSplitMessage;
    getHead(message, head);
    tmpSplitMessage = g_strsplit(message, "Connection: ", MAX_TOKENS);
    
    if(tmpSplitMessage[1] != NULL) {
        splitMessage = g_strsplit(tmpSplitMessage[1], "\n", MAX_TOKENS);
        if(splitMessage[0] != NULL) {
            if((strcmp(splitMessage[0], "keep-alive\r") == 0) ||(strcmp(splitMessage[0], "Keep-Alive\r") == 0)) { 
                //fprintf(stdout, "Keep-Alive: %s\n", splitMessage[0]);
                //fflush(stdout);
                return 1;
            }
        }
    }

    return 0;
}

/* A method that creates the header that we will send in our server response to the client.
 * It includes the basic header fields Date, Server and Content-Type.
 */
void handleHEAD(char head[], int sizeOfBody) {
    time_t now;
    time(&now);
    char buf[sizeof "2011-10-08T07:07:09Z"];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    memset(head, 0, HEAD_LENGTH);
    strcpy(head, "HTTP/1.1 200 OK\r\n");
    strcat(head, "Date: ");
    strcat(head, buf);
    strcat(head, "\r\n");
    strcat(head, "Server: jordanthor\r\n");
    strcat(head, "Content-Type: text/html\r\n");
    strcat(head, "Content-Length: ");
    char s_sizeOfBody[512];
    sprintf(s_sizeOfBody, "%d", sizeOfBody);
    strcat(head, s_sizeOfBody);
    strcat(head, "\r\n\r\n");
}

/* A method that creates the header that we will send in our server response to the client.
 * It differs from the one above as it includes information on setting a cookie
 * and is only called when we want to set a cookie for the client.
 */
void handleHEADWithCookie(char head[], char variable[], char value[], int sizeOfBody) {
    time_t now;
    time(&now);
    char buf[sizeof "2011-10-08T07:07:09Z"];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    memset(head, 0, HEAD_LENGTH);
    strcpy(head, "HTTP/1.1 200 OK\r\n");
    strcat(head, "Date: ");
    strcat(head, buf);
    strcat(head, "\r\n");
    strcat(head, "Server: jordanthor\r\n");
    strcat(head, "Content-Type: text/html\r\n");
    strcat(head, "Content-Length: ");
    char s_sizeOfBody[512];
    sprintf(s_sizeOfBody, "%d", sizeOfBody);
    strcat(head, s_sizeOfBody);
    strcat(head, "\r\nSet-Cookie: ");
    strcat(head, variable);
    strcat(head, "=");
    strcat(head, value);
    strcat(head, "\r\n\r\n");
}


/*
 *
 */
void handleGET(int connfd, char requestURL[], char ip_address[], int port, char head[], char variable[], char value[], char cookie[], char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH]) {
    int colorCookie = 0;
    int i = 0;
    char body[MAX_HTML_SIZE], result[MAX_HTML_SIZE];
    memset(body, 0, MAX_HTML_SIZE);
    memset(result, 0, MAX_HTML_SIZE);

    while(strlen(allQueries[i]) != 0) {
        if(strcmp(allQueries[i], "bg") == 0) {
            strcpy(variable, allQueries[i]);
            strcpy(value, allQueries[i+1]);
            colorCookie = 1;
            break;
        }
        i += 2;
    }
    
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
        strcat(body, " style='background-color:");
        strcat(body, value);
        strcat(body, "'>\n");
    }
    else {
        if(strlen(cookie) > 0) {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
            int i = 0;
            while(strlen(allQueries[i]) > 0) {
                if(strcmp(allQueries[i], "bg") == 0) {
                    strcat(body, " style='background-color:");
                    strcat(body, allQueries[i+1]);
                    strcat(body, "'");
                    break;
                }
                i += 2;
            }
            strcat(body, ">\n");
        }
        else {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body>\n");
        }
    }
    
    strcat(body, "\t<p>\n\t\t");
    strcat(body, requestURL);
    strcat(body, "<br>\n\t\t");

    if(cookie != NULL) {
        int j = 0;
        while(strlen(allQueries[j]) > 0) {
            strcat(body, allQueries[j]);

            if(strlen(allQueries[j+1]) > 0) {
                strcat(body, "=");
                strcat(body, allQueries[j+1]);
            }

            strcat(body, "<br>\n\t\t");
            j += 2;
        }
    }

    strcat(body, ip_address);
    strcat(body, "<br>\n\t\t");
    char s_port[PORT_LENGTH];
    memset(s_port, 0, PORT_LENGTH);
    snprintf(s_port, PORT_LENGTH, "%d", port);
    strcat(body, s_port);
    strcat(body, "<br>\n\t</p>\n");
    strcat(body, "</body>\n</html>\n");

    int sizeOfBody = strlen(body);
 
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        handleHEADWithCookie(head, variable, value, sizeOfBody);
    }
    else {
        handleHEAD(head, sizeOfBody);
    }

    strcpy(result, head);
    strcat(result, body);
    ssize_t n = strlen(result);

    write(connfd, result, (size_t) n);
}

/*
 *
 */
void handlePOST(int connfd, char requestURL[], char ip_address[], int port, char content[], char head[], char variable[], char value[], char cookie[], char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH]) {
    int colorCookie = 0;
    int i = 0;
    char body[MAX_HTML_SIZE], result[MAX_HTML_SIZE];
    memset(body, 0, MAX_HTML_SIZE);
    memset(result, 0, MAX_HTML_SIZE);

    while(strlen(allQueries[i]) != 0) {
        if(strcmp(allQueries[i], "bg") == 0) {
            strcpy(variable, allQueries[i]);
            strcpy(value, allQueries[i+1]);
            colorCookie = 1;
            break;
        }

        i += 2;
    }
    
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
        strcat(body, " style='background-color:");
        strcat(body, value);
        strcat(body, "'>\n");
    }
    else { 
        if(strlen(cookie) > 0) {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
            int i = 0;

            while(strlen(allQueries[i]) != 0) {
                if(strcmp(allQueries[i], "bg") == 0) {
                    strcat(body, " style='background-color:");
                    strcat(body, allQueries[i+1]);
                    strcat(body, "'");
                    break;
                }

                i += 2;
            }
            strcat(body, ">\n");
        }
        else {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body>\n");
        }
    }

    strcat(body, "\t<p>\n\t\t");
    strcat(body, requestURL);
    strcat(body, "<br>\n\t\t");

    if(cookie != NULL) {
        int j = 0;
        while(strlen(allQueries[j]) != 0) {
            strcat(body, allQueries[j]);
            strcat(body, "=");
            strcat(body, allQueries[j+1]);
            strcat(body, "<br>\n\t\t");
            j += 2;
        }
    }

    strcat(body, ip_address);
    strcat(body, "<br>\n\t\t");
    char s_port[PORT_LENGTH];
    memset(s_port, 0, PORT_LENGTH);
    snprintf(s_port, PORT_LENGTH, "%d", port);
    strcat(body, s_port);
    strcat(body, "<br>\n\t</p>\n\t<p>\n\t\t");
    strcat(body, content);
    strcat(body, "<br>\n\t</p>\n</body>\n</html>\n");

    int sizeOfBody = strlen(body);
 
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        handleHEADWithCookie(head, variable, value, sizeOfBody);
    }
    else {
        handleHEAD(head, sizeOfBody);
    }

    strcpy(result, head);
    strcat(result, body);
    ssize_t n = strlen(result);

    write(connfd, result, (size_t) n);
}

/*
 *
 */
void handler(int connfd, struct sockaddr_in client, FILE *fp, char message[], char ip_address[]) {
    char requestMethod[REQUEST_METHOD_LENGTH];
    char requestURL[REQUEST_URL_LENGTH];
    char content[CONTENT_LENGTH];
    char head[HEAD_LENGTH];
    char query[REQUEST_URL_LENGTH];
    char variable[REQUEST_URL_LENGTH];
    char value[REQUEST_URL_LENGTH];
    char cookie[COOKIE_LENGTH];
    char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH];
    
    memset(requestMethod, 0, REQUEST_METHOD_LENGTH);
    memset(requestURL, 0, REQUEST_URL_LENGTH);
    memset(content, 0, CONTENT_LENGTH);
    memset(head, 0, HEAD_LENGTH);
    memset(query, 0, REQUEST_URL_LENGTH);
    memset(variable, 0, REQUEST_URL_LENGTH);
    memset(value, 0, REQUEST_URL_LENGTH);
    memset(cookie, 0, COOKIE_LENGTH);
    memset(allQueries, 0, sizeof(char) * MAX_NUMBER_OF_QUERIES * MAX_QUERY_LENGTH);

    strcpy(requestURL, "http://localhost/");
    strcat(requestURL, ip_address);
    time_t now;
    time(&now);
    char buf[sizeof "2011-10-08T07:07:09Z"];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    getRequestMethod(message, requestMethod);
    getRequestURL(message, requestURL);
    getCookie(message, cookie);

    if(strchr(requestURL, '?') != NULL) {
        getQuery(requestURL, query);
        //getParam(query, variable, value);
        getParam(query, allQueries);
    }

    /* GET. */ 
    if(strcmp(requestMethod, "GET") == 0) {
        handleGET(connfd, requestURL, inet_ntoa(client.sin_addr), client.sin_port, head, variable, value, cookie, allQueries);
    }
    /* POST. */
    else if(strcmp(requestMethod, "POST") == 0) {
        getContent(message, content);
        handlePOST(connfd, requestURL, inet_ntoa(client.sin_addr), client.sin_port, content, head, variable, value, cookie, allQueries);
    }
    /* HEAD. */
    else if(strcmp(requestMethod, "HEAD") == 0) {
        ssize_t n = HEAD_LENGTH;
        handleHEAD(head, 0);
        write(connfd, head, (size_t) n);
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
}

int main(int argc, char **argv) {
    FILE *fp;
    fprintf(stderr, "%d \n", argc);
    fflush(stderr);
    int sockfd;
    struct sockaddr_in server, client;
    char message[MESSAGE_LENGTH];
    time_t currTime;
    time(&currTime);

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
    struct connection conn;
    conn.connfd = -1;    
    conn.keepAlive = 0;
    
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int retval;
        memset(message, 0, MESSAGE_LENGTH);

        /* Check whether there is data on the socket fd. */
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        /* Wait for five seconds. */
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        time(&currTime);
        
        if((currTime - conn.startTime) > CONNECTION_TIME) {
            shutdown(conn.connfd, SHUT_RDWR);
            close(conn.connfd);
            conn.connfd = -1;
        } 

        if(conn.connfd != -1) {
            FD_SET(conn.connfd, &rfds);
            retval = select(conn.connfd + 1, &rfds, NULL, NULL, &tv);
        }
        else {
            retval = select(sockfd + 1, &rfds, NULL, NULL, &tv);
        }

        //sleep(2);
        
        if (retval == -1) {
            perror("select()");
        } else if (retval > 0) {
            /* Open file. */
            fp = fopen("src/httpd.log", "a+");

            /* Data is available, receive it. */
            //assert(FD_ISSET(sockfd, &rfds));

            /* Copy to len, since recvfrom may change it. */
            socklen_t len = (socklen_t) sizeof(client);

            if(FD_ISSET(sockfd, &rfds)) {
                conn.connfd = accept(sockfd, (struct sockaddr *) &client, &len);
                time(&conn.startTime);
            }
            
            ssize_t n = read(conn.connfd, message, sizeof(message) - 1);
            message[n] = '\0';        

            if(strlen(message) > 0) {
                conn.keepAlive = getPersistence(message);
                time(&conn.startTime);
                handler(conn.connfd, client, fp, message, argv[1]);
            }

            /*
            else {
                shutdown(conn.connfd, SHUT_RDWR);
                close(conn.connfd);
                conn.connfd = -1;
            }
            */

            if(!conn.keepAlive) {
                shutdown(conn.connfd, SHUT_RDWR);
                close(conn.connfd);
                conn.connfd = -1;
            }

            fclose(fp);
        } else {
            fprintf(stdout, "No message in five seconds.\n");
            fflush(stdout);
            shutdown(conn.connfd, SHUT_RDWR);
            close(conn.connfd);
            conn.connfd = -1;
        }
    }
}
