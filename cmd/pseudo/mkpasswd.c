/* See LICENSE file for copyright and license details. */
#include "passwd.h"
#include "paths.h"
#include "util.h"

#include <ctype.h>
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
        char *mflag, *sflag, *parg, *password, *cryptpass;
        char *prefix;
        char salt[128];
        char passbuf[1024];
        int pfd, len;

        mflag = NULL;
        sflag = NULL;
        parg = NULL;
        password = NULL;
        cryptpass = NULL;
        pfd = -1;
        len = 2;
        prefix = NULL;

        ARGBEGIN {
        case 'P':
                // ?man -P:fd: read password from file descriptor
                parg = EARGF(usage());
                pfd = estrtonum(parg, 0, INT_MAX);
                break;
        case 'm':
                // ?man -m:type: encryption method (des, md5, sha256, sha512)
                mflag = EARGF(usage());
                break;
        case 'S':
                // ?man -S:salt: salt to use
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

        if (!mflag)
                mflag = "des";

        if (strcasecmp(mflag, "des") == 0) {
                len = 2;
                prefix = NULL;
        } else if (strcasecmp(mflag, "md5") == 0) {
                len = 8;
                prefix = "$1$";
        } else if (strcasecmp(mflag, "sha256") == 0) {
                len = 16;
                prefix = "$5$";
        } else if (strcasecmp(mflag, "sha512") == 0) {
                len = 16;
                prefix = "$6$";
        } else {
                eprintf("bad method: %s\n", mflag);
        }

        if (sflag) {
                char *s = sflag;
                while (*s) {
                        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '/')
                                eprintf("bad SALT (need [a-zA-Z0-9./])\n");
                        s++;
                }
                if (prefix)
                        snprintf(salt, sizeof(salt), "%s%s", prefix, sflag);
                else
                        estrlcpy(salt, sflag, sizeof(salt));
        } else {
                if (pw_gensalt_cipher(salt, sizeof(salt), prefix, len) < 0)
                        eprintf("pw_gensalt_cipher:");
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
                        int bytes = read(0, passbuf, sizeof(passbuf) - 1);
                        if (bytes < 0)
                                eprintf("read stdin:");
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
