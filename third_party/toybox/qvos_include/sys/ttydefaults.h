/* Minimal <sys/ttydefaults.h> constants for toybox/picolibc. */
#ifndef QVOS_SYS_TTYDEFAULTS_H
#define QVOS_SYS_TTYDEFAULTS_H

#define CEOF      1
#define CEOL      0
#define CERASE    2
#define CINTR     3
#define CSTATUS   0
#define CKILL     4
#define CMIN      1
#define CQUIT     5
#define CSUSP     6
#define CTIME     0
#define CDSUSP    7
#define CSTART    8
#define CSTOP     9
#define CLNEXT    10
#define CDISCARD  11
#define CWERASE   12
#define CREPRINT  13
#define CEOT      CEOF
#define CBRK      CEOL
#define CRPRNT    CREPRINT
#define CFLUSH    CDISCARD

#endif
