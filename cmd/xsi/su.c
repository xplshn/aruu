/* See LICENSE file for copyright and license details. */
#include "config.h"
#include "passwd.h"
#include "util.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

extern char **environ;

#ifndef ENV_PATH_SHELL
#define ENV_PATH_SHELL "/bin/sh"
#endif

static int lflag = 0;
static int pflag = 0;

static void
usage(void)
{
	eprintf("usage: %s [-lp] [username]\n", argv0);
}

// ?man su: run a command with substitute user and group id
// ?man arguments: [username]
// ?man run a shell or command as the named user. defaults to root
int
main(int argc, char *argv[])
{
	char *usr, *pass;
	char *shell, *envshell, *term;
	struct passwd *pw;
	char *newargv[3];
	uid_t uid;

	ARGBEGIN {
	case 'l':
		// ?man -l: make the shell a login shell
		lflag = 1;
		break;
	case 'p':
		// ?man -p: preserve the current environment
		pflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc > 1)
		usage();
	usr = argc > 0 ? argv[0] : "root";

	errno = 0;
	pw = getpwnam(usr);
	if (!pw) {
		if (errno)
			eprintf("getpwnam: %s:", usr);
		else
			eprintf("who are you?\n");
	}

	uid = getuid();
	if (uid) {
		pass = getpass("Password: ");
		if (!pass)
			eprintf("getpass:");
		if (pw_check(pw, pass) <= 0)
			exit(1);
		explicit_bzero(pass, strlen(pass));
	}

	if (initgroups(usr, pw->pw_gid) < 0)
		eprintf("initgroups:");
	if (setgid(pw->pw_gid) < 0)
		eprintf("setgid:");
	if (setuid(pw->pw_uid) < 0)
		eprintf("setuid:");

	shell = pw->pw_shell[0] == '\0' ? ENV_PATH_SHELL : pw->pw_shell;
	if (lflag) {
		term = getenv("TERM");
		clearenv();
		setenv("HOME", pw->pw_dir, 1);
		setenv("SHELL", shell, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("LOGNAME", pw->pw_name, 1);
		setenv("TERM", term ? term : "dumb", 1);
		setenv("PATH", ENV_PATH, 1);
		if (chdir(pw->pw_dir) < 0)
			eprintf("chdir %s:", pw->pw_dir);
		newargv[0] = shell;
		newargv[1] = "-l";
		newargv[2] = NULL;
	} else {
		if (pflag) {
			envshell = getenv("SHELL");
			if (envshell && envshell[0] != '\0')
				shell = envshell;
		} else {
			setenv("HOME", pw->pw_dir, 1);
			setenv("SHELL", shell, 1);
			if (strcmp(pw->pw_name, "root") != 0) {
				setenv("USER", pw->pw_name, 1);
				setenv("LOGNAME", pw->pw_name, 1);
			}
		}
		newargv[0] = shell;
		newargv[1] = NULL;
	}
	execve(shell, newargv, environ);
	weprintf("execve %s:", shell);
	return (errno == ENOENT) ? 127 : 126;
}
