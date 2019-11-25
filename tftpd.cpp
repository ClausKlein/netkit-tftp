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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
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
 * From: @(#)tftpd.c    5.13 (Berkeley) 2/26/91
 */
// char rcsid[] = "$Id: tftpd.c,v 1.20 2000/07/29 18:37:21 dholland Exp $";

/*
 * Trivial file transfer protocol server.
 *
 * TFTP's design was influenced from the earlier protocol EFTP,
 * which was part of the PUP protocol suite.
 * TFTP was first defined in 1980 by IEN 133.
 * In June 1981 The TFTP Protocol (Revision 2) was published as RFC 783 and
 * later updated!
 *
 * In July 1992 by RFC 1350 which fixed among other things the Sorcerer's
 * Apprentice Syndrome.
 *
 * This version includes many modifications by Jim Guyton <guyton@rand-unix>
 */
#include "tftp/tftpsubs.h"

#include <arpa/tftp.h>
#include <syslog.h>
#include <unistd.h>

//
// async_tftpd_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//

#include <type_traits>

#undef USE_SYNC_RECEIVE
#ifndef USE_SYNC_RECEIVE
// NO! to much! #include "asio.hpp"
#    include <asio/io_context.hpp>
#    include <asio/ip/udp.hpp>
#    include <asio/read.hpp>
#    include <asio/steady_timer.hpp>
#    include <asio/system_error.hpp>
#    include <asio/write.hpp>
// not yet! #include <asio/use_future.hpp>
#else
#    include <asio/ts/buffer.hpp>
#    include <asio/ts/internet.hpp>
#endif

#include <boost/algorithm/string/case_conv.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static int validate_access(const char *filename, int mode);

constexpr int ERRNO_OFFSET{100};
constexpr int TIMEOUT{3};
constexpr int rexmtval{TIMEOUT};
constexpr int maxtimeout{5 * TIMEOUT};

static const char *default_dirs[] = {"/tmp/tftpboot", 0};
static const char *const *dirs = default_dirs;

constexpr bool suppress_naks{false};
constexpr bool secure_tftp{false};

static FILE *file; // TODO: prevent static variables! CK

struct formats
{
    const char *f_mode;
    // int (*f_validate)(const char *, int);
    // int (*f_send)(struct formats *);
    // int (*f_recv)(struct formats *);
    bool f_convert;
} formats[] =
    { // XXX {"netascii", /* validate_access, sendfile, recvfile, */ true},
        {"octet", /* validate_access, sendfile, recvfile, */ false},
        {0, false}};

/*
 * Handle initial connection protocol.
 */
int tftp(const std::vector<char> &rxbuffer)
{
    syslog(LOG_NOTICE, "%s(%lu)\n", __FUNCTION__, rxbuffer.size());

    struct tftphdr *tp = (struct tftphdr *)(rxbuffer.data());
    tp->th_opcode = ntohs(tp->th_opcode);
    if ((tp->th_opcode != RRQ) && (tp->th_opcode != WRQ)) {
        syslog(LOG_ERR, "tftpd: invalid state!\n");
        return (EBADID);
    }

    const char *cp;
    const char *filename, *mode = NULL;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    filename = cp = static_cast<const char *>(tp->th_stuff);

    bool first = true;
    do {
        // TODO: for(const char& c: rxbuffer)
        while (cp < rxbuffer.data() + rxbuffer.size()) {
            if (*cp == '\0') {
                break;
            }
            cp++;
        }
        if (*cp != '\0') {
            syslog(LOG_WARNING, "tftpd: missing filename\n");
            return (EBADOP);
        }
        if (first) {
            mode = ++cp;
            first = false;
            continue;
        }
        break;
    } while (true);

    std::string l_mode(mode);
    boost::to_lower(l_mode);

    struct formats *pf;
    // NOLINTNEXTLINE
    for (pf = formats; pf->f_mode != nullptr; pf++) {
        if (l_mode == pf->f_mode) {
            break;
        }
    }
    if (pf->f_mode == nullptr) {
        syslog(LOG_WARNING, "tftpd: wrong mode\n");
        return (EBADOP);
    }

    int ecode = validate_access(filename, tp->th_opcode);
    if (ecode != 0) {
        /*
         * Avoid storms of naks to a RRQ broadcast for a relative
         * bootfile pathname from a diskless Sun.
         */
        if (suppress_naks && *filename != '/' && ecode == ENOTFOUND) {
            syslog(LOG_WARNING, "tftpd: deny to asscess file: %s\n", filename);
            return 0; // OK
        }
        return (ecode);
    }

    if (tp->th_opcode == RRQ) {
        // NEVER! (*pf->f_send)(pf); // sendfile()
        syslog(LOG_WARNING, "tftpd: only upload supported!\n");
        return (EBADOP);
    }

    return 0; // OK
}

/*
 * Validate file access.
 *
 * Since we have no uid or gid, for now require file to exist and be
 * publicly readable/writable.  If we were invoked with arguments from inetd
 * then the file must also be in one of the given directory prefixes.
 * Note also, full path name must be given as we have no login directory.
 */
static int validate_access(const char *filename, int mode)
{
    struct stat stbuf = {};
    int fd;
    const char *cp;
    const char *const *dirp;

    syslog(LOG_NOTICE, "tftpd: Validate access to file: %s\n", filename);

    // TODO: prevent strncmp usage! CK
    if (*filename != '/' || secure_tftp) {
        syslog(LOG_NOTICE, "tftpd: Check file access at %s\n", dirs[0]);
        /*chdir(dirs[0]);*/
        if (chdir(dirs[0]) < 0) {
            syslog(LOG_WARNING, "tftpd: chdir: %s\n", strerror(errno));
            return (EACCESS);
        }
        while (*filename == '/') {
            filename++;
        }
    } else {
        // NOLINTNEXTLINE
        for (dirp = dirs; *dirp != 0; dirp++) {
            if (strncmp(filename, *dirp, strlen(*dirp)) == 0) {
                break;
            }
        }
        if (*dirp == 0 && dirp != dirs) {
            syslog(LOG_WARNING, "tftpd: invalid root dir!\n");
            return (EACCESS);
        }
        syslog(LOG_NOTICE, "tftpd: Check access to file %s\n", filename);
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
        // stat error, no such file or no read access
        if (mode == RRQ || secure_tftp) {
            syslog(LOG_WARNING, "tftpd: File not found %s\n", filename);
            return (errno == ENOENT ? ENOTFOUND : EACCESS);
        }
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
            syslog(LOG_WARNING, "tftpd: File has not S_IROTH set\n");
            return (EACCESS);
        }

    } else if (secure_tftp) {
        if ((stbuf.st_mode & S_IWOTH) == 0) {
            syslog(LOG_WARNING, "tftpd: File has not S_IWOTH set\n");
            return (EACCESS);
        }
    }

    fd = open(filename,
              (mode == RRQ ? O_RDONLY : (O_WRONLY | O_TRUNC | O_CREAT)), 0666);
    if (fd < 0) {
        return (errno + ERRNO_OFFSET);
    }
    file = fdopen(fd, (mode == RRQ) ? "r" : "w");
    if (file == NULL) {
        return errno + ERRNO_OFFSET;
    }

    syslog(LOG_NOTICE, "tftpd: successfully open file\n");
    return 0; // OK
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

//----------------------------------------------------------------------
using asio::ip::udp;
//----------------------------------------------------------------------

class server
{
public:
    server(asio::io_context &io_context, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), port)),
          timer_(io_context), timeout_(rexmtval)
    {
        start_timeout(maxtimeout); // max idle wait ...
        do_receive();
    }

