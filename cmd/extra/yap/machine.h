#ifndef MACHINE_H
#define MACHINE_H

struct state {
	char s_char;
	char s_endstate;
	struct state *s_match;
	struct state *s_next;
	short s_cnt;
};

#define FSM_OKE 0
#define FSM_ISPREFIX -1
#define FSM_HASPREFIX 1

int addstring(char *str, int cnt, struct state **mach);
int match(char *str, int *p_int, struct state *mach);

#endif
