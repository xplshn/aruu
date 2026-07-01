#ifndef COMMANDS_H
#define COMMANDS_H

#define SCREENSIZE_ADAPT 01
#define SCROLLSIZE_ADAPT 02
#define TONEXTFILE 04
#define AHEAD 010
#define BACK 020
#define NEEDS_SCREEN 040
#define TOPREVFILE 0100
#define STICKY 0200
#define NEEDS_COUNT 0400
#define ESC 01000

extern struct commands {
	char *c_cmd;
	int c_flags;
	int (*c_func)(long);
	char *c_descr;
} commands[];

int do_chkm(long cnt);
void do_comm(int command, long count);
int lookup(char *str);
void wrt_fd(int fd);

#endif
