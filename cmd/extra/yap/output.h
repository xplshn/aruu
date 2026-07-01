#ifndef OUTPUT_H
#define OUTPUT_H

extern int _ocnt;
extern char *_optr;

#define putch(ch) \
	do { \
		if (--_ocnt <= 0) \
			flush(); \
		*_optr++ = (ch); \
	} while (0)

void flush(void);
void nflush(void);
int fputch(int c);
void putline(char *s);
void cputline(char *s);
void prnum(long n);
char *getnum(long n);

#endif
