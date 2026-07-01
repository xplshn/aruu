/* see license file for copyright and license details */
#ifndef LINEEDIT_H_
#define LINEEDIT_H_

#if FEATURE_SH_HISTEDIT

#include "redline.h"

extern int sh_history_enabled;
extern int displayhist;

void histedit(void);
void sethistsize(const char *s);
void setterm(const char *t);
void histload(void);
void histsave(void);

#else

#define histedit()     ((void)0)
#define sethistsize(s) ((void)0)
#define setterm(t)     ((void)0)
#define histload()     ((void)0)
#define histsave()     ((void)0)

#endif

#endif /* LINEEDIT_H_ */