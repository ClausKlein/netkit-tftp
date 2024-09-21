#pragma once

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
 * This version is based on code found in tftpd/ and tftp/ subdirs (CK)
 *
 * See copyright notice at: @(#)tftpd/tftpd.c	5.13 (Berkeley) 2/26/91
 */
#include "tftp/tftpsubs.h"

#include <boost/asio/ts/buffer.hpp>
#include <boost/asio/ts/internet.hpp>
#include <boost/current_function.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring> // strncpy still used! CK
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <syslog.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace tftpd {
extern off_t g_tsize;
extern uintmax_t g_segsize;
extern uintmax_t g_timeout;   // NOTE: 1 s as ms! CK
extern const char *g_rootdir; // the only tftp root dir used!
extern std::function<void(size_t)> g_callback;

int validate_access(std::string &filename, int mode, FILE *&file);
int tftp(const std::vector<char> &rxbuffer, FILE *&file, std::string &file_path, std::vector<char> &optack);

constexpr int TIMEOUT{1};
constexpr int rexmtval{TIMEOUT};
constexpr int maxtimeout{5 * TIMEOUT};
constexpr uintmax_t max_segsize{MAXSEGSIZE}; // RFC2348

struct errmsg
{
    int e_code;
    const char *e_msg;
};
static const errmsg errmsgs[] = {{EUNDEF, "Undefined error code"},
                                 {ENOTFOUND, "File not found"},
                                 {EACCESS, "Access violation"},
                                 {ENOSPACE, "Disk full or allocation exceeded"},
                                 {EBADOP, "Illegal TFTP operation"},
                                 {EBADID, "Unknown transfer ID"},
                                 {EEXISTS, "File already exists"},
                                 {ENOUSER, "No such user"},
                                 {EOPTNEG, "Failure to negotiate RFC2347 options"},
                                 {-1, nullptr}};

//----------------------------------------------------------------------
using boost::asio::ip::udp;
//----------------------------------------------------------------------

class server
{
public:
    server(boost::asio::io_context &io_context, uint16_t port)
        : socket_(io_context, udp::endpoint(udp::v4(), port)), timer_(io_context), timeout_(rexmtval)
    {
        start_timeout(maxtimeout); // max idle wait ...
        do_receive();
    }

    virtual ~server() = default;

    server(const server &) = delete;
    void operator=(const server &) = delete;

    server(server &&) = delete;
    server &operator=(server &&) = delete;

    std::string get_filename() { return file_path_; }

protected:
    void cancel_timeout() { timer_.cancel(); }

    void restart_timeout() { start_timeout(rexmtval); }

    void start_last_timeout()
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        start_timeout(rexmtval);
        timeout_ = maxtimeout; // NOTE: Normally times out and quits
        last_timeout_ = true;
    }

    void start_timeout(size_t seconds)
    {
        syslog(LOG_NOTICE, "%s(%lu)\n", BOOST_CURRENT_FUNCTION, seconds);

        timer_.cancel();
        timer_.expires_after(std::chrono::seconds(seconds));
        timer_.async_wait([this](const std::error_code &error) {
            // Cancel all asynchronous operations associated with the socket.
            if (!error) {
                timeout_ += rexmtval;
                if (!last_timeout_) {
                    syslog(LOG_WARNING, "tftpd: timeout\n");
                }
                if (socket_.is_open()) {
                    socket_.cancel();
                    if (timeout_ >= maxtimeout) {
                        socket_.close();
                        if (!last_timeout_) {
                            syslog(LOG_ERR, "tftpd: maxtimeout!\n");
                            throw std::system_error(std::make_error_code(std::errc::timed_out));
                        }
                    }
                }
            }
        });
    }

    virtual std::shared_ptr<FILE> start_recvfile(const udp::endpoint &senderEndpoint_, FILE *&file,
                                                 const std::vector<char> &optack) = 0;

    void do_receive()
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        rxdata_.resize(PKTSIZE);
        socket_.async_receive_from(boost::asio::buffer(rxdata_, PKTSIZE), senderEndpoint_,
                                   [this](std::error_code ec, std::size_t bytes_recvd) {
                                       if (!ec && bytes_recvd > 0) {
                                           rxdata_.resize(bytes_recvd);
                                           FILE *file = nullptr;
                                           int const error = tftp(rxdata_, file, file_path_, optack_);
                                           if (error != 0) {
                                               send_error(error);
                                           } else {
                                               socket_.close();
                                               socket_.open(udp::v4());
                                               socket_.bind(udp::endpoint(udp::v4(), 0));
                                               file_guard_ = start_recvfile(senderEndpoint_, file, optack_);
                                           }
                                       }
                                   });

        // NOTE: we need to return! CK
    }

    /*
     * Send a nak packet (error message).  Error code passed in is one of the
     * standard TFTP codes, or a UNIX errno offset by ERRNO_OFFSET(100).
     */
    void send_error(int error)
    {
        const struct errmsg *pe = nullptr;
        std::vector<char> txbuf;
        txbuf.resize(PKTSIZE);
        std::string err_msg;

        auto *tp = reinterpret_cast<struct tftphdr *>(txbuf.data());
        tp->th_opcode = htons(static_cast<u_short>(ERROR));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        tp->th_code = htons(static_cast<u_short>(error));
        for (pe = errmsgs; pe->e_code >= 0; pe++) {
            if (pe->e_code == error) {
                err_msg = pe->e_msg;
                syslog(LOG_ERR, "tftpd: send_error(%d): %s\n", error, err_msg.c_str());
                break;
            }
        }

        if (pe->e_code < 0) {
            err_msg = strerror(error - ERRNO_OFFSET);
            syslog(LOG_ERR, "tftpd: send_error(%d): %s\n", (error - ERRNO_OFFSET), err_msg.c_str());
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            tp->th_code = htons(static_cast<u_short>(EUNDEF)); /* set 'eundef(0)' errorcode */
        }

        size_t const extra = TFTP_HEADER + 1; // include strend '\0'
        err_msg.resize(std::min(err_msg.size(), PKTSIZE - TFTP_HEADER));
        size_t const length = err_msg.size() + extra;

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        (void)strncpy(tp->th_msg, err_msg.c_str(), length);

        txbuf.resize(std::min(length, static_cast<size_t>(PKTSIZE)));

        do_send_error(txbuf);
    }

    void do_send_error(const std::vector<char> &txdata)
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        socket_.async_send_to(boost::asio::buffer(txdata), senderEndpoint_,
                              [this](std::error_code /*ec*/, std::size_t /*bytes_sent*/) {
                                  // XXX if (ec) { syslog(LOG_ERR, "do_send_error: %s\n", ec.message().c_str()); }
                                  // TODO: now we have no timer running, the run loop terminates
                                  timer_.cancel();
                                  throw std::system_error(std::make_error_code(std::errc::operation_canceled));
                              });
    }

    udp::socket socket_;               // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    udp::endpoint senderEndpoint_;     // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    std::shared_ptr<FILE> file_guard_; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string file_path_;            // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<char> optack_;         // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

