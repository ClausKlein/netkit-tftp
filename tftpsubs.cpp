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

/*
 * From: @(#)tftpsubs.c	5.6 (Berkeley) 2/28/91
 */
// char subs_rcsid[] = "$Id: tftpsubs.c,v 1.8 2000/07/22 19:06:29 dholland Exp
// $";

/* Simple minded read-ahead/write-behind subroutines for tftp user and
   server.  Written originally with multiple buffers in mind, but current
   implementation has two buffer logic wired in.

   Todo:  add some sort of final error check so when the write-buffer
   is finally flushed, the caller can detect if the disk filled up
   (or had an i/o error) and return a nak to the other side.

                        Jim Guyton 10/85
 */
#include "tftp/tftpsubs.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

struct bf
{
    int counter;       /* size of data in buffer, or flag */
    char buf[PKTSIZE]; /* room for data packet */
} bfs[2];

/* Values for bf.counter  */
#define BF_ALLOC -3 /* alloc'd but not yet filled */
#define BF_FREE -2  /* free */
/* [-1 .. SEGSIZE] = size of data in the data buffer */

static int nextone; /* index of next buffer to use */
static int current; /* index of buffer in use */

/* control flags for crlf conversions */
bool newline = false; /* fillbuf: in middle of newline expansion */
int prevchar = -1;    /* putbuf: previous char (cr check) */

/*
 * init for either read-ahead or write-behind
 * zero for write-behind, one for read-head
 */
struct tftphdr *rw_init(int x)
{
    newline = false; /* init crlf flag */
    prevchar = -1;
    bfs[0].counter = BF_ALLOC; /* pass out the first buffer */
    current = 0;
    bfs[1].counter = BF_FREE;
    nextone = x; /* ahead or behind? */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    return (struct tftphdr *)bfs[0].buf;
}

#if 0
/*
 * Have emptied current buffer by sending to net and getting ack.
 * Free it and return next buffer filled with data.
 */
int readit(FILE *file, struct tftphdr **dpp,
           bool convert /* if true, convert to ascii */)
{
    struct bf *b;

    bfs[current].counter = BF_FREE; /* free old one */
    current = !current;             /* "incr" current */

    b = &bfs[current];             /* look at new buffer */
    if (b->counter == BF_FREE) {   /* if it's empty */
        read_ahead(file, convert); /* fill it */
    }
    /*      assert(b->counter != BF_FREE); */ /* check */
    *dpp = (struct tftphdr *)b->buf;          /* set caller's ptr */
    return b->counter;
}

/*
 * fill the input buffer, doing ascii conversions if requested
 * conversions are  lf -> cr,lf  and cr -> cr, nul
 */
void read_ahead(FILE *file, bool convert /* if true, convert to ascii */)
{
    int i;
    char *p;
    int c;
    struct bf *b;
    struct tftphdr *dp;

    b = &bfs[nextone];           /* look at "next" buffer */
    if (b->counter != BF_FREE) { /* nop if not free */
        return;
    }
    nextone = !nextone; /* "incr" next buffer ptr */

    dp = (struct tftphdr *)b->buf;

    if (!convert) {
        b->counter = read(fileno(file), dp->th_data, SEGSIZE);
        return;
    }

    p = dp->th_data;
    for (i = 0; i < SEGSIZE; i++) {
        if (newline) {
            if (prevchar == '\n') {
                c = '\n'; /* lf to cr,lf */
            } else {
                c = '\0'; /* cr to cr,nul */
            }
            newline = false;
        } else {
            c = getc(file);
            if (c == EOF) {
                break;
            }
            if (c == '\n' || c == '\r') {
                prevchar = c;
                c = '\r';
                newline = true;
            }
        }
        *p++ = c;
    }
    b->counter = (int)(p - dp->th_data);
}
#endif

/*
 * Update count associated with the buffer, get new buffer
 * from the queue.  Calls write_behind only if next buffer not
 * available.
 */
int writeit(FILE *file, struct tftphdr **dpp, int count, bool convert)
{
    bfs[current].counter = count;            /* set size of data to write */
    current = !current;                      /* switch to other buffer */
    if (bfs[current].counter != BF_FREE) {   /* if not free */
        count = write_behind(file, convert); /* flush it */
    }
    bfs[current].counter = BF_ALLOC; /* mark as alloc'd */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    *dpp = (struct tftphdr *)bfs[current].buf;
    return count; /* this is a lie of course */
}

/*
 * Output a buffer to a file, converting from netascii if requested.
 * CR,NUL -> CR  and CR,LF => LF.
 * Note spec is undefined if we get CR as last byte of file or a
 * CR followed by anything else.  In this case we leave it alone.
 */
int write_behind(FILE *file, bool /*convert*/)
{
    char *buf;
    int count;
    struct bf *b;
    struct tftphdr *dp;

    b = &bfs[nextone];
    if (b->counter < -1) { /* anything to flush? */
        syslog(LOG_INFO, "tftpd: write() nothing to flush!\n");
        return 0; /* just nop if nothing to do */
    }

    count = b->counter;   /* remember byte count */
    b->counter = BF_FREE; /* reset flag */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    dp = (struct tftphdr *)b->buf;
    nextone = !nextone; /* incr for next time */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    buf = dp->th_data;

    if (count < 0) {
        syslog(LOG_ERR, "tftpd: write() invalid buffer count!\n");
        return -1; /* TBD: no nak logic! CK */
    }

#ifndef USE_CONVERT
    return write(fileno(file), buf, count);
#else
    if (!convert) {
        return write(fileno(file), buf, count);
    }

    char *p = buf;
    int ct = count;
    while ((ct--) != 0) {           /* loop over the buffer */
        int c;                      /* current character */
        c = *p++;                   /* pick up a character */
        if (prevchar == '\r') {     /* if prev char was cr */
            if (c == '\n') {        /* if have cr,lf then just */
                fseek(file, -1, 1); /* smash lf on top of the cr */
            } else if (c == '\0') { /* if have cr,nul then */
                goto skipit;        /* just skip over the putc */
                                    /* else just fall through and allow it */
            }
        }
        putc(c, file);
    skipit:
        prevchar = c;
    }

    return count;
#endif
}
