/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

char copyright[] =
    "@(#) Copyright (c) 1983 Regents of the University of California.\n"
    "All rights reserved.\n";

/*
 * From: @(#)main.c	5.10 (Berkeley) 3/1/91
 */
char main_rcsid[] = "$Id: main.c,v 1.15 2000/07/22 19:06:29 dholland Exp $";

/* Many bug fixes are from Jim Guyton <guyton@rand-unix> */

/*
 * TFTP User Program -- Command Interface.
 */
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
/* #include <netinet/ip.h> <--- unused? */
#include "tftpsubs.h" /* for mysignal() */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TIMEOUT 5 /* secs between rexmt's */

struct sockaddr_storage s_inn;
socklen_t s_inn_len;
int f = -1;
int trace;
int verbose;
int rexmtval = TIMEOUT;
int maxtimeout = 5 * TIMEOUT;
sigjmp_buf toplevel;
void sendfile(int fd, char *name, char *modestr);
void recvfile(int fd, char *name, char *modestr);

static int connected =
    AF_UNSPEC; /* If non-zero, contains active address family! */
static char service[NI_MAXSERV] = "tftp";
static char mode[32];
static char line[200];
static int margc;
static char *margv[20];
static const char *prompt = "tftp";
static struct servent *sp;

static void intr(int);

void makeargv(void);
void command(int top);
void quit(int argc, char *argv[]);
void help(int argc, char *argv[]);
void setverbose(int argc, char *argv[]);
void settrace(int argc, char *argv[]);
void status(int argc, char *argv[]);
void get(int argc, char *argv[]);
void put(int argc, char *argv[]);
void setpeer(int argc, char *argv[]);
void modecmd(int argc, char *argv[]);
void setrexmt(int argc, char *argv[]);
void settimeout(int argc, char *argv[]);
void setbinary(int argc, char *argv[]);
void setascii(int argc, char *argv[]);
void setmode(const char *newmode);
void putusage(const char *s);
void getusage(const char *s);

#define HELPINDENT ((int)sizeof("connect"))

struct cmd
{
    const char *name;
    const char *help;
    void (*handler)(int, char *[]);
};

char vhelp[] = "toggle verbose mode";
char thelp[] = "toggle packet tracing";
char chelp[] = "connect to remote tftp";
char qhelp[] = "exit tftp";
char hhelp[] = "print help information";
char shelp[] = "send file";
char rhelp[] = "receive file";
char mhelp[] = "set file transfer mode";
char sthelp[] = "show current status";
char xhelp[] = "set per-packet retransmission timeout";
char ihelp[] = "set total retransmission timeout";
char ashelp[] = "set mode to netascii";
char bnhelp[] = "set mode to octet";

struct cmd cmdtab[] = {{"connect", chelp, setpeer},
                       {"mode", mhelp, modecmd},
                       {"put", shelp, put},
                       {"get", rhelp, get},
                       {"quit", qhelp, quit},
                       {"verbose", vhelp, setverbose},
                       {"trace", thelp, settrace},
                       {"status", sthelp, status},
                       {"binary", bnhelp, setbinary},
                       {"ascii", ashelp, setascii},
                       {"rexmt", xhelp, setrexmt},
                       {"timeout", ihelp, settimeout},
                       {"?", hhelp, help},
                       {0, 0, 0}};

static struct cmd *getcmd(const char *name);
static char *tail(char *filename);

