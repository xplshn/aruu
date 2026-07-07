#ifndef GETLINE_H
#define GETLINE_H

char *getline(long ln, int disable_interrupt);
char *alloc(unsigned size);
void  do_clean(void);
int   getch(void);
long  to_lastline(void);
long  getpos(long line);

#endif
