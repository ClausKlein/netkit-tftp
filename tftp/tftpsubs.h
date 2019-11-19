#pragma once

#include <stdint.h>
#include <stdio.h>

#define TFTP_HEADER (2 * sizeof(uint16_t)) /* should be moved to tftp.h */
#define PKTSIZE (SEGSIZE + TFTP_HEADER)    /* should be moved to tftp.h */

#ifdef __cplusplus
#    define _Bool bool
#else
#    define _Bool int
#endif

void initsock(int);
void synchnet(int f, _Bool trace);
struct tftphdr *r_init(void);
struct tftphdr *w_init(void);
int readit(FILE *file, struct tftphdr **dpp, _Bool convert);
int writeit(FILE *file, struct tftphdr **dpp, int ct, _Bool convert);
void read_ahead(FILE *file, _Bool convert /* if true, convert to ascii */);
int write_behind(FILE *file, _Bool convert);

void mysignal(int sig, void (*handler)(int));
