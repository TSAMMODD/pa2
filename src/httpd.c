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

/* Macros */
#define REQUEST_METHOD_LENGTH 8
#define REQUEST_URL_LENGTH 100
#define CONTENT_LENGTH 6000
#define PORT_LENGTH 6
#define MAX_QUERY_LENGTH 100
#define MESSAGE_LENGTH 512
#define COOKIE_LENGTH 1000
#define MAX_HTML_LENGTH 99999
#define HEAD_LENGTH 1000
#define MAX_TOKENS -1
#define CONNECTION_TIME 10
#define MAX_NUMBER_OF_QUERIES 100
#define NUMBER_OF_CONNECTIONS 5

/* A struct containing information about a connection, that is its file descriptor, 
 * whether the connection is "keep-alive" or not, and the starting time of the connection.
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
    if(splitMessage[0] != NULL) {
        strcpy(requestMethod, (char*)splitMessage[0]);
    }
    else {
        requestMethod = NULL;
    }

    g_strfreev(splitMessage);
}

/* A method that gets the second string from the request from
 * the client, which is the requested URL (i.e. "/color")
 */
void getRequestURL(char message[], char requestURL[]) {
    gchar** splitMessage = g_strsplit(message, " ", MAX_TOKENS);
    if(splitMessage[1] != NULL) {
        strcat(requestURL, splitMessage[1]);
    }
    else {
        requestURL = NULL;
    }

    g_strfreev(splitMessage);
}

/* A method that is only called when the requested URL from the client
 * includes "?", which marks the beginning of a query (i.e. "?bg=red"). 
 * This method gets the query string that comes after the question mark.
 */
void getQuery(char requestURL[], char query[]) { 
    gchar** splitMessage = g_strsplit(requestURL, "?", MAX_TOKENS);
    if(splitMessage[1] != NULL) {
        strcpy(query, splitMessage[1]);
    }
    else {
        query = NULL;
    }

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
    if(splitMessage[1] != NULL) {
        strcat(content, splitMessage[1]);
    }
    else {
        content = NULL;
    }

    g_strfreev(splitMessage);
}

/* A method that gets the header of a client request, i.e. everything before
 * the double line break in the request that seperates the header lines
 * and the actual content.
 */