void initsock(int af)
{
    struct sockaddr_storage s_in;

    if (f >= 0)
        close(f);

    f = socket(af, SOCK_DGRAM, 0);
    if (f < 0) {
        perror("tftp: socket");
        exit(3);
    }

    memset(&s_in, 0, sizeof(s_in));
    s_in.ss_family = af;
    if (bind(f, (struct sockaddr *)&s_in, sizeof(s_in)) < 0) {
        perror("tftp: bind");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    int top;

    /* Make a minimal sanity check. */
    sp = getservbyname("tftp", "udp");
    if (sp == 0) {
        fprintf(stderr, "tftp: udp/tftp: unknown service\n");
        exit(1);
    }

    strcpy(mode, "netascii");
    mysignal(SIGINT, intr);
    if (argc > 1) {
        if (sigsetjmp(toplevel, 1) != 0)
            exit(0);
        setpeer(argc, argv);
    }
    top = sigsetjmp(toplevel, 1) == 0;
    for (;;)
        command(top);
}

static char hostname[NI_MAXHOST];

void setpeer(int argc, char *argv[])
{
    struct addrinfo hints, *aiptr, *ai;
    size_t len;

    if (argc < 2) {
        strcpy(line, "Connect ");
        printf("(to) ");
        len = strlen(line);
        fgets(line + len, sizeof(line) - len, stdin);
        makeargv();
        argc = margc;
        argv = margv;
    }
    /* We should have 2 or 3 args now: the cmd and its
     * parameters. If not, we bail out here.
     */
    if (argc != 2 && argc != 3) {
        printf("usage: %s host-name [port]\n", argv[0]);
        return;
    }

    /* First we record the service name. Default is "tftp".
     */
    if (argc == 3) {
        if (argv[2] == NULL || *argv[2] == '\0') {
            printf("%s: bad port name\n", argv[2]);
            connected = 0;
            return;
        }
        strncpy(service, argv[2], sizeof(service));
        service[sizeof(service) - 1] = '\0';
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ALL | AI_V4MAPPED | AI_ADDRCONFIG | AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(argv[1], service, &hints, &aiptr)) {
        connected = 0;
        printf("%s: unknown host\n", argv[1]);
        return;
    }

    /* Choose first applicable address. */
    ai = aiptr;

    while (ai && (ai->ai_family != AF_INET6) && (ai->ai_family != AF_INET))
        ai = ai->ai_next;

    if (ai == NULL) {
        connected = 0;
        freeaddrinfo(aiptr);
        printf("%s: unknown host\n", argv[1]);
        return;
    }

    memcpy(&s_inn, ai->ai_addr, ai->ai_addrlen);
    s_inn_len = ai->ai_addrlen;
    connected = ai->ai_family;
    strncpy(hostname, aiptr->ai_canonname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    freeaddrinfo(aiptr);

    /* Test and set socket for the relevant address family. */
    initsock(connected);
}

struct modes
{
    const char *m_name;
    const char *m_mode;
} modes[] = {{"ascii", "netascii"},
             {"netascii", "netascii"},
             {"binary", "octet"},
             {"image", "octet"},
             {"octet", "octet"},
             /*      { "mail",       "mail" },       */
             {0, 0}};

void modecmd(int argc, char *argv[])
{
    register struct modes *p;
    const char *sep;

    if (argc < 2) {
        printf("Using %s mode to transfer files.\n", mode);
        return;
    }
    if (argc == 2) {
        for (p = modes; p->m_name; p++)
            if (strcmp(argv[1], p->m_name) == 0)
                break;
        if (p->m_name) {
            setmode(p->m_mode);
            return;
        }
        printf("%s: unknown mode\n", argv[1]);
        /* drop through and print usage message */
    }

    printf("usage: %s [", argv[0]);
    sep = " ";
    for (p = modes; p->m_name; p++) {
        printf("%s%s", sep, p->m_name);
        if (*sep == ' ')
            sep = " | ";
    }
    printf(" ]\n");
    return;
}

void setbinary(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    setmode("octet");
}

void setascii(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    setmode("netascii");
}

void setmode(const char *newmode)
{
    strncpy(mode, newmode, sizeof(mode));
    if (verbose)
        printf("mode set to %s\n", mode);
}

/*
 * Send file(s).
 */
void put(int argc, char *argv[])
{
    int fd;
    register int n;
    register char *ccp, *targ;
    size_t len;

    if (argc < 2) {
        strcpy(line, "send ");
        printf("(file) ");
        len = strlen(line);
        fgets(line + len, sizeof(line) - len, stdin);
        makeargv();
        argc = margc;
        argv = margv;
    }
    if (argc < 2) {
        putusage(argv[0]);
        return;
    }
    targ = argv[argc - 1];
    if (strchr(argv[argc - 1], ':')) {
        char *cp;
        struct addrinfo hints, *aiptr, *ai;
        int status;

        for (n = 1; n < argc - 1; n++)
            if (strchr(argv[n], ':')) {
                putusage(argv[0]);
                return;
            }
        cp = argv[argc - 1];
        /* Last colon. Numerical IPv6 addresses! */
        targ = strrchr(cp, ':');
        *targ++ = 0;

        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_CANONNAME;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        status = getaddrinfo(cp, service, &hints, &aiptr);
        if (status != 0) {
            fprintf(stderr, "tftp: %s: %s\n", cp, gai_strerror(status));
            return;
        }

        ai = aiptr;
        while (ai && (ai->ai_family != AF_INET) && (ai->ai_family != AF_INET6))
            ai = ai->ai_next;

        if (ai == NULL) {
            freeaddrinfo(aiptr);
            fprintf(stderr, "tftp: %s: %s\n", cp, "Address not found");
            return;
        }

        memcpy(&s_inn, ai->ai_addr, ai->ai_addrlen);
        s_inn_len = ai->ai_addrlen;
        connected = ai->ai_family;
        strncpy(hostname, aiptr->ai_canonname, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';
        freeaddrinfo(aiptr);
        initsock(connected);
    }
    if (!connected) {
        printf("No target machine specified.\n");
        return;
    }
    if (argc < 4) {
        ccp = argc == 2 ? tail(targ) : argv[1];
        fd = open(ccp, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "tftp: ");
            perror(ccp);
            return;
        }
        if (verbose)
            printf("putting %s to %s:%s [%s]\n", ccp, hostname, targ, mode);

        sendfile(fd, targ, mode);
        return;
    }
    /* this assumes the target is a directory */
    /* on a remote unix system.  hmmmm.  */
    ccp = targ + strlen(targ);
    *ccp++ = '/';
    for (n = 1; n < argc - 1; n++) {
        strcpy(ccp, tail(argv[n]));
        fd = open(argv[n], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "tftp: ");
            perror(argv[n]);
            continue;
        }
        if (verbose)
            printf("putting %s to %s:%s [%s]\n", argv[n], hostname, targ, mode);

        sendfile(fd, targ, mode);
    }
}

void putusage(const char *s)
{
    printf("usage: %s file ... host:target, or\n", s);
    printf("       %s file ... target (when already connected)\n", s);
}

/*
 * Receive file(s).
 */
void get(int argc, char *argv[])
{
    int fd;
    register int n;
    register char *cp;
    char *src;
    size_t len;

    if (argc < 2) {
        strcpy(line, "get ");
        printf("(files) ");
        len = strlen(line);
        fgets(line + len, sizeof(line) - len, stdin);
        makeargv();
        argc = margc;
        argv = margv;
    }
    if (argc < 2) {
        getusage(argv[0]);
        return;
    }
    if (!connected) {
        for (n = 1; n < argc; n++)
            if (strchr(argv[n], ':') == 0) {
                getusage(argv[0]);
                return;
            }
    }
    for (n = 1; n < argc; n++) {
        /* Last colon. Numerical IPv6 addresses! */
        src = strrchr(argv[n], ':');
        if (src == NULL)
            src = argv[n];
        else {
            struct addrinfo hints, *aiptr, *ai;
            int status;

            *src++ = 0;
            memset(&hints, 0, sizeof(hints));
            hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_CANONNAME;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;

            status = getaddrinfo(argv[n], service, &hints, &aiptr);
            if (status) {
                fprintf(stderr, "tftp: %s: %s\n", argv[n],
                        gai_strerror(status));
                continue;
            }

            ai = aiptr;
            while (ai && (ai->ai_family != AF_INET) &&
                   (ai->ai_family != AF_INET6))
                ai = ai->ai_next;

            if (ai == NULL) {
                freeaddrinfo(aiptr);
                fprintf(stderr, "tftp: %s: %s\n", argv[n], "Address not found");
                continue;
            }

            memcpy(&s_inn, ai->ai_addr, ai->ai_addrlen);
            s_inn_len = ai->ai_addrlen;
            connected = ai->ai_family;
            strncpy(hostname, aiptr->ai_canonname, sizeof(hostname));
            hostname[sizeof(hostname) - 1] = 0;
            freeaddrinfo(aiptr);
            initsock(connected);
        }
        if (argc == 2 || (argc == 3 && n == 1 && !strchr(argv[2], ':'))) {
            cp = argc == 3 ? argv[2] : tail(src);
            fd = creat(cp, 0644);
            if (fd < 0) {
                fprintf(stderr, "tftp: ");
                perror(cp);
                return;
            }
            if (verbose)
                printf("getting from %s:%s to %s [%s]\n", hostname, src, cp,
                       mode);

            recvfile(fd, src, mode);
            break;
        }
        cp = tail(src); /* new .. jdg */
        fd = creat(cp, 0644);
        if (fd < 0) {
            fprintf(stderr, "tftp: ");
            perror(cp);
            continue;
        }
        if (verbose)
            printf("getting from %s:%s to %s [%s]\n", hostname, src, cp, mode);

        recvfile(fd, src, mode);
    }
}

void getusage(const char *s)
{
    printf("usage: %s host:file host:file ... file, or\n", s);
    printf("       %s file file ... file   if connected, or\n", s);
    printf("       %s host:rfile lfile\n", s);
}

void setrexmt(int argc, char *argv[])
{
    int t;
    size_t len;

    if (argc < 2) {
        strcpy(line, "Rexmt-timeout ");
        printf("(value) ");
        len = strlen(line);
        fgets(line + len, sizeof(line) - len, stdin);
        makeargv();
        argc = margc;
        argv = margv;
    }
    if (argc != 2) {
        printf("usage: %s value\n", argv[0]);
        return;
    }
    t = strtol(argv[1], NULL, 10);
    if (t < 0)
        printf("%s: bad value\n", argv[1]);
    else
        rexmtval = t;
}

void settimeout(int argc, char *argv[])
{
    int t;
    size_t len;

    if (argc < 2) {
        strcpy(line, "Maximum-timeout ");
        printf("(value) ");
        len = strlen(line);
        fgets(line + len, sizeof(line) - len, stdin);
        makeargv();
        argc = margc;
        argv = margv;
    }
    if (argc != 2) {
        printf("usage: %s value\n", argv[0]);
        return;
    }
    t = strtol(argv[1], NULL, 10);
    if (t < 0)
        printf("%s: bad value\n", argv[1]);
    else
        maxtimeout = t;
}

void status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (connected)
        printf("Connected to %s.\n", hostname);
    else
        printf("Not connected.\n");
    printf("Mode: %s Verbose: %s Tracing: %s\n", mode, verbose ? "on" : "off",
           trace ? "on" : "off");
    printf("Rexmt-interval: %d seconds, Max-timeout: %d seconds\n", rexmtval,
           maxtimeout);
}

