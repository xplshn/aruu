#ifndef GETCOMM_H
#define GETCOMM_H

int   getcomm(long *arg);
void  shellescape(char *command, int esc_char);
char *readline(char *prompt);

#endif
