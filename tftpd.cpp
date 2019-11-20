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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

//
// async_tftpd_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
// to much! #include "asio.hpp"
//
// not yet! #include <asio/use_future.hpp>

#include <type_traits>

#define USE_SYNC_RECEIVE
#ifndef USE_SYNC_RECEIVE
#    include <asio/io_context.hpp>
#    include <asio/ip/udp.hpp>
#    include <asio/read.hpp>
#    include <asio/steady_timer.hpp>
#    include <asio/system_error.hpp>
#    include <asio/write.hpp>
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

#define TIMEOUT 5

struct formats;
// XXX static int sendfile(struct formats *pf);
// XXX static int recvfile(struct formats *pf);
static int validate_access(const char *filename, int mode);

// XXX static int peer;
static const int rexmtval = TIMEOUT;
static const int maxtimeout = 5 * TIMEOUT;

// XXX static char buf[PKTSIZE];
// XXX static char ackbuf[PKTSIZE];
// XXX static struct sockaddr_storage from;
// XXX static socklen_t fromlen;

static const char *default_dirs[] = {"/tmp/tftpboot", 0};
static const char *const *dirs = default_dirs;

static const bool suppress_naks = false;
static const bool secure_tftp = false;
static FILE *file;

struct formats
{
    const char *f_mode;
    // int (*f_validate)(const char *, int);
    // int (*f_send)(struct formats *);
    // int (*f_recv)(struct formats *);
    int f_convert;
} formats[] =
    { // XXX {"netascii", /* validate_access, sendfile, recvfile, */ 1},
        {"octet", /* validate_access, sendfile, recvfile, */ 0},
        {0, 0}};

/*
 * Handle initial connection protocol.
 */
int tftp(const std::vector<char> &rxbuffer)
{
    const char *cp;
    bool first = true;
    int ecode;
    struct formats *pf;
    const char *filename, *mode = NULL;

    struct tftphdr *tp = (struct tftphdr *)(rxbuffer.data());
    tp->th_opcode = ntohs(tp->th_opcode);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    filename = cp = static_cast<const char *>(tp->th_stuff);

    do {
        // TODO: for(const char& c: rxbuffer)
        while (cp < rxbuffer.data() + rxbuffer.size()) {
            if (*cp == '\0') {
                break;
            }
            cp++;
        }
        if (*cp != '\0') {
            syslog(LOG_NOTICE, "tftpd: missing filename\n");
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

    // NOLINTNEXTLINE
    for (pf = formats; pf->f_mode != nullptr; pf++) {
        if (l_mode == pf->f_mode) {
            break;
        }
    }
    if (pf->f_mode == nullptr) {
        syslog(LOG_NOTICE, "tftpd: wrong mode\n");
        return (EBADOP);
    }

    ecode = validate_access(filename, tp->th_opcode);
    if (ecode != 0) {
        /*
         * Avoid storms of naks to a RRQ broadcast for a relative
         * bootfile pathname from a diskless Sun.
         */
        if (suppress_naks && *filename != '/' && ecode == ENOTFOUND) {
            syslog(LOG_NOTICE, "tftpd: deny to asscess file: %s\n", filename);
            return 0; // OK
        }
        return (ecode);
    }

    if (tp->th_opcode == RRQ) {
        // NEVER! (*pf->f_send)(pf); // sendfile()
        syslog(LOG_NOTICE, "tftpd: only upload supported!\n");
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

    syslog(LOG_NOTICE, "tftpd: trying to get file: %s\n", filename);

    if (*filename != '/' || secure_tftp) {
        syslog(LOG_NOTICE, "tftpd: serving file from %s\n", dirs[0]);
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
        syslog(LOG_NOTICE, "tftpd: serving file %s\n", filename);
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
            syslog(LOG_WARNING, "tftpd: file not found %s\n", filename);
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
            syslog(LOG_WARNING, "tftpd: file has not S_IROTH set\n");
            return (EACCESS);
        }

    } else if (secure_tftp) {
        if ((stbuf.st_mode & S_IWOTH) == 0) {
            syslog(LOG_WARNING, "tftpd: file has not S_IWOTH set\n");
            return (EACCESS);
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
    return 0; // OK
}

#if 0
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
                return 0;   // OK
            }
            ap->th_opcode = ntohs((u_short)ap->th_opcode);
            ap->th_block = ntohs((u_short)ap->th_block);

            if (ap->th_opcode == ERROR) {
                return 0;   // OK
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

    return 0;   // OK
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
                    return 0;   // OK
                }
                dp->th_opcode = ntohs((u_short)dp->th_opcode);
                dp->th_block = ntohs((u_short)dp->th_block);
                if (dp->th_opcode == ERROR) {
                    return 0;   // OK
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
                    return (errno + 100);
                } else {
                    return (ENOSPACE);
                }
            }
        } while (size == SEGSIZE);

        write_behind(file, pf->f_convert);
    }

    ap->th_opcode = htons((u_short)ACK); /* send the "final" ack */
    ap->th_block = htons((u_short)(block));
    (void)sendto(peer, ackbuf, 4, 0, (struct sockaddr *)&from, fromlen);

    mysignal(SIGALRM, justquit); /* FIXME just quit on timeout */
    alarm(rexmtval);             // FIXME
    n = recv(peer, buf, sizeof(buf),
             0); /* normally times out and quits */
    alarm(0);
    if (n >= 4 &&                /* if read some data */
        dp->th_opcode == DATA && /* and got a data block */
        block == dp->th_block) { /* then my last ack was lost */
        (void)sendto(peer, ackbuf, 4, 0, (struct sockaddr *)&from,
                     fromlen); /* resend final ack */
    }

    return 0;   // OK
}
#endif

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

