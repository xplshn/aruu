/* see license file for copyright and license details */
#ifndef REDLINE_H_
#define REDLINE_H_

#include <stddef.h>

struct redlineCompletions {
  size_t len;
  char **cvec;
};

char *redline(const char *prompt);
void  redlineSetCompletionCallback(void (*cb)(const char *, struct redlineCompletions *));
void  redlineAddCompletion(struct redlineCompletions *lc, const char *str);
void  redlineHistoryAdd(const char *line);
void  redlineHistorySetMaxLen(int len);
int   redlineHistorySave(const char *filename);
int   redlineHistoryLoad(const char *filename);
char *redlineHistoryGet(int idx);
int   redlineHistoryLen(void);
void  redlineSetMultiLine(int ml);
void  redlineClearScreen(void);

#endif /* REDLINE_H_ */
