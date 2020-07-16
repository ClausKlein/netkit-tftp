#pragma once

#include <arpa/tftp.h>

#ifdef __cplusplus
#    include <boost/current_function.hpp>

#    include <cstdint>
#    include <cstdio>
#    define _Bool bool
constexpr int ERRNO_OFFSET{100};
#else
#    include <stdint.h>
#    include <stdio.h>
#    define _Bool int
#endif

#ifndef TFTP_HEADER
#    define TFTP_HEADER (2 * sizeof(short)) /* should be moved to tftp.h */
#    ifndef PKTSIZE
#        define PKTSIZE (SEGSIZE + TFTP_HEADER) /* should be moved to tftp.h */
#        define MAXSEGSIZE 65464UL              /* RFC2348 */
#        define MAXPKTSIZE (MAXSEGSIZE + TFTP_HEADER)
#    endif
#endif

#ifndef OACK
#    define OACK 6
#endif
#ifndef EOPTNEG
#    define EOPTNEG 8
#endif

void initsock(int /*af*/);
void synchnet(int f, _Bool trace);
struct tftphdr *rw_init(int /*x*/);
static inline struct tftphdr *w_init() { return rw_init(0); } /* write-behind */
static inline struct tftphdr *r_init() { return rw_init(1); } /* read-ahead */
ssize_t readit(FILE *file, struct tftphdr **dpp, _Bool convert);
ssize_t writeit(FILE *file, struct tftphdr **dpp, size_t count, _Bool convert);
void read_ahead(FILE *file, _Bool convert /* if true, convert to ascii */);
ssize_t write_behind(FILE *file, _Bool convert);

void mysignal(int sig, void (*handler)(int));