static void intr(int ignore)
{
    (void)ignore;

    mysignal(SIGALRM, SIG_IGN);
    alarm(0);
    siglongjmp(toplevel, -1);
}

static char *tail(char *filename)
{
    register char *s;

    while (*filename) {
        s = strrchr(filename, '/');
        if (s == NULL)
            break;
        if (s[1])
            return (s + 1);
        *s = '\0';
    }
    return filename;
}

/*
 * Command parser.
 */
void command(int top)
{
    register struct cmd *c;

    if (!top)
        putchar('\n');
    for (;;) {
        printf("%s> ", prompt);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (feof(stdin)) {
                quit(0, NULL);
            } else {
                continue;
            }
        }
        if (line[0] == 0)
            continue;
        makeargv();
        if (margc < 1) {
            /* empty line */
            continue;
        }
        c = getcmd(margv[0]);
        if (c == (struct cmd *)-1) {
            printf("?Ambiguous command\n");
            continue;
        }
        if (c == 0) {
            printf("?Invalid command\n");
            continue;
        }
        (*c->handler)(margc, margv);
    }
}

struct cmd *getcmd(const char *name)
{
    const char *p, *q;
    struct cmd *c, *found;
    int nmatches, longest;

    longest = 0;
    nmatches = 0;
    found = 0;
    for (c = cmdtab; (p = c->name) != NULL; c++) {
        for (q = name; *q == *p++; q++)
            if (*q == 0) /* exact match? */
                return (c);
        if (!*q) { /* the name was a prefix */
            if (q - name > longest) {
                longest = q - name;
                nmatches = 1;
                found = c;
            } else if (q - name == longest)
                nmatches++;
        }
    }
    if (nmatches > 1)
        return ((struct cmd *)-1);
    return (found);
}

