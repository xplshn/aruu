/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include <ctype.h>
#include "in_all.h"
#include "machine.h"
#include "keys.h"
#include "commands.h"
#include "prompt.h"
#include "assert.h"

struct keymap *currmap, *othermap;

char defaultmap[] = "\
bf=P:bl=k:bl=^K:bl=^[[A:bot=l:bot=$:bp=-:bs=^B:bse=?:bsl=S:bsp=F:chm=X:exg=x:\
ff=N:fl=^J:fl=^M:fl=j:fl=^[[B:fp= :fs=^D:fse=/:fsl=s:fsp=f:hlp=h:nse=n:nsr=r:\
red=^L:rep=.:bps=Z:bss=b:fps=z:fss=d:shl=!:tom=':top=\\^:vis=e:\
wrf=w:qui=q:qui=Q:mar=m:pip=|";

/*
 * Construct an error message and return it
 */

static char *
kerror(char *key, char *emess)
{
	static char ebuf[80]; /* Room for the error message */

	(void)strcpy(ebuf, key);
	(void)strcat(ebuf, emess);
	return ebuf;
}

/*
 * Compile a keymap into commtable. Returns an error message if there
 * is one
 */

static char *
compile(char *map, struct keymap *commtable)
{
	char *mark; /* Indicates start of mnemonic */
	char *c;    /* Runs through buf */
	int temp;
	char *escapes = commtable->k_esc;
	char buf[10]; /* Will hold key sequence */

	(void)strcpy(commtable->k_help, "Illegal command");
	while (*map) {
		c = buf;
		mark = map; /* Start of mnemonic */
		while (*map && *map != '=') {
			map++;
		}
		if (!*map) {
			/*
			 * Mnemonic should end with '='
			 */
			return kerror(mark, ": Syntax error");
		}
		*map++ = 0;
		while (*map) {
			/*
			 * Get key sequence
			 */
			if (*map == ':') {
				/*
				 * end of key sequence
				 */
				map++;
				break;
			}
			*c = *map++ & 0x7f;
			if (*c == '^' || *c == '\\') {
				if (!(temp = *map++)) {
					/*
					 * Escape not followed by a character
					 */
					return kerror(mark, ": Syntax error");
				}
				if (*c == '^') {
					if (temp == '?')
						*c = 0x7f;
					else
						*c = temp & 0x1f;
				} else
					*c = temp & 0x7f;
			}
			setused(*c);
			c++;
			if (c >= &buf[9]) {
				return kerror(mark, ": Key sequence too long");
			}
		}
		*c = 0;
		if (!(temp = lookup(mark))) {
			return kerror(mark, ": Nonexistent function");
		}
		if (c == &buf[1] && (commands[temp].c_flags & ESC) &&
		    escapes < &(commtable->k_esc[sizeof(commtable->k_esc) - 1])) {
			*escapes++ = buf[0] & 0x7f;
		}
		temp = addstring(buf, temp, &(commtable->k_mach));
		if (temp == FSM_ISPREFIX) {
			return kerror(mark, ": Prefix of other key sequence");
		}
		if (temp == FSM_HASPREFIX) {
			return kerror(mark, ": Other key sequence is prefix");
		}
		assert(temp == FSM_OKE);
		if (!strcmp(mark, "hlp")) {
			/*
			 * Create an error message to be given when the user
			 * types an illegal command
			 */
			(void)strcpy(commtable->k_help, "Type ");
			(void)strcat(commtable->k_help, buf);
			(void)strcat(commtable->k_help, " for help");
		}
	}
	*escapes = 0;
	return (char *)0;
}

/*
 * Initialize the keymaps
 */

void
initkeys()
{
	char *p;
	static struct keymap xx[2];

	currmap = &xx[0];
	othermap = &xx[1];
	p = compile(defaultmap, currmap); /* Compile default map */
	assert(p == (char *)0);
	p = getenv("YAPKEYS");
	if (p) {
		if (!(p = compile(p, othermap))) {
			/*
			 * No errors in user defined keymap. So, use it
			 */
			do_chkm(0L);
			return;
		}
		error(p);
	}
	othermap = 0; /* No other keymap */
}

int
is_escape(int c)
{
	char *p = currmap->k_esc;

	while (*p) {
		if (c == *p++)
			return 1;
	}
	return 0;
}

static char keyset[16]; /* bitset indicating which keys are
                         * used
                         */
/*
 * Mark key "key" as used
 */

void
setused(int key)
{

	keyset[(key & 0x7f) >> 3] |= (1 << (key & 0x07));
}

/*
 * return non-zero if key "key" is used in a keymap
 */

int
isused(int key)
{

	return keyset[(key & 0x7f) >> 3] & (1 << (key & 0x07));
}