using asio::ip::udp;

//----------------------------------------------------------------------
#if 0

using asio::ip::tcp;

// A custom completion token that makes asynchronous operations behave as
// though they are blocking calls with a timeout.
struct close_after
{
  close_after(std::chrono::steady_clock::duration t, tcp::socket& s)
    : timeout_(t), socket_(s)
  {
  }

  // The maximum time to wait for an asynchronous operation to complete.
  std::chrono::steady_clock::duration timeout_;

  // The socket to be closed if the operation does not complete in time.
  tcp::socket& socket_;
};

namespace asio {

// The async_result template is specialised to allow the close_after token to
// be used with asynchronous operations that have a completion signature of
// void(error_code, T). Generalising this for all completion signature forms is
// left as an exercise for the reader.
template <typename T>
class async_result<close_after, void(std::error_code, T)>
{
public:
  // An asynchronous operation's initiating function automatically creates an
  // completion_handler_type object from the token. This function object is
  // then called on completion of the asynchronous operation.
  class completion_handler_type
  {
  public:
    completion_handler_type(const close_after& token)
      : token_(token)
    {
    }

    void operator()(const std::error_code& error, T t)
    {
      *error_ = error;
      *t_ = t;
    }

  private:
    friend class async_result;
    close_after token_;
    std::error_code* error_;
    T* t_;
  };

  // The async_result constructor associates the completion handler object with
  // the result of the initiating function.
  explicit async_result(completion_handler_type& h)
    : timeout_(h.token_.timeout_),
      socket_(h.token_.socket_)
  {
    h.error_ = &error_;
    h.t_ = &t_;
  }

  // The return_type typedef determines the result type of the asynchronous
  // operation's initiating function.
  typedef T return_type;

