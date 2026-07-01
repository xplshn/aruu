#ifndef TERM_H
#define TERM_H

extern int expandtabs;
extern int stupid;
extern int hardcopy;

extern char *CE;
extern char *CL;
extern char *SO;
extern char *SE;
extern char *US;
extern char *UE;
extern char *UC;
extern char *MD;
extern char *ME;
extern char *TI;
extern char *TE;
extern char *CM;
extern char *TA;
extern char *SR;
extern char *AL;
extern char *UP;
extern char *HO;
extern char *BO;

extern int LINES;
extern int COLS;
extern int AM;
extern int XN;
extern int DB;

extern int erasech;
extern int killch;
extern struct state *sppat;
extern char *BC;

void inittty(void);
void resettty(void);
void ini_terminal(void);
void backspace(void);
void clrscreen(void);
void clrtoeol(void);
void scrollreverse(void);
void standout(void);
void standend(void);
void underline(void);
void end_underline(void);
void bold(void);
void end_bold(void);
void underchar(void);
void givetab(void);
void mgoto(int n);
void clrbline(void);
void home(void);
void bottom(void);
int window(void);
void ins_line(int l);
#define insert_line(l) ins_line(l)

#endif
