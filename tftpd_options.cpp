/*
 * Copyright (c) 1983 Regents of the University of California.
 * Copyright (c) 1999-2009 H. Peter Anvin
 * Copyright (c) 2011-2014 Intel Corporation; author: H. Peter Anvin
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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

#include "tftp/tftpsubs.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cinttypes> // strtoumax used
#include <cstring>   // strcasecmp, memcpy used
#include <string>
#include <syslog.h>

namespace tftpd {
constexpr uintmax_t min_blksize_rfc{8}; // TBD: after RFC2348! CK
constexpr uintmax_t default_blksize{SEGSIZE};
constexpr uintmax_t max_blksize{MAX_SEGSIZE};
constexpr uintmax_t max_windowsize{64};
constexpr uintmax_t max_timeout{255}; // seconds
constexpr uintmax_t MS_1K{1000};      // default timeout

// XXX static uint16_t rollover_val = 0;
// XXX static uintmax_t windowsize = 1;
static constexpr bool tsize_ok{true}; // only octet mode supported!

uintmax_t g_timeout = MS_1K; // NOTE: 1 s as ms! CK
uintmax_t g_segsize{default_blksize};
off_t g_tsize{0};

static bool set_blksize(uintmax_t *vp);
static bool set_blksize2(uintmax_t *vp);
static bool set_tsize(uintmax_t *vp);
static bool set_timeout(uintmax_t *vp);
static bool set_utimeout(uintmax_t *vp);
// XXX static bool set_rollover(uintmax_t *vp);
// XXX static bool set_windowsize(uintmax_t *vp);

struct option
{
    const char *o_opt;
    bool (*o_fnc)(uintmax_t *);
};

static const struct option options[] = {{"blksize", set_blksize},
                                        {"blksize2", set_blksize2},
                                        {"tsize", set_tsize},
                                        {"timeout", set_timeout},
                                        {"utimeout", set_utimeout},
                                        // TBD: {"rollover", set_rollover},
                                        // TBD: not yet! CK {"windowsize", set_windowsize},
                                        {nullptr, nullptr}};

static bool blksize_set{false};

/*
 * Set a non-standard block size (c.f. RFC2348)
 */
static bool set_blksize(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (blksize_set) {
        return false;
    }

    if (sz < min_blksize_rfc) {
        return false;
    }

    if (sz > max_blksize) {
        sz = max_blksize;
    }

    *vp = g_segsize = sz;
    blksize_set = true;
    return true;
}

/*
 * Set a power-of-two block size (nonstandard)
 */
static bool set_blksize2(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (blksize_set) {
        return false;
    }

    if (sz < min_blksize_rfc) {
        return false;
    }

    if (sz > max_blksize) {
        sz = max_blksize;
    } else {
        /* Convert to a power of two */
        if ((sz & (sz - 1)) != 0) {
            unsigned int sz1 = 1;
            /* Not a power of two - need to convert */
            while ((sz >>= 1) != 0) {
                sz1 <<= 1;
            }
            sz = sz1;
        }
    }

    *vp = g_segsize = sz;
    blksize_set = true;
    return true;
}

/***
 * Set the block number rollover value
 * NOLINTNEXTLINE(readability-non-const-parameter)
static bool set_rollover(uintmax_t *vp) // NOLINT
{
    uintmax_t ro = *vp;

    if (ro > UINT16_MAX) {
        return false;
    }

    rollover_val = (uint16_t)ro;
    return true;
}
 ***/

/*
 * Return a file size (c.f. RFC2349)
 * For netascii mode, we don't know the size ahead of time;
 * so reject the option.
 */
static bool set_tsize(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (!tsize_ok) {
        return false; // netascii
    }

    if (sz == 0) {
        sz = g_tsize; // only usefull for RRQ
    } else {
        g_tsize = sz; // in case of WRQ
    }

    *vp = sz;
    return true;
}

/*
 * Set the timeout (c.f. RFC2349).  This is supposed
 * to be the (default) retransmission timeout, but being an
 * integer in seconds it seems a bit limited.
 * NOLINTNEXTLINE(readability-non-const-parameter)
 */
static bool set_timeout(uintmax_t *vp) // NOLINT
{
    uintmax_t to = *vp;

    if (to < 1 || to > max_timeout) {
        return false;
    }

    g_timeout = to * MS_1K;

    return true;
}

/*
 * Similar, but in microseconds.  We allow down to 10 ms.
 * NOLINTNEXTLINE(readability-non-const-parameter)
 */
static bool set_utimeout(uintmax_t *vp) // NOLINT
{
    uintmax_t to = *vp;

    if (to < MS_1K || to > (max_timeout * MS_1K * MS_1K)) {
        return false;
    }

    g_timeout = to / MS_1K;

    return true;
}

/***
 * Set window size (c.f. RFC7440)
 * NOLINTNEXTLINE(readability-non-const-parameter)
static bool set_windowsize(uintmax_t *vp)
{
    if (*vp < 1 || *vp > max_windowsize) {
        return false;
    }

    windowsize = *vp;

    return true;
}
 ***/

/* Global option-parsing variables initialization */
void init_opt()
{
    blksize_set = false;
    g_segsize = default_blksize;
    g_timeout = MS_1K;
    g_tsize = 0;
}

/*
 * Parse RFC2347 style options; we limit the arguments to positive
 * integers which matches all our current options.
 */
void do_opt(const char *opt, const char *val, char **ackbuf_ptr)
{
    const struct option *po;
    char *p = *ackbuf_ptr;
    assert(ackbuf_ptr != nullptr);
    assert(opt != nullptr);
    assert(val != nullptr);

    if (*opt == 0 || *val == 0) {
        return;
    }

    syslog(LOG_NOTICE, "tftpd: %s:%s\n", opt, val);

    errno = 0;
    char *vend;
    uintmax_t v = strtoumax(val, &vend, 10); // TODO: or std::strtoul! CK
    if (*vend != 0 || errno == ERANGE) {
        syslog(LOG_ERR, "tftpd: Invallid option value (%s:%s)\n", opt, val);
        return;
    }

    for (po = options; po->o_opt != nullptr; po++) {
        if (strcasecmp(po->o_opt, opt) == 0) { // XXX C-style compare
            if (po->o_fnc(&v)) {               // found and the option is valid
                size_t optlen = strlen(opt);
                std::string ret_value = std::to_string(v);
                size_t retlen = ret_value.size();

                memcpy(p, opt, optlen + 1);
                p += optlen + 1;
                memcpy(p, ret_value.c_str(), retlen + 1);
                p += retlen + 1;
            } else {
                syslog(LOG_ERR, "tftpd: Unsupported option(%s:%s)\n", opt, val);
            }
            break;
        }
    }

    *ackbuf_ptr = p;
}
} // namespace tftpd
