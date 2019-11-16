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
 * From: @(#)tftpd.c	5.13 (Berkeley) 2/26/91
 */
// char rcsid[] = "$Id: tftpd.c,v 1.20 2000/07/29 18:37:21 dholland Exp $";

/*
 * Trivial file transfer protocol server.
 *
 * This version includes many modifications by Jim Guyton <guyton@rand-unix>
 */
#include "tftp/tftpsubs.h"

#include <arpa/tftp.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

#define TIMEOUT 5

// TODO extern interface:
// XXX int tftp(struct tftphdr *tp, size_t size);

struct formats;
static int sendfile(struct formats *pf);
static int recvfile(struct formats *pf);
static int validate_access(const char *filename, int mode);

static int peer;
static int rexmtval = TIMEOUT;
static int maxtimeout = 5 * TIMEOUT;

static char buf[PKTSIZE];
static char ackbuf[PKTSIZE];
static struct sockaddr_storage from;
static socklen_t fromlen;

static const char *default_dirs[] = {"/tmp/tftpboot", 0};
static const char *const *dirs = default_dirs;

static bool suppress_naks = false;
static bool secure_tftp = false;
static FILE *file;

struct formats
{
    const char *f_mode;
    int (*f_validate)(const char *, int);
    int (*f_send)(struct formats *);
    int (*f_recv)(struct formats *);
    int f_convert;
} formats[] = {{"netascii", validate_access, sendfile, recvfile, 1},
               {"octet", validate_access, sendfile, recvfile, 0},
               {0, 0, 0, 0, 0}};

/*
 * Handle initial connection protocol.
 */
// XXX int tftp(struct tftphdr *tp, size_t size)
int tftp(const std::vector<char> &buf)
{
    char *cp;
    bool first = true;
    int ecode;
    struct formats *pf;
    char *filename, *mode = NULL;

    struct tftphdr *tp = (struct tftphdr *)buf.data();
    filename = cp = tp->th_stuff;
    // XXX assert(cp == buf.data());
    do {
        // XXX for(const char& c: buf)
        while (cp < buf.data() + buf.size()) {
            if (*cp == '\0') {
                break;
            }
            cp++;
        }
        if (*cp != '\0') {
            syslog(LOG_NOTICE, "tftpd: missing filename\n");
            // nak(EBADOP);
            return (EBADOP);
        }
        if (first) {
            mode = ++cp;
            first = false;
            continue;
        }
        break;
    } while (true);

    for (cp = mode; *cp != 0; cp++) {
        if (isupper(*cp) != 0) {
            *cp = tolower(*cp);
        }
    }
    for (pf = formats; pf->f_mode != nullptr; pf++) {
        if (strcmp(pf->f_mode, mode) == 0) {
            break;
        }
    }
    if (pf->f_mode == 0) {
        syslog(LOG_NOTICE, "tftpd: missing mode\n");
        // nak(EBADOP);
        return (EBADOP);
    }

    ecode = (*pf->f_validate)(filename, tp->th_opcode); // validate_access()
    if (ecode != 0) {
        /*
         * Avoid storms of naks to a RRQ broadcast for a relative
         * bootfile pathname from a diskless Sun.
         */
        if (suppress_naks && *filename != '/' && ecode == ENOTFOUND) {
            syslog(LOG_NOTICE, "tftpd: deny to asscess file: %s\n", filename);
            return (0);
        }
        // nak(ecode);
        return (ecode);
    }

    if (tp->th_opcode == WRQ) {
        syslog(LOG_NOTICE, "tftpd: not yet implemented write request: %s\n",
               filename);
        // NO! (*pf->f_recv)(pf);    // recvfile()
    } else {
        // NEVER! (*pf->f_send)(pf); // sendfile()
        return (EBADOP);
    }
    return (0);
}

/*
 * Validate file access.  Since we
 * have no uid or gid, for now require
 * file to exist and be publicly
 * readable/writable.
 * If we were invoked with arguments
 * from inetd then the file must also be
 * in one of the given directory prefixes.
 * Note also, full path name must be
 * given as we have no login directory.
 */
static int validate_access(const char *filename, int mode)
{
    struct stat stbuf = {};
    int fd;
    const char *cp;
    const char *const *dirp;

    syslog(LOG_NOTICE, "tftpd: trying to get file: %s\n", filename);

    if (*filename != '/' || secure_tftp) {
        syslog(LOG_NOTICE, "tftpd: serving file from %s\n", dirs[0]);
        /*chdir(dirs[0]);*/
        if (chdir(dirs[0]) < 0) {
            syslog(LOG_WARNING, "tftpd: chdir: %m\n");
            return (EACCESS);
        }
        while (*filename == '/') {
            filename++;
        }
    } else {
        for (dirp = dirs; *dirp != nullptr; dirp++) {
            if (strncmp(filename, *dirp, strlen(*dirp)) == 0) {
                break;
            }
        }
        if (*dirp == 0 && dirp != dirs) {
            syslog(LOG_WARNING, "tftpd: invalid root dir!\n");
            return (EACCESS);
        }
    }
    /*
     * prevent tricksters from getting around the directory restrictions
     */
    if (strncmp(filename, "../", 3) == 0) {
        syslog(LOG_WARNING, "tftpd: Blocked illegal request for %s\n",
               filename);
        return EACCESS;
    }

    for (cp = filename + 1; *cp != 0; cp++) {
        if (*cp == '.' && strncmp(cp - 1, "/../", 4) == 0) {
            syslog(LOG_WARNING, "tftpd: Blocked illegal request for %s\n",
                   filename);
            return (EACCESS);
        }
    }

    if (stat(filename, &stbuf) < 0) {
        if (mode == RRQ) {
            syslog(LOG_WARNING, "tftpd: file not found %s\n", filename);
            return (errno == ENOENT ? ENOTFOUND : EACCESS);
        }

#if 0
	/*
	 * The idea is that symlinks are dangerous. However, a symlink
	 * in the tftp area has to have been put there by root, and it's
	 * not part of the philosophy of Unix to keep root from shooting
	 * itself in the foot if it tries to. So basically we assume if
	 * there are symlinks they're there on purpose and not pointing
	 * to /etc/passwd or /tmp or other dangerous places.
	 *
	 * Note if this gets turned on the stat above needs to be made
	 * an lstat, or the check is useless.
	 */
	/* symlinks prohibited */
	if (S_ISLNK(stbuf.st_mode)) {
		return (EACCESS);
	}
#endif

        if (mode == RRQ) {
            if ((stbuf.st_mode & S_IROTH) == 0) {
                syslog(LOG_WARNING, "tftpd: file has not S_IROTH set\n");
                return (EACCESS);
            }

#if 0
        } else {
            if ((stbuf.st_mode & S_IWOTH) == 0) {
                syslog(LOG_WARNING, "tftpd: file has not S_IWOTH set\n");
                return (EACCESS);
            }
#endif

        }
    }

    fd = open(filename,
              (mode == RRQ ? O_RDONLY : (O_WRONLY | O_TRUNC | O_CREAT)), 0666);
    if (fd < 0) {
        return (errno + 100);
    }
    file = fdopen(fd, (mode == RRQ) ? "r" : "w");
    if (file == NULL) {
        return errno + 100;
    }
    syslog(LOG_NOTICE, "tftpd: successfully open file\n");
    return (0);
}

static int timeout;
static sigjmp_buf timeoutbuf;
static void timer(int signum)
{
    (void)signum;

    timeout += rexmtval;
    if (timeout >= maxtimeout) {
        exit(EXIT_FAILURE);
    }
    siglongjmp(timeoutbuf, 1);
}

/*
 * Send the requested file.
 */
static int sendfile(struct formats *pf)
{
    struct tftphdr *dp;
    struct tftphdr *ap; /* ack packet */
    volatile u_int16_t block = 1;
    int size, n;

    const std::shared_ptr<FILE> guard(file, std::fclose);
    mysignal(SIGALRM, timer); // FIXME
    dp = r_init();
    ap = (struct tftphdr *)ackbuf;
    do {
        size = readit(file, &dp, pf->f_convert);
        if (size < 0) {
            // nak(errno + 100);
            return (errno + 100);
        }
        dp->th_opcode = htons((u_short)DATA);
        dp->th_block = htons((u_short)block);
        timeout = 0;
        (void)sigsetjmp(timeoutbuf, 1); // FIXME

    send_data:
        if (sendto(peer, dp, size + 4, 0, (struct sockaddr *)&from, fromlen) !=
            size + 4) {
            syslog(LOG_ERR, "tftpd: data write: %m\n");
            break;
        }

        read_ahead(file, pf->f_convert);
        for (;;) {
            alarm(rexmtval); /* read the ack */ // FIXME
            n = recv(peer, ackbuf, sizeof(ackbuf), 0);
            alarm(0);
            if (n < 0) {
                syslog(LOG_ERR, "tftpd: read ack: %m\n");
                return 0;
            }
            ap->th_opcode = ntohs((u_short)ap->th_opcode);
            ap->th_block = ntohs((u_short)ap->th_block);

            if (ap->th_opcode == ERROR) {
                return 0;
            }

            if (ap->th_opcode == ACK) {
                if (ap->th_block == block) {
                    break;
                }
                /* Re-synchronize with the other side */
                (void)synchnet(peer, false);
                if (ap->th_block == (block - 1)) {
                    // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                    goto send_data;
                }
            }
        }
        block++;
    } while (size == SEGSIZE);
    // XXX abort: (void)fclose(file);

    return 0;
}

static void justquit(int signum)
{
    (void)signum;
    exit(EXIT_SUCCESS);
}

/*
 * Receive a file.
 */
static int recvfile(struct formats *pf)
{
    struct tftphdr *dp;
    struct tftphdr *ap; /* ack buffer */
    volatile u_int16_t block = 0;
    int n;

    {
        int size;
        const std::shared_ptr<FILE> guard(file, std::fclose);
        mysignal(SIGALRM, timer); // FIXME
        dp = w_init();
        ap = (struct tftphdr *)ackbuf;
        do {
            timeout = 0;
            ap->th_opcode = htons((u_short)ACK);
            ap->th_block = htons((u_short)block);
            block++;
            (void)sigsetjmp(timeoutbuf, 1); // FIXME
        send_ack:
            if (sendto(peer, ackbuf, 4, 0, (struct sockaddr *)&from, fromlen) !=
                4) {
                syslog(LOG_ERR, "tftpd: write ack: %m\n");
                break;
            }

            write_behind(file, pf->f_convert);
            for (;;) {
                alarm(rexmtval); // FIXME
                n = recv(peer, dp, PKTSIZE, 0);
                alarm(0);
                if (n < 0) { /* really? */
                    syslog(LOG_ERR, "tftpd: read data: %m\n");
                    return 0;
                }
                dp->th_opcode = ntohs((u_short)dp->th_opcode);
                dp->th_block = ntohs((u_short)dp->th_block);
                if (dp->th_opcode == ERROR) {
                    return 0;
                }
                if (dp->th_opcode == DATA) {
                    if (dp->th_block == block) {
                        break; /* normal */
                    }
                    /* Re-synchronize with the other side */
                    (void)synchnet(peer, false);
                    if (dp->th_block == (block - 1)) {
                        // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                        goto send_ack; /* rexmit */
                    }
                }
            }

            /*  size = write(file, dp->th_data, n - 4); */
            size = writeit(file, &dp, n - 4, pf->f_convert);
            if (size != (n - 4)) { /* ahem */
                if (size < 0) {
                    // nak(errno + 100);
                    return (errno + 100);
                } else {
                    // nak(ENOSPACE);
                    return (ENOSPACE);
                }
            }
        } while (size == SEGSIZE);

        write_behind(file, pf->f_convert);
        // XXX (void)fclose(file); /* close data file */
    }

    ap->th_opcode = htons((u_short)ACK); /* send the "final" ack */
    ap->th_block = htons((u_short)(block));
    (void)sendto(peer, ackbuf, 4, 0, (struct sockaddr *)&from, fromlen);

    mysignal(SIGALRM, justquit); /* FIXME just quit on timeout */
    alarm(rexmtval);             // FIXME
    n = recv(peer, buf, sizeof(buf),
             0); /* TODO(buf used!) normally times out and quits */
    alarm(0);
    if (n >= 4 &&                /* if read some data */
        dp->th_opcode == DATA && /* and got a data block */
        block == dp->th_block) { /* then my last ack was lost */
        (void)sendto(peer, ackbuf, 4, 0, (struct sockaddr *)&from,
                     fromlen); /* resend final ack */
    }

    return 0;
}

struct errmsg
{
    int e_code;
    const char *e_msg;
} errmsgs[] = {{EUNDEF, "Undefined error code"},
               {ENOTFOUND, "File not found"},
               {EACCESS, "Access violation"},
               {ENOSPACE, "Disk full or allocation exceeded"},
               {EBADOP, "Illegal TFTP operation"},
               {EBADID, "Unknown transfer ID"},
               {EEXISTS, "File already exists"},
               {ENOUSER, "No such user"},
               {-1, 0}};

//
// async_tftpd_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "asio.hpp"

#include <cstdlib>
#include <iostream>

using asio::ip::udp;

class server
{
public:
    server(asio::io_context &io_context, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), port))
    {
        do_receive();
    }

    /*
     * Send a nak packet (error message).
     * Error code passed in is one of the standard TFTP codes,
     * or a UNIX errno offset by 100.
     */
    void send_nak(int error)
    {
        struct tftphdr *tp;
        int length;
        struct errmsg *pe;
        std::vector<char> txbuf;
        txbuf.resize(PKTSIZE);

        tp = (struct tftphdr *)txbuf.data();
        tp->th_opcode = htons((u_short)ERROR);
        tp->th_code = htons((u_short)error);
        for (pe = errmsgs; pe->e_code >= 0; pe++) {
            if (pe->e_code == error) {
                break;
            }
        }
        if (pe->e_code < 0) {
            pe->e_msg = strerror(error - 100);
            tp->th_code = htons((u_short)EUNDEF); /* set 'undef(0)' errorcode */
        }
        strcpy(tp->th_msg, pe->e_msg);
        length = strlen(pe->e_msg);
        tp->th_msg[length] = '\0';
        length += 5;
        txbuf.resize(length);

        do_send(txbuf);
    }

    void do_receive()
    {
        rxdata_.resize(max_length);
        socket_.async_receive_from(
            asio::buffer(rxdata_, max_length), sender_endpoint_,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    rxdata_.resize(bytes_recvd);
                    int error = tftp(rxdata_);
                    if (error != 0) {
                        send_nak(error);
                        exit(EXIT_FAILURE);
                    }
                    exit(EXIT_SUCCESS);
                } else {
                    do_receive();
                }
            });
    }

    void do_send(const std::vector<char> &txdata)
    {
        socket_.async_send_to(
            asio::buffer(txdata), sender_endpoint_,
            [this](std::error_code ec, std::size_t /*bytes_sent*/) {
                if (ec) {
                    syslog(LOG_ERR, "do_send: %m\n");
                }
                // XXX do_receive();
            });
    }

private:
    udp::socket socket_;
    udp::endpoint sender_endpoint_;
    enum
    {
        max_length = PKTSIZE
    };
    std::vector<char> rxdata_;
};

int main(int argc, char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: tftpd <port>\n";
            return 0;
        }

        asio::io_context io_context;
        server s(io_context, std::strtol(argv[1], nullptr, 10));
        io_context.run();
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
