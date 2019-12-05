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
#include <cctype> // tolower used
#include <cerrno>
#include <cinttypes> // strtoumax used
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring> // strcasecmp, memcpy used
#include <string>
#include <syslog.h>
#include <unistd.h>

constexpr uintmax_t MAX_SEGSIZE{65464}; // RFC2348
constexpr uintmax_t min_blksize_rfc{8}; // TBD: after RFC2348! CK
constexpr uintmax_t default_blksize{512};
constexpr uintmax_t max_blksize{MAX_SEGSIZE};
constexpr uintmax_t max_windowsize{64};
constexpr uintmax_t max_timeout{255}; // seconds
constexpr uintmax_t MS_1K{1000};      // default timeout

// XXX static uint16_t rollover_val = 0;
// XXX static uintmax_t windowsize = 1;
static constexpr bool tsize_ok{true}; // only octet mode supported!
static uintmax_t g_timeout = MS_1K;   // NOTE: ms! CK

namespace tftpd {
uintmax_t segsize{default_blksize};
off_t tsize{0};

static int set_blksize(uintmax_t *);
static int set_blksize2(uintmax_t *);
static int set_tsize(uintmax_t *);
static int set_timeout(uintmax_t *);
static int set_utimeout(uintmax_t *);
// XXX static int set_rollover(uintmax_t *);
// XXX static int set_windowsize(uintmax_t *);

struct options
{
    const char *o_opt;
    int (*o_fnc)(uintmax_t *);
};

static const struct options options[] = {{"blksize", set_blksize},
                                         {"blksize2", set_blksize2},
                                         {"tsize", set_tsize},
                                         {"timeout", set_timeout},
                                         {"utimeout", set_utimeout},
                                         // TBD: {"rollover", set_rollover},
                                         // TBD: not yet! CK {"windowsize", set_windowsize},
                                         {nullptr, nullptr}};

static bool blksize_set;

/*
 * Set a non-standard block size (c.f. RFC2348)
 */
static int set_blksize(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (blksize_set) {
        return 0;
    }

    if (sz < min_blksize_rfc) {
        return 0;
    }

    if (sz > max_blksize) {
        sz = max_blksize;
    }

    *vp = segsize = sz;
    blksize_set = true;
    return 1;
}

/*
 * Set a power-of-two block size (nonstandard)
 */
static int set_blksize2(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (blksize_set) {
        return 0;
    }

    if (sz < min_blksize_rfc) {
        return (0);
    }

    if (sz > max_blksize) {
        sz = max_blksize;
    } else {
        /* Convert to a power of two */
        if (sz & (sz - 1)) {
            unsigned int sz1 = 1;
            /* Not a power of two - need to convert */
            while (sz >>= 1) {
                sz1 <<= 1;
            }
            sz = sz1;
        }
    }

    *vp = segsize = sz;
    blksize_set = true;
    return 1;
}

/***
 * Set the block number rollover value
 * NOLINTNEXTLINE(readability-non-const-parameter)
static int set_rollover(uintmax_t *vp) // NOLINT
{
    uintmax_t ro = *vp;

    if (ro > UINT16_MAX) {
        return 0;
    }

    rollover_val = (uint16_t)ro;
    return 1;
}
 ***/

/*
 * Return a file size (c.f. RFC2349)
 * For netascii mode, we don't know the size ahead of time;
 * so reject the option.
 */
static int set_tsize(uintmax_t *vp)
{
    uintmax_t sz = *vp;

    if (!tsize_ok) {
        return 0; // netascii
    }

    if (sz == 0) {
        sz = tsize; // only usefull for RRQ
    } else {
        tsize = sz; // in case of WRQ
    }

    *vp = sz;
    return 1;
}

/*
 * Set the timeout (c.f. RFC2349).  This is supposed
 * to be the (default) retransmission timeout, but being an
 * integer in seconds it seems a bit limited.
 * NOLINTNEXTLINE(readability-non-const-parameter)
 */
static int set_timeout(uintmax_t *vp) // NOLINT
{
    uintmax_t to = *vp;

    if (to < 1 || to > max_timeout) {
        return 0;
    }

    g_timeout = to * MS_1K;

    return 1;
}

/*
 * Similar, but in microseconds.  We allow down to 10 ms.
 * NOLINTNEXTLINE(readability-non-const-parameter)
 */
static int set_utimeout(uintmax_t *vp) // NOLINT
{
    uintmax_t to = *vp;

    if (to < MS_1K || to > (max_timeout * MS_1K)) {
        return 0;
    }

    g_timeout = to / MS_1K;

    return 1;
}

/***
 * Set window size (c.f. RFC7440)
 * NOLINTNEXTLINE(readability-non-const-parameter)
static int set_windowsize(uintmax_t *vp)
{
    if (*vp < 1 || *vp > max_windowsize) {
        return 0;
    }

    windowsize = *vp;

    return 1;
}
 ***/

/* Global option-parsing variables initialization */
void init_opt()
{
    blksize_set = false;
    segsize = default_blksize;
    tsize = 0;
}

/*
 * Parse RFC2347 style options; we limit the arguments to positive
 * integers which matches all our current options.
 */
void do_opt(const char *opt, const char *val, char **ap)
{
    const struct options *po;
    char *p = *ap;
    size_t optlen;
    size_t retlen;
    char *vend;
    uintmax_t v;

    if (!*opt || !*val) {
        return;
    }

    syslog(LOG_NOTICE, "tftpd: %s:%s\n", opt, val);

    errno = 0;
    v = strtoumax(val, &vend, 10); // XXX see std::strtoul
    if (*vend || errno == ERANGE) {
        return;
    }

    for (po = options; po->o_opt; po++) {
        if (!strcasecmp(po->o_opt, opt)) { // XXX C-style compare
            if (po->o_fnc(&v)) {           // and the option is valid
                optlen = strlen(opt);
                std::string retbuf = std::to_string(v);
                retlen = retbuf.size();

                memcpy(p, opt, optlen + 1);
                p += optlen + 1;
                memcpy(p, retbuf.c_str(), retlen + 1);
                p += retlen + 1;
            } else {
                syslog(LOG_ERR, "tftpd: Unsupported option(%s:%s)\n", opt, val);
            }
            break;
        }
    }

    *ap = p;
}
} // namespace tftpd