  // The get() function is used to obtain the result of the asynchronous
  // operation's initiating function. For the close_after completion token, we
  // use this function to run the io_context until the operation is complete.
  // see async_completion<CompletionToken, Signature> completion(token).result.get()
  return_type get()
  {
    asio::io_context& io_context = socket_.get_executor().context();

    // Restart the io_context, as it may have been left in the "stopped" state
    // by a previous operation.
    io_context.restart();

    // Block until the asynchronous operation has completed, or timed out. If
    // the pending asynchronous operation is a composed operation, the deadline
    // applies to the entire operation, rather than individual operations on
    // the socket.
    io_context.run_for(timeout_);

    // If the asynchronous operation completed successfully then the io_context
    // would have been stopped due to running out of work. If it was not
    // stopped, then the io_context::run_for call must have timed out and the
    // operation is still incomplete.
    if (!io_context.stopped())
    {
      // Close the socket to cancel the outstanding asynchronous operation.
      socket_.close();

      // Run the io_context again until the operation completes.
      io_context.run();
    }

    // If the operation failed, throw an exception. Otherwise return the result.
    return error_ ? throw std::system_error(error_) : t_;
  }

private:
  std::chrono::steady_clock::duration timeout_;
  tcp::socket& socket_;
  std::error_code error_;
  T t_;
};

} // namespace asio

#endif
//----------------------------------------------------------------------

class server
{
public:
    server(asio::io_context &io_context, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), port)),
          timer_(io_context, std::chrono::seconds(maxtimeout)),
          timeout_(rexmtval), io_context_(io_context)
    {
        start_timeout(maxtimeout); // max idle wait ...
        do_receive();
    }