/*
 * Slice a string up into argc/argv.
 */
void makeargv(void)
{
    register char *cp;
    register char **argp = margv;

    margc = 0;
    for (cp = line; *cp;) {
        while (isspace(*cp))
            cp++;
        if (*cp == '\0')
            break;
        *argp++ = cp;
        margc += 1;
        while (*cp != '\0' && !isspace(*cp))
            cp++;
        if (*cp == '\0')
            break;
        *cp++ = '\0';
    }
    *argp++ = 0;
}

void quit(int ign1, char *ign2[])
{
    (void)ign1;
    (void)ign2;
    exit(0);
}

/*
 * Help command.
 */
void help(int argc, char *argv[])
{
    register struct cmd *c;

    if (argc == 1) {
        printf("Commands may be abbreviated.  Commands are:\n\n");
        for (c = cmdtab; c->name; c++)
            printf("%-*s\t%s\n", HELPINDENT, c->name, c->help);
        return;
    }
    while (--argc > 0) {
        register char *arg;
        arg = *++argv;
        c = getcmd(arg);
        if (c == (struct cmd *)-1)
            printf("?Ambiguous help command %s\n", arg);
        else if (c == (struct cmd *)0)
            printf("?Invalid help command %s\n", arg);
        else
            printf("%s\n", c->help);
    }
}

void settrace(int ign1, char *ign2[])
{
    (void)ign1;
    (void)ign2;
    trace = !trace;
    printf("Packet tracing %s.\n", trace ? "on" : "off");
}

void setverbose(int ign1, char *ign2[])
{
    (void)ign1;
    (void)ign2;
    verbose = !verbose;
    printf("Verbose mode %s.\n", verbose ? "on" : "off");
}
