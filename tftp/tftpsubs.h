#pragma once

#include <arpa/tftp.h>

#ifdef __cplusplus
#    include <cstdint>
#    include <cstdio>
#    define _Bool bool
#    define BOOST_CURRENT_FUNCTION static_cast<const char *>(__PRETTY_FUNCTION__)
constexpr int ERRNO_OFFSET{100};
#else
#    include <stdint.h>
#    include <stdio.h>
#    define _Bool int
#endif

#define TFTP_HEADER (2 * sizeof(uint16_t)) /* should be moved to tftp.h */
#define PKTSIZE (SEGSIZE + TFTP_HEADER)    /* should be moved to tftp.h */

#ifndef OACK
#define OACK 6
#endif
#ifndef EOPTNEG
#define EOPTNEG 8
#endif

void initsock(int);
void synchnet(int f, _Bool trace);
struct tftphdr *rw_init(int);
static inline struct tftphdr *w_init() { return rw_init(0); } /* write-behind */
static inline struct tftphdr *r_init() { return rw_init(1); } /* read-ahead */
int readit(FILE *file, struct tftphdr **dpp, _Bool convert);
int writeit(FILE *file, struct tftphdr **dpp, int count, _Bool convert);
void read_ahead(FILE *file, _Bool convert /* if true, convert to ascii */);
int write_behind(FILE *file, _Bool convert);

void mysignal(int sig, void (*handler)(int));
