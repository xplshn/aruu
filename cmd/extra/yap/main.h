#ifndef MAIN_H
#define MAIN_H

extern int   nopipe;
extern char *progname;
extern int   interrupt;
extern int   no_tty;

int  main(int argc, char **argv);
void catchdel(int signo);
int  quit(void);
void panic(char *s);

#ifdef SIGTSTP
void suspend(void);
#endif

#endif