private:
    boost::asio::steady_timer timer_;
    std::vector<char> rxdata_;
    int timeout_;
    bool last_timeout_{false};
};

class receiver : public server
{
public:
    receiver(boost::asio::io_context &io_context, uint16_t port) : server(io_context, port) {}

    std::shared_ptr<FILE> start_recvfile(const udp::endpoint &senderEndpoint, FILE *&file,
                                         const std::vector<char> &optack) override
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        clientEndpoint_ = senderEndpoint;
        std::shared_ptr<FILE> file_guard(file, std::fclose);
        block = 0;
        dp_ = w_init(); // get first data buffer ptr
        if (optack.empty()) {
            send_ack();
        } else {
            memcpy(ackbuf_, optack.data(), optack.size());
            block++;
            send_ackbuf(optack.size());
        }

        return file_guard;
    }

    void send_ack()
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        auto *ap = reinterpret_cast<struct tftphdr *>(ackbuf_); /* ptr to ack buffer */
        ap->th_opcode = htons(static_cast<u_short>(ACK));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        ap->th_block = htons(static_cast<u_short>(block));
        block++;
        send_ackbuf();
    }

    void send_ackbuf(size_t length = TFTP_HEADER)
    {
        syslog(LOG_NOTICE, "%s(%lu)\n", BOOST_CURRENT_FUNCTION, length);

        // output the current buffer if needed
        (void)write_behind(file_guard_.get(), false);

        socket_.async_send_to(boost::asio::buffer(ackbuf_, length), clientEndpoint_,
                              [this](std::error_code ec, std::size_t /*bytes_sent*/) {
                                  if (ec) {
                                      syslog(LOG_ERR, "tftpd: send_ackbuf: %s\n", ec.message().c_str());
                                  } else {
                                      receive_block();
                                  }
                              });
    }

    void receive_block()
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        // Run an asynchronous read operation with a timeout.
        restart_timeout();
        socket_.async_receive_from(boost::asio::buffer(dp_, MAXPKTSIZE), clientEndpoint_,
                                   [this](std::error_code ec, std::size_t bytes_recvd) {
                                       if (ec) {
                                           syslog(LOG_ERR, "tftpd: read data: %s\n", ec.message().c_str());
                                           receive_block();
                                       } else {
                                           int const err = check_and_write_block(bytes_recvd);
                                           if (err != 0) {
                                               send_error(err);
                                           }
                                       }
                                   });
    }

    int check_and_write_block(size_t rxlen)
    {
        const uint16_t tmp = block;
        syslog(LOG_NOTICE, "%s(%u, len=%lu)\n", BOOST_CURRENT_FUNCTION, tmp, rxlen);

#if 0
        if (senderEndpoint_ != clientEndpoint_) {
            syslog(LOG_WARNING, "tftpd: Invalid endpoint ID!\n");
            return (EBADID);    // FIXME: this aborts running tftp Operation! CK
        }
#endif

        assert(rxlen >= TFTP_HEADER);

        do {
            dp_->th_opcode = ntohs(dp_->th_opcode);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            dp_->th_block = ntohs(dp_->th_block);
            if (dp_->th_opcode == ERROR) {
                syslog(LOG_ERR, "tftpd: ERROR received, abort Operation!\n");
                return 0; // OK
            }

            if (dp_->th_opcode == DATA) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                if (dp_->th_block == block) {
                    if (g_tsize != 0 && g_segsize != 0) { // NOTE: prevent division by zero! CK
                        auto totalBlocks = g_tsize / g_segsize + ((g_tsize % g_segsize) > 0 ? 1 : 0);
                        size_t const percent = (100UL * block) / totalBlocks;
                        if (percent != percent_) {
                            syslog(LOG_NOTICE, "tftpd: Progress: %lu%% received\n", percent);
                            percent_ = percent;
                            if (g_callback != nullptr) {
                                if ((percent % 10) == 0) {
                                    g_callback(percent);
                                }
                            }
                        }
                    }
                    break; /* normal */
                }

                // ==============================================
                synchnet(); // Re-synchronize with the other side
                // ==============================================

                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                if (dp_->th_block == (block - 1)) {
                    send_ackbuf(); /* rexmit => send last ack buf again */
                    return 0;      // OK
                }
            } else {
                syslog(LOG_ERR, "tftpd: Invalid opcode, DATA expected!\n");
                return (EBADID);
            }
        } while (false);

        // ===============================
        // write the current data segment
        // ===============================
        size_t const seg_length = rxlen - TFTP_HEADER;
        ssize_t written = writeit(file_guard_.get(), &dp_, seg_length, false);
        if (written != static_cast<ssize_t>(seg_length)) { /* ahem */
            int error = ENOSPACE;
            if (written < 0) {
                syslog(LOG_ERR, "tftpd: writeit() failed! %s\n", strerror(errno));
                error = (errno + ERRNO_OFFSET);
            }
            return (error);
        }

        if (seg_length == g_segsize) {
            send_ack();
            return 0; // OK
        }

        // =======================================================
        // write the final data segment
        written = write_behind(file_guard_.get(), false);
        if (written >= 0) {
            std::string const old_path(file_path_ + ".upload");
            (void)rename(old_path.c_str(), file_path_.c_str());
            syslog(LOG_NOTICE, "tftpd: successfully received file: %s\n", file_path_.c_str());
        } else {
            syslog(LOG_ERR, "tftpd: write_behind() failed! %s\n", strerror(errno));
            return (ENOSPACE);
        }
        // =======================================================

        send_last_ack();

        return 0; // OK
    }

    void send_last_ack()
    {
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        auto *ap = reinterpret_cast<struct tftphdr *>(ackbuf_); /* ptr to ack buffer */
        ap->th_opcode = htons(static_cast<u_short>(ACK));       /* send the "final" ack */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        ap->th_block = htons(static_cast<u_short>(block));
        (void)socket_.send_to(boost::asio::buffer(ackbuf_, TFTP_HEADER), clientEndpoint_);
        start_last_timeout();

        // Run an asynchronous read operation with a timeout.
        socket_.async_receive_from(boost::asio::buffer(rxbuf_, sizeof(rxbuf_)), clientEndpoint_,
                                   [this](std::error_code ec, std::size_t bytes_recvd) {
                                       if (!ec) {
                                           auto *dp = reinterpret_cast<struct tftphdr *>(rxbuf_);
                                           if ((bytes_recvd >= TFTP_HEADER) && /* if read some data */
                                               (dp->th_opcode == DATA) &&      /* and got a data block */
                                               // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                                               (block == dp->th_block)) {
                                               /* then my last ack was lost, resend final ack */
                                               // NOTE: do not call! send_ackbuf(); CK
                                               syslog(LOG_WARNING, "tftpd: Resend the final ack!");
                                               (void)socket_.send_to(boost::asio::buffer(ackbuf_, TFTP_HEADER),
                                                                     clientEndpoint_);
                                           }
                                       }
                                       cancel_timeout();
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
        syslog(LOG_NOTICE, "%s\n", BOOST_CURRENT_FUNCTION);

        int j = 0;
        struct sockaddr_storage from = {};
        socklen_t fromlen = 0;
        int const s = socket_.native_handle();

        while (true) {
            std::size_t const i = socket_.available();
            if (i != 0) {
                j++;
                fromlen = sizeof(from);
                (void)recvfrom(s, rxbuf_, sizeof(rxbuf_), 0, reinterpret_cast<struct sockaddr *>(&from), &fromlen);
            } else {
                if (j != 0) {
                    syslog(LOG_WARNING, "tftpd: Discarded %d packets\n", j);
                }
                return;
            }
        }
    }

private:
    struct tftphdr *dp_{nullptr};
    udp::endpoint clientEndpoint_;
    char ackbuf_[PKTSIZE]{};
    char rxbuf_[MAXPKTSIZE]{};
    std::atomic<u_int16_t> block{0};
    size_t percent_{0};
};
} // namespace tftpd