private:
    void start_timeout(size_t seconds)
    {
        timer_.cancel();    // TODO: needed? CK
        timer_.expires_after(std::chrono::seconds(seconds));
        timer_.async_wait([this](const std::error_code & /*error*/) {
            syslog(LOG_WARNING, "tftpd: timeout!\n");
            timeout_ += rexmtval;
            // Cancel all asynchronous operations associated with the socket.
            if (socket_.is_open()) {
                socket_.cancel();

                if (timeout_ >= maxtimeout) {
                    syslog(LOG_ERR, "tftpd: maxtimeout!\n");
                    socket_.close();
                }
            }
        });
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
                    } else {
                        error = recvfile(formats);
                        if (error == 0) {
                            syslog(LOG_NOTICE, "well done!\n");
                            start_timeout(maxtimeout); // max idle wait ...
                            do_receive();
                        }
                    }
                }
            });

         // NOTE: we need to return! CK
    }

    /*
     * Send a nak packet (error message).  Error code passed in is one of the
     * standard TFTP codes, or a UNIX errno offset by 100.
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
        if (pe->e_code < 0) {
            pe->e_msg = strerror(error - 100);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            tp->th_code =
                htons((u_short)EUNDEF); /* set 'eundef(0)' errorcode */
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        strncpy(tp->th_msg, pe->e_msg, PKTSIZE - 5);
        length = strlen(pe->e_msg);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        tp->th_msg[length] = '\0'; // TODO: check this! CK
        length += 5;
        txbuf.resize(length);

        do_send(txbuf);
    }

    void do_send(const std::vector<char> &txdata)
    {
        socket_.async_send_to(
            asio::buffer(txdata), sender_endpoint_,
            [this](std::error_code ec, std::size_t /*bytes_sent*/) {
                if (ec) {
                    syslog(LOG_ERR, "do_send: %s\n", strerror(errno));
                }
            });
    }

    int recvfile(struct formats *pf)
    {
        struct tftphdr *ap = (struct tftphdr *)ackbuf_; /* ptr to ack buffer */
        volatile u_int16_t block = 0;

        {
            int size;
            int rxlen = 0;
            const std::shared_ptr<FILE> guard(file, std::fclose);

            struct tftphdr *dp = w_init(); // get first data buffer ptr
            do {
                ap->th_opcode = htons((u_short)ACK);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                ap->th_block = htons((u_short)block);
                block++;

            send_ack:
                if (socket_.send_to(asio::buffer(ackbuf_, TFTP_HEADER),
                                    sender_endpoint_) != TFTP_HEADER) {
                    syslog(LOG_ERR, "tftpd: write ack: %s\n", strerror(errno));
                    return -1;
                }

                int txlen = write_behind(
                    file, pf->f_convert); // output the current buffer if needed
                if (txlen < 0) {
                    syslog(LOG_ERR, "tftpd: write file: %s\n", strerror(errno));
                    return -1;
                }

                for (;;) {

#ifndef USE_SYNC_RECEIVE
                    // Run an asynchronous read operation with a timeout.
                    timer_.cancel();
                    socket_.async_receive_from(
                        asio::buffer(dp, PKTSIZE), sender_endpoint_,
                        [this, &rxlen](std::error_code ec,
                                       std::size_t bytes_recvd) {
                            if (ec) {
                                rxlen = -1;
                            } else {
                                rxlen = bytes_recvd;
                            }
                        });
                    run(std::chrono::seconds(timeout_));
#else
                    start_timeout(timeout_);
                    rxlen = socket_.receive_from(asio::buffer(dp, PKTSIZE),
                                                 sender_endpoint_);
#endif

                    if (rxlen < 0) { /* really? */
                        syslog(LOG_ERR, "tftpd: read data: %s\n",
                               strerror(errno));
                        return -1;
                    }

                    dp->th_opcode = ntohs((u_short)dp->th_opcode);
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                    dp->th_block = ntohs((u_short)dp->th_block);
                    if (dp->th_opcode == ERROR) {
                        return -1;
                    }
                    if (dp->th_opcode == DATA) {
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                        if (dp->th_block == block) {
                            break; /* normal */
                        }

                        synchnet(); // Re-synchronize with the other side

                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                        if (dp->th_block == (block - 1)) {
                            // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                            goto send_ack; /* rexmit */
                        }
                    }
                }

                int length = rxlen - TFTP_HEADER;
                size =
                    writeit(file, &dp, length,
                            pf->f_convert); // write the current data segement
                if (size != length) {       /* ahem */
                    if (size < 0) {
                        return (errno + 100);
                    } else {
                        return (ENOSPACE);
                    }
                }
            } while (size == SEGSIZE);

            write_behind(file, pf->f_convert); // write the final data segment
        }

        ap->th_opcode = htons((u_short)ACK); /* send the "final" ack */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        ap->th_block = htons((u_short)(block));
        (void)socket_.send_to(asio::buffer(ackbuf_, TFTP_HEADER),
                              sender_endpoint_);

        start_timeout(rexmtval);
        timeout_ = maxtimeout; // NOTE: Normally times out and quits

        // Run an asynchronous read operation with a timeout.
        socket_.async_receive_from(
            asio::buffer(rxbuf_, sizeof(rxbuf_)), sender_endpoint_,
            [this, &block](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec) {
                    struct tftphdr *dp = (struct tftphdr *)this->rxbuf_;
                    if ((bytes_recvd >= TFTP_HEADER) && /* if read some data */
                        (dp->th_opcode == DATA) && /* and got a data block */
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                        (block ==
                         dp->th_block)) { /* then my last ack was lost */
                        /* resend final ack */
                        (void)socket_.send_to(
                            asio::buffer(this->ackbuf_, TFTP_HEADER),
                            this->sender_endpoint_);
                    }
                }
            });

        return 0; // OK
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
    void run(std::chrono::steady_clock::duration timeout)
    {
        // Restart the io_context, as it may have been left in the "stopped"
        // state by a previous operation.
        io_context_.restart();

        // Block until the asynchronous operation has completed, or timed out.
        // If the pending asynchronous operation is a composed operation, the
        // deadline applies to the entire operation, rather than individual
        // operations on the socket.
        io_context_.run_for(timeout);

        // If the asynchronous operation completed successfully then the
        // io_context would have been stopped due to running out of work. If it
        // was not stopped, then the io_context::run_for call must have timed
        // out.
        if (!io_context_.stopped()) {
            // Cancel the outstanding asynchronous operation.
            socket_.cancel();

            // Run the io_context again until the operation completes.
            io_context_.run();
        }
    }

    udp::socket socket_;
    asio::steady_timer timer_;
    udp::endpoint sender_endpoint_;
    enum
    {
        max_length = PKTSIZE
    };
    std::vector<char> rxdata_;
    int timeout_;
    char ackbuf_[PKTSIZE] = {};
    char rxbuf_[PKTSIZE] = {};
    asio::io_context &io_context_;
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