void getHead(char message[], char head[]) {	
    gchar** splitMessage = g_strsplit(message, "\r\n\r\n", MAX_TOKENS); 
    if(splitMessage[0] != NULL) {
        strcat(head, splitMessage[0]);
    }
    else {
        head = NULL;
    }
    
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

/* A method that gets the type of the connection we are dealing with,
 * i.e. "HTTP/1.1" or "HTTP/1.0" etc.
 */
void typeOfConnection(char message[], char type[]) {
    gchar** splitMessage = g_strsplit_set(message, " \n", MAX_TOKENS);
    if(splitMessage[2] != NULL) {
        strcpy(type, splitMessage[2]);
    }
    else {
        type = NULL;
    }
}

/* Method that tells if a client requested a persistence connection or not.
 * If the request contains HTTP/1.1 the requested connection is persistence but
 * if that does not apply than we have to search for a "Connection: keep-alive"
 * message in the header from the client.
 */
int getPersistence(char message[]) {
    char head[HEAD_LENGTH];
    char type[MESSAGE_LENGTH];
    memset(head, 0, HEAD_LENGTH);
    memset(type, 0, MESSAGE_LENGTH);
    typeOfConnection(message, type);

    if((type != NULL) && (strcmp(type, "HTTP/1.1\r") == 0)) {
        return 1;
    }

    gchar** splitMessage;
    gchar** tmpSplitMessage;
    getHead(message, head);
    tmpSplitMessage = g_strsplit(message, "Connection: ", MAX_TOKENS);
    
    if(tmpSplitMessage[1] != NULL) {
        splitMessage = g_strsplit(tmpSplitMessage[1], "\n", MAX_TOKENS);
        if(splitMessage[0] != NULL) {
            if((strcmp(splitMessage[0], "keep-alive\r") == 0) ||(strcmp(splitMessage[0], "Keep-Alive\r") == 0)) { 
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


/* A method that is called when we handle a GET request from a client.
 * It creates our server response as a HTML document to such a request
 * and includes the correctly structured header and content.
 * Parameters sent to this function are connfd (the connection file descriptor), 
 * requestURL (the client's requested URL), ip_address (the client's IP address), 
 * port (the client's port), head (the header lines sent in our server response),
 * variable (if there is a query, this is the value left of the equation mark, i.e. "bg"),
 * value (if there is a query, this is the value right of the equation mark, i.e. "red"),
 * cookie (the cookie received from the client, if it exists), 
 * allQueries (an array of all query parameters, described above in the getParam function).
 */
void handleGET(int connfd, char requestURL[], char ip_address[], int port, char head[], char variable[], char value[], char cookie[], char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH]) {
    int colorCookie = 0;
    int i = 0;
    char body[MAX_HTML_LENGTH], result[MAX_HTML_LENGTH];
    memset(body, 0, MAX_HTML_LENGTH);
    memset(result, 0, MAX_HTML_LENGTH);

    /* Go through all the queries from the client and search for "bg". If
     * that is found than we set the color to the body tag of the response.
     */
    while(strlen(allQueries[i]) != 0) {
        if(strcmp(allQueries[i], "bg") == 0) {
            strcpy(variable, allQueries[i]);
            strcpy(value, allQueries[i+1]);
            colorCookie = 1;
            break;
        }
        i += 2;
    }
        
    /* If we found a query that contains "bg" than we set the bg-color. 
     */     
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
        strcat(body, " style='background-color:");
        strcat(body, value);
        strcat(body, "'>\n");
    }
    /* If we did not find "bg" in the queries we search in cookies. If
     * we find "bg" in the the cookies then we set the bg-color to the
     * value there.
     */    
    else {
        if(strlen(cookie) > 0) {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
            gchar** splitCookie = g_strsplit(cookie, "=", MAX_TOKENS);

            if((strcmp(splitCookie[0], "bg") == 0) && (splitCookie[1] != NULL)) {
                gchar** cleanValue = g_strsplit_set(splitCookie[1], " \n\r", MAX_TOKENS);
                strcat(body, " style='background-color:");
                strcat(body, cleanValue[0]);
                strcat(body, "'");
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

    /* Set the query parameters to the body of the html. */ 
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
 
    /* If we got a query that contained "bg" then we handle the head 
     * with cookie. (That is add the cookie to the header response).
     */
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        handleHEADWithCookie(head, variable, value, sizeOfBody);
    }
    /* Else we handle the head normally. */
    else {
        handleHEAD(head, sizeOfBody);
    }

    strcpy(result, head);
    strcat(result, body);
    ssize_t n = strlen(result);

    write(connfd, result, (size_t) n);
}

/* A method that is called when we handle a POST request from a client.
 * It creates our server response as a HTML document to such a request
 * and includes the correctly structured header and content.
 * Parameters sent to this function are connfd (the connection file descriptor), 
 * requestURL (the client's requested URL), ip_address (the client's IP address), 
 * port (the client's port), content (the content posted by the client), 
 * head (the header lines sent in our server response),
 * variable (if there is a query, this is the value left of the equation mark, i.e. "bg"),
 * value (if there is a query, this is the value right of the equation mark, i.e. "red"),
 * cookie (the cookie received from the client, if it exists), 
 * allQueries (an array of all query parameters, described above in the getParam function).
 */
void handlePOST(int connfd, char requestURL[], char ip_address[], int port, char content[], char head[], char variable[], char value[], char cookie[], char allQueries[MAX_NUMBER_OF_QUERIES][MAX_QUERY_LENGTH]) {
    char body[MAX_HTML_LENGTH], result[MAX_HTML_LENGTH];
    memset(body, 0, MAX_HTML_LENGTH);
    memset(result, 0, MAX_HTML_LENGTH);

    /* Go through all the queries from the client and search for "bg". If
     * that is found than we set the color to the body tag of the response.
     */
    int colorCookie = 0;
    int i = 0;
    while(strlen(allQueries[i]) != 0) {
        if(strcmp(allQueries[i], "bg") == 0) {
            strcpy(variable, allQueries[i]);
            strcpy(value, allQueries[i+1]);
            colorCookie = 1;
            break;
        }

        i += 2;
    }
    
    /* If we found a query that contains "bg" than we set the bg-color. 
     */     
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
        strcat(body, " style='background-color:");
        strcat(body, value);
        strcat(body, "'>\n");
    }
    /* If we did not find "bg" in the queries we search in cookies. If
     * we find "bg" in the the cookies then we set the bg-color to the
     * value there.
     */    
    else { 
        if(strlen(cookie) > 0) {
            strcat(body, "<!DOCTYPE html>\n<html>\n<head></head>\n<body");
            gchar** splitCookie = g_strsplit(cookie, "=", MAX_TOKENS);

            if((strcmp(splitCookie[0], "bg") == 0) && (splitCookie[1] != NULL)) {
                gchar** cleanValue = g_strsplit_set(splitCookie[1], " \n\r", MAX_TOKENS);
                strcat(body, " style='background-color:");
                strcat(body, cleanValue[0]);
                strcat(body, "'");
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

    /* Set the query parameters to the body of the html. */ 
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
 
    /* If we got a query that contained "bg" then we handle the head 
     * with cookie. (That is add the cookie to the header response).
     */
    if((strchr(requestURL, '?') != NULL) && colorCookie == 1) {
        handleHEADWithCookie(head, variable, value, sizeOfBody);
    }
    /* Else we handle the head normally. */
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
    /* Create filepointer for log file */
    FILE *fp;
    fprintf(stdout, "SERVER INITIALIZING -- %d C00L 4 SCH00L!\n", argc);
    fflush(stdout);
    /* Create sockfd */
    int sockfd;
    /* Create a sockaddress for server and client */
    struct sockaddr_in server, client;
    /* Create a message of size MESSAGE_LENGTH to store message from client */
    char message[MESSAGE_LENGTH];
    /* Create and get current time for use with persistence */
    time_t currTime;
    time(&currTime);
    /* Struct an array of connections where we will store connections for parallel */
    struct connection connections[NUMBER_OF_CONNECTIONS];
    
    /* Initialize connections fd's as -1 to know they're not set later, also 
     * initialize their persistence setting to false (keepalive = false).
     */
    int i = 0;
    for(i; i < NUMBER_OF_CONNECTIONS; i++){
        connections[i].connfd = -1;
        connections[i].keepAlive = 0;
    }

    /* Create and bind a UDP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    /* Clear anything that might have left in server */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    /* Network functions need arguments in network byte order instead of
     * host byte order. The macros htonl, htons convert the values, 
     */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(atoi(argv[1]));
    bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

    /* Before we can accept messages, we have to listen to the port. We allow five
     * ("NUMBER_OF_CONNECTIONS") connection to queue parallel.
     */
    listen(sockfd, NUMBER_OF_CONNECTIONS);
    
    for (;;) {
        /* Create fd_set to store fd's */
        fd_set rfds;
        /* TimeVal for select timeout */
        struct timeval tv;
        /* Retval for return value from select */
        int retval;
        /* Clear the message of unwanted old bytes */
        memset(message, 0, MESSAGE_LENGTH);

        /* Check whether there is data on the socket fd. */
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        /* Wait for five seconds in select() timeout. */
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        /* Update current time for use with 30 second timeout for persistence */
        time(&currTime);
        /* Store the highest fd to know how far to check in select */
        int highestFD = sockfd;
        
        /* Loop through all connections in list, check if they're connected
         * and if they are check if their 30 second keep-alive time is up,
         * if it is up: close the connection and reset the fd to -1 to know
         * that now it's not in use.
         * if 30 seconds from connection message: put it in the fd_set to
         * so that select will cover it. Also put it as the highest fd if
         * it is higher than the last connection or sockfd so that select
         * will cover that far.
         */
        for(i = 0; i < NUMBER_OF_CONNECTIONS; i++){
            if(connections[i].connfd != -1){
                if((currTime - connections[i].startTime) > CONNECTION_TIME){
                    shutdown(connections[i].connfd, SHUT_RDWR);
                    close(connections[i].connfd);
                    connections[i].connfd = -1;
                }
            }
            if(connections[i].connfd != -1){
                FD_SET(connections[i].connfd, &rfds);
                if(highestFD < connections[i].connfd){
                    highestFD = connections[i].connfd;
                }
            }        
        }
        
        /* Scan all connections for data to be read, scan for 5 seconds */
        retval = select(highestFD + 1, &rfds, NULL, NULL, &tv);

        if (retval == -1) {
            perror("select()");
        } else if (retval > 0) {
            /* When retval is > 0 there is something to be read for any of 
             * the connections or socfd. 
             */
            /* Open file. */
            fp = fopen("src/httpd.log", "a+");

            /* Copy to len, since recvfrom may change it. */
            socklen_t len = (socklen_t) sizeof(client);
            
            /* Check if sockfd had data at select() */
            if(FD_ISSET(sockfd, &rfds)) {
                /* This means there is a new, unhandled connection ready 
                 * to be dealt with so we accept it and store it in a temp
                 * variable to be sure we can add it to our list later.
                 */
                int newconnfd = accept(sockfd, (struct sockaddr *) &client, &len);
                
                /* Loop through all connections and see if any one of them
                 * can store our new connection.
                 * If it there is room free in our list, store the connections
                 * fd and give it a start time for persistence checking.
                 * Also, give the temp variable for the new connection a
                 * value of -1 to know later that the connection is being stored.
                 * Break when we find an empty slot.
                 */ 
                for(i=0; i < NUMBER_OF_CONNECTIONS; i++){
                    if(connections[i].connfd == -1){
                        connections[i].connfd =  newconnfd;
                        time(&connections[i].startTime);
                        newconnfd = -1;
                        break;
                    }
                }
                /* Check if the temp connection was stored and if there was
                 * no room: close the connection.
                 */
                if(newconnfd != -1) {
                    shutdown(newconnfd, SHUT_RDWR);
                    close(newconnfd);
                }
            }
            
            /*  Read/handle loop for connections */ 
            for(i=0; i < NUMBER_OF_CONNECTIONS; i++){
                /* Check if the connection is active */
                if(connections[i].connfd != -1){
                    /* Check if the connection has data ready to be read */
                    if(FD_ISSET(connections[i].connfd, &rfds)){
                        /* Clear old message contents */
                        memset(message, 0, MESSAGE_LENGTH);
                        /* Read from the connection */
                        ssize_t n = read(connections[i].connfd, message, sizeof(message) - 1);
                        message[n] = '\0';        
                        /* If the message gotten from the connection has a size
                         * larger than 0, check if it supposed to be persistent,
                         * reset the start time of the connection so it can go 
                         * another 30 seconds and send the message to our handler.
                         */
                        if(strlen(message) > 0) {
                            connections[i].keepAlive = getPersistence(message);
                            time(&connections[i].startTime);
                            /* Handle the message's content, also send the
                             * connections' fd, client, logfile and port of server.
                             */
                            handler(connections[i].connfd, client, fp, message, argv[1]);
                        }
                        else {
                            /* Here the message size is 0 (or less) which we interpret
                             * as the persistent connection telling us it is done with
                             * sending us messages. Close the connection and reset its
                             * slot in the connection list.
                             */
                            shutdown(connections[i].connfd, SHUT_RDWR);
                            close(connections[i].connfd);
                            connections[i].connfd = -1;
                        }

                        /* Check if the connection should be kept alive and close it
                         * if it isn't. 
                         */
                        if(connections[i].keepAlive == 0) {
                            shutdown(connections[i].connfd, SHUT_RDWR);
                            close(connections[i].connfd);
                            connections[i].connfd = -1;
                        }

                    }
                }
            }
            /* Close the logfile */
            fclose(fp);
        } else {
            /* Select has no connections to be read from. */
            fprintf(stdout, "No message in five seconds\n");
            fflush(stdout);
        }
    }
}