protected:
    void start_timeout(size_t seconds)
    {
        syslog(LOG_NOTICE, "%s(%lu)\n", __FUNCTION__, seconds);
        timer_.cancel();
        timer_.expires_after(std::chrono::seconds(seconds));
        timer_.async_wait([this](const std::error_code &error) {
            if (!error) {
                syslog(LOG_WARNING, "tftpd: timeout!\n");
                timeout_ += rexmtval;
                // Cancel all asynchronous operations associated with the
                // socket.
                if (socket_.is_open()) {
                    socket_.cancel();
                    if (timeout_ >= maxtimeout) {
                        syslog(LOG_ERR, "tftpd: maxtimeout!\n");
                        socket_.close();
                    }
                }
            }
        });
    }

    void do_receive()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        rxdata_.resize(max_length);
        socket_.async_receive_from(
            asio::buffer(rxdata_, max_length), senderEndpoint_,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    rxdata_.resize(bytes_recvd);
                    int error = tftp(rxdata_);
                    if (error != 0) {
                        send_nak(error);
                    } else {
                        clientEndpoint_ = senderEndpoint_;
                        socket_.close();
                        socket_.open(udp::v4());
                        socket_.bind(udp::endpoint(udp::v4(), 0));
                        this->guard = start_recvfile();
                    }
                }
            });

        // NOTE: we need to return! CK
    }

    /*
     * Send a nak packet (error message).  Error code passed in is one of the
     * standard TFTP codes, or a UNIX errno offset by ERRNO_OFFSET(100).
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        tp->th_code = htons((u_short)error);
        for (pe = errmsgs; pe->e_code >= 0; pe++) {
            if (pe->e_code == error) {
                break;
            }
        }

        // TODO: prevent strncpy usage, use std::string! CK
        if (pe->e_code < 0) {
            pe->e_msg = strerror(error - ERRNO_OFFSET);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            tp->th_code =
                htons((u_short)EUNDEF); /* set 'eundef(0)' errorcode */
        }
        size_t extra = TFTP_HEADER + 1;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        strncpy(tp->th_msg, pe->e_msg, PKTSIZE - extra);
        length = strlen(pe->e_msg);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        tp->th_msg[length] = '\0'; // TODO: check if needed! CK
        length += extra;
        txbuf.resize(length);

        do_send_nak(txbuf);
    }

    void do_send_nak(const std::vector<char> &txdata)
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        socket_.async_send_to(
            asio::buffer(txdata), senderEndpoint_,
            [this](std::error_code ec, std::size_t /*bytes_sent*/) {
                if (ec) {
                    syslog(LOG_ERR, "do_send_nak: %s\n", ec.message().c_str());
                }
                // TODO: now we have no timer running, the run loop terminates ...
                timer_.cancel();
            });
    }

    std::shared_ptr<FILE> start_recvfile()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        std::shared_ptr<FILE> guard(file, std::fclose);
        block = 0;
        dp = w_init(); // get first data buffer ptr
        send_ack();

        return guard;
    }

    void send_ack()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        struct tftphdr *ap = (struct tftphdr *)ackbuf_; /* ptr to ack buffer */
        ap->th_opcode = htons((u_short)ACK);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        ap->th_block = htons((u_short)block);
        block++;
        send_ackbuf();
    }

    void send_ackbuf()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        // output the current buffer if needed
        int txlen = write_behind(file, false);
        if (txlen < 0) {
            syslog(LOG_ERR, "tftpd: write file: %s\n", strerror(errno));
            exit(EXIT_FAILURE); // TODO: throw ...
        }

        socket_.async_send_to(
            asio::buffer(ackbuf_, TFTP_HEADER), clientEndpoint_,
            [this](std::error_code ec, std::size_t /*bytes_sent*/) {
                if (ec) {
                    syslog(LOG_ERR, "tftpd: send_ackbuf: %s\n",
                           ec.message().c_str());
                } else {
                    receive_block();
                }
            });
    }

    void receive_block()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        // Run an asynchronous read operation with a timeout.
        start_timeout(timeout_);
        socket_.async_receive_from(
            asio::buffer(dp, PKTSIZE), senderEndpoint_,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (ec) {
                    syslog(LOG_ERR, "tftpd: read data: %s\n",
                           ec.message().c_str());
                } else {
                    int err = check_block(bytes_recvd);
                    if (err) {
                        syslog(LOG_ERR, "tftpd: check data: %s\n",
                               strerror(err));
                        exit(EXIT_FAILURE); // TODO: throw ...
                    }
                }
            });
    }

    int check_block(int rxlen)
    {
        syslog(LOG_NOTICE, "%s(%d)\n", __FUNCTION__, block);
        if (senderEndpoint_ != clientEndpoint_)
        {
            syslog(LOG_ERR, "tftpd: invalid ID!\n");
            send_nak(EBADID);
            return 0;   // OK
        }

        do {
            dp->th_opcode = ntohs((u_short)dp->th_opcode);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            dp->th_block = ntohs((u_short)dp->th_block);
            if (dp->th_opcode == ERROR) {
                syslog(LOG_ERR, "tftpd: NAK received!\n");
                return ERRNO_OFFSET;
            }

            if (dp->th_opcode == DATA) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                if (dp->th_block == block) {
                    break; /* normal */
                }

                // ==============================================
                synchnet(); // Re-synchronize with the other side
                // ==============================================

                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                if (dp->th_block == (block - 1)) {
                    send_ackbuf(); /* rexmit => send last ack buf again */
                    return 0;      // OK
                }
            } else {
                syslog(LOG_ERR, "tftpd: invalid state!\n");
                return EBADID;
            }
        } while (false);

        // ===============================
        // write the current data segement
        // ===============================
        int length = rxlen - TFTP_HEADER;
        int size = writeit(file, &dp, length, false);
        if (size != length) { /* ahem */
            int error = ENOSPACE;
            if (size < 0) {
                error = (errno + ERRNO_OFFSET);
            }
            send_nak(error);
            return (error);
        }

        if (size == SEGSIZE) {
            send_ack();
            return 0; // OK
        }

        // =======================================================
        write_behind(file, false); // write the final data segment
        // =======================================================

        send_last_ack();

        return 0; // OK
    }

    void send_last_ack()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        struct tftphdr *ap = (struct tftphdr *)ackbuf_; /* ptr to ack buffer */
        ap->th_opcode = htons((u_short)ACK); /* send the "final" ack */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        ap->th_block = htons((u_short)(block));
        (void)socket_.send_to(asio::buffer(ackbuf_, TFTP_HEADER),
                              clientEndpoint_);

        start_timeout(rexmtval);
        timeout_ = maxtimeout; // NOTE: Normally times out and quits

        // Run an asynchronous read operation with a timeout.
        socket_.async_receive_from(
            asio::buffer(rxbuf_, sizeof(rxbuf_)), senderEndpoint_,
            [this](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec) {
                    struct tftphdr *dp = (struct tftphdr *)this->rxbuf_;
                    if ((bytes_recvd >= TFTP_HEADER) && /* if read some data */
                        (dp->th_opcode == DATA) && /* and got a data block */
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                        (this->block == dp->th_block)) {
                        /* then my last ack was lost */
                        /* resend final ack */
                        (void)socket_.send_to(
                            asio::buffer(this->ackbuf_, TFTP_HEADER),
                            this->clientEndpoint_);
                    }
                }
                timer_.cancel();
            });
    }

    /* When an error has occurred, it is possible that the two sides
     * are out of synch.  Ie: that what I think is the other side's
     * response to packet N is really their response to packet N-1.
     *
     * So, to try to prevent that, we flush all the input queued up
     * for us on the network connection on our host.
     *
     * We return the number of packets we flushed (mostly for reporting
     * when trace is active).
     */
    void synchnet()
    {
        syslog(LOG_NOTICE, "%s\n", __FUNCTION__);
        int j = 0;
        struct sockaddr_storage from;
        socklen_t fromlen;
        int s = socket_.native_handle();

        while (true) {
            std::size_t i = socket_.available();
            if (i != 0) {
                j++;
                fromlen = sizeof(from);
                (void)recvfrom(s, rxbuf_, sizeof(rxbuf_), 0,
                               (struct sockaddr *)&from, &fromlen);
            } else {
                if (j != 0) {
                    syslog(LOG_WARNING, "tftpd: discarded %d packets\n", j);
                }
                return;
            }
        }
    }

private:
    udp::socket socket_;
    asio::steady_timer timer_;
    udp::endpoint senderEndpoint_;
    udp::endpoint clientEndpoint_;
    enum
    {
        max_length = PKTSIZE
    };
    std::vector<char> rxdata_;
    int timeout_;
    char ackbuf_[PKTSIZE] = {};
    char rxbuf_[PKTSIZE] = {};

    struct tftphdr *dp = {nullptr};
    std::shared_ptr<FILE> guard;
    volatile u_int16_t block = {0};
};

int main(int argc, char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: tftpd <port>\n";
            return 0; // OK
        }

        asio::io_context io_context;
        server s(io_context, std::strtol(argv[1], nullptr, 10));

        io_context.run();
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
