/* See LICENSE file for copyright and license details. */
#include "util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
	eprintf("usage: %s [-P fd] [-m type] [-S salt] [password] [salt]\n", argv0);
}

// ?man mkpasswd: encrypt the given password using salt
// ?man arguments: [password] [salt]
// ?man encrypt password using crypt(3) with random or provided salt
int
main(int argc, char *argv[])
{
	char *mflag;
	char *sflag;
	char *parg;
	char *password;
	char *cryptpass;
	int pfd;
	int fd;
	int r;
	int i;
	int len;
	int id;
	char salt[128];
	char rand_bytes[32];
	char passbuf[1024];
	static const char saltchars[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	mflag = NULL;
	sflag = NULL;
	parg = NULL;
	password = NULL;
	cryptpass = NULL;
	pfd = -1;

	ARGBEGIN {
	// ?man -P:fd: read password from file descriptor
	case 'P':
		parg = EARGF(usage());
		pfd = estrtonum(parg, 0, INT_MAX);
		break;
	// ?man -m:type: encryption method (des, md5, sha256, sha512)
	case 'm':
		mflag = EARGF(usage());
		break;
	// ?man -S:salt: salt to use
	case 'S':
		sflag = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (argc > 2)
		usage();

	if (argc == 2) {
		if (sflag)
			eprintf("duplicate salt\n");
		sflag = argv[1];
	}

	if (argc >= 1)
		password = argv[0];

	/* default type is des */
	if (!mflag)
		mflag = "des";

	if (strcasecmp(mflag, "des") == 0) {
		id = 0;
		len = 2;
	} else if (strcasecmp(mflag, "md5") == 0) {
		id = 1;
		len = 8;
	} else if (strcasecmp(mflag, "sha256") == 0) {
		id = 5;
		len = 16;
	} else if (strcasecmp(mflag, "sha512") == 0) {
		id = 6;
		len = 16;
	} else {
		eprintf("bad method: %s\n", mflag);
	}

	if (sflag) {
		/* validate provided salt */
		char *s = sflag;
		while (*s) {
			if (!isalnum((unsigned char)*s) && *s != '.' && *s != '/')
				eprintf("bad SALT (need [a-zA-Z0-9./])\n");
			s++;
		}
		if (id > 0)
			snprintf(salt, sizeof(salt), "$%d$%s", id, sflag);
		else
			estrlcpy(salt, sflag, sizeof(salt));
	} else {
		/* generate random salt */
		fd = open("/dev/urandom", O_RDONLY);
		if (fd < 0)
			eprintf("open /dev/urandom:");
		r = read(fd, rand_bytes, len);
		if (r < len)
			eprintf("read /dev/urandom:");
		close(fd);

		if (id > 0) {
			snprintf(salt, sizeof(salt), "$%d$", id);
			for (i = 0; i < len; i++)
				salt[3 + i] = saltchars[(unsigned char)rand_bytes[i] % 64];
			salt[3 + len] = '\0';
		} else {
			for (i = 0; i < len; i++)
				salt[i] = saltchars[(unsigned char)rand_bytes[i] % 64];
			salt[len] = '\0';
		}
	}

	if (pfd >= 0) {
		if (dup2(pfd, 0) == -1)
			eprintf("dup2:");
		close(pfd);
	}

	if (!password) {
		if (isatty(0)) {
			char *p = getpass("Password: ");
			if (!p)
				eprintf("getpass failed\n");
			estrlcpy(passbuf, p, sizeof(passbuf));
			password = passbuf;
		} else {
			/* read from stdin */
			int bytes = read(0, passbuf, sizeof(passbuf) - 1);
			if (bytes < 0)
				eprintf("read stdin:");
			/* strip trailing newline or cr */
			while (bytes > 0 && (passbuf[bytes - 1] == '\n' || passbuf[bytes - 1] == '\r'))
				bytes--;
			passbuf[bytes] = '\0';
			password = passbuf;
		}
	}

	cryptpass = crypt(password, salt);
	if (!cryptpass)
		eprintf("crypt:");
	printf("%s\n", cryptpass);

	if (fshut(stdout, "<stdout>"))
		return 2;
	return 0;
}
