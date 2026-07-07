#ifndef PROCESS_H
#define PROCESS_H

#include <setjmp.h>

extern jmp_buf SetJmpBuf;
extern int     DoneSetJmp;

extern int    stdf;
extern int    filecount;
extern char **filenames;
extern char  *currentfile;
extern long   maxpos;

void visitfile(char *fn);
void processfiles(int n, char **argv);
int  nextfile(int n);

#endif
