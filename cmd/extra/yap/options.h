#ifndef OPTIONS_H
#define OPTIONS_H

extern int   cflag;
extern int   uflag;
extern int   nflag;
extern int   qflag;
extern char *startcomm;

char **readoptions(char **argv);

#endif
