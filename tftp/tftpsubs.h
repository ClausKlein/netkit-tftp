#pragma once

#include <stdio.h>

#define PKTSIZE (SEGSIZE + 4) /* should be moved to tftp.h */

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
