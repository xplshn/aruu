#ifndef DISPLAY_H
#define DISPLAY_H

#define MINPAGESIZE 5

extern int pagesize;
extern int maxpagesize;
extern int scrollsize;

struct scr_info {
	struct linelist {
		int cnt;
		long line;
#define firstline head->line
#define lastline tail->line
		struct linelist *next;
		struct linelist *prev;
	} *tail, *head;
	int nf;
	int currentpos;
	struct linelist ssaavv;
#define savfirst ssaavv.line
#define savnf ssaavv.cnt
};

extern struct scr_info scr_info;
extern int status;

#define EOFILE 01
#define START 02

void redraw(int flag);
int display(long first_line, int nodispl, int nlines, int really);
int scrollf(int nlines, int really);
int scrollb(int nlines, int really);
int tomark(long cnt);
int setmark(long cnt);
int exgmark(long cnt);
void d_clean(void);

#endif
