/* See LICENSE file for copyright and license details. */
#include "../passwd.h"
#include "../paths.h"
#include "../text.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../config.h"

#if defined(__linux__) || defined(__GLIBC__)
#include <crypt.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define HAVE_SHADOW 1
#include <shadow.h>
#endif

#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define HAVE_MASTER_PASSWD 1
#endif

int
pw_init(void)
{
        struct rlimit rlim;

        rlim.rlim_cur = 0;
        rlim.rlim_max = 0;
        if (setrlimit(RLIMIT_CORE, &rlim) < 0)
                eprintf("setrlimit:");
        return 0;
}

int
pw_check(const struct passwd *pw, const char *pass)
{
        char *cryptpass, *p;
        const char *stored;

        stored = pw->pw_passwd;
        if (stored[0] == '!' || stored[0] == '*') {
                weprintf("denied\n");
                return -1;
        }

        if (stored[0] == '\0') {
                if (pass[0] == '\0')
                        return 1;
                weprintf("incorrect password\n");
                return 0;
        }

#if defined(HAVE_SHADOW)
        if (stored[0] == 'x' && stored[1] == '\0') {
                struct spwd *spw;
                errno = 0;
                spw = getspnam(pw->pw_name);
                if (!spw) {
                        if (errno)
                                weprintf("getspnam: %s:", pw->pw_name);
                        else
                                weprintf("who are you?\n");
                        return -1;
                }
                stored = spw->sp_pwdp;
                if (stored[0] == '!' || stored[0] == '*') {
                        weprintf("denied\n");
                        return -1;
                }
        }
#endif

        cryptpass = crypt(pass, stored);
        if (!cryptpass) {
                weprintf("crypt:");
                return -1;
        }
        p = estrdup(cryptpass);
        if (strcmp(p, stored) != 0) {
                free(p);
                weprintf("incorrect password\n");
                return 0;
        }
        free(p);
        return 1;
}

int
pwdb_lookup(struct pwdb_entry *ent, const char *name)
{
        struct passwd *pw;
        const char *hash;

        errno = 0;
        pw = getpwnam(name);
        if (!pw) {
                if (errno)
                        weprintf("getpwnam: %s:", name);
                else
                        weprintf("who are you?\n");
                return -1;
        }

        ent->name = estrdup(pw->pw_name);
        ent->uid = pw->pw_uid;
        ent->gid = pw->pw_gid;
        hash = pw->pw_passwd;

#if defined(HAVE_SHADOW)
        if (hash[0] == 'x' && hash[1] == '\0') {
                struct spwd *spw;
                errno = 0;
                spw = getspnam(name);
                if (spw)
                        hash = spw->sp_pwdp;
        }
#endif

        ent->hash = estrdup(hash);
        return 0;
}

static int
fill_random(void *buf, size_t n)
{
#if defined(__linux__)
        if (getentropy(buf, n) == 0)
                return 0;
#endif
        {
                int fd;
                size_t off;
                fd = open(ARUU_PATH_DEVURANDOM, O_RDONLY);
                if (fd < 0) {
                        weprintf("open /dev/urandom:");
                        return -1;
                }
                off = 0;
                while (off < n) {
                        ssize_t r = read(fd, (char *)buf + off, n - off);
                        if (r <= 0) {
                                close(fd);
                                weprintf("read /dev/urandom:");
                                return -1;
                        }
                        off += (size_t)r;
                }
                close(fd);
        }
        return 0;
}

int
pw_gensalt_cipher(char *salt, size_t salt_sz, const char *prefix,
                  size_t rand_len)
{
        static const char b64[] =
                "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        unsigned char raw[32];
        char body[48];
        size_t i, n, body_len;
        unsigned v;
        int prefix_len;

        if (rand_len > sizeof(raw))
                rand_len = sizeof(raw);
        if (rand_len == 0) {
                salt[0] = '\0';
                return 0;
        }

        prefix_len = 0;
        if (prefix) {
                prefix_len = snprintf(salt, salt_sz, "%s", prefix);
                if (prefix_len < 0 || (size_t)prefix_len >= salt_sz) {
                        weprintf("snprintf:");
                        return -1;
                }
        }

        if (fill_random(raw, rand_len) < 0)
                return -1;

        body_len = (rand_len * 4 + 2) / 3;
        if (body_len >= sizeof(body))
                body_len = sizeof(body) - 1;
        n = 0;
        for (i = 0; i + 3 <= rand_len; i += 3) {
                v = (raw[i] << 16) | (raw[i+1] << 8) | raw[i+2];
                body[n++] = b64[v & 0x3f]; v >>= 6;
                body[n++] = b64[v & 0x3f]; v >>= 6;
                body[n++] = b64[v & 0x3f]; v >>= 6;
                body[n++] = b64[v & 0x3f];
        }
        if (i < rand_len) {
                v = raw[i] << 16;
                if (i + 1 < rand_len)
                        v |= raw[i+1] << 8;
                body[n++] = b64[v & 0x3f]; v >>= 6;
                body[n++] = b64[v & 0x3f]; v >>= 6;
                if (i + 1 < rand_len)
                        body[n++] = b64[v & 0x3f];
        }
        body[n] = '\0';

        if ((size_t)prefix_len + n + 1 >= salt_sz) {
                weprintf("salt buffer too small\n");
                return -1;
        }
        memcpy(salt + prefix_len, body, n + 1);
        return 0;
}

int
pw_gensalt(char *salt, size_t salt_sz)
{
        return pw_gensalt_cipher(salt, salt_sz, PW_CIPHER, 16);
}

#if defined(HAVE_SHADOW)
static int
update_shadow(const char *name, const char *newhash)
{
        struct spwd *spw, cur;
        FILE *fp, *tfp;
        int wrote = 0;
        char path[256];

        snprintf(path, sizeof(path), ARUU_PATH_ETC "/tcb/%s/shadow", name);
        fp = fopen(path, "r+");
        if (!fp) {
                memcpy(path, ARUU_PATH_SHADOW, sizeof(ARUU_PATH_SHADOW));
                fp = fopen(path, "r+");
        }
        if (!fp) {
                weprintf("fopen %s:", path);
                return -1;
        }

        tfp = tmpfile();
        if (!tfp) {
                weprintf("tmpfile:");
                fclose(fp);
                return -1;
        }

        while ((spw = getspent())) {
                cur = *spw;
                if (strcmp(cur.sp_namp, name) == 0) {
                        cur.sp_pwdp = (char *)newhash;
                        wrote = 1;
                }
                errno = 0;
                if (putspent(&cur, tfp) == -1) {
                        weprintf("putspent:");
                        fclose(tfp);
                        fclose(fp);
                        return -1;
                }
        }
        if (!wrote) {
                weprintf("shadow: no matching entry\n");
                fclose(tfp);
                fclose(fp);
                return -1;
        }
        fflush(tfp);
        rewind(tfp);
        rewind(fp);
        fconcat(tfp, "tmpfile", fp, "shadow");
        ftruncate(fileno(fp), ftell(tfp));
        fclose(tfp);
        {
                int fd = fileno(fp);
                if (fd >= 0)
                        fsync(fd);
        }
        fclose(fp);
        return 0;
}
#endif

static int
update_passwd_file(const char *path, const char *name, const char *newhash)
{
        struct passwd *pw, cur;
        FILE *fp, *tfp;
        int wrote = 0;

        fp = fopen(path, "r+");
        if (!fp) {
                weprintf("fopen %s:", path);
                return -1;
        }
        tfp = tmpfile();
        if (!tfp) {
                weprintf("tmpfile:");
                fclose(fp);
                return -1;
        }
        while ((pw = fgetpwent(fp))) {
                cur = *pw;
                if (strcmp(cur.pw_name, name) == 0) {
                        cur.pw_passwd = (char *)newhash;
                        wrote = 1;
                }
                errno = 0;
                if (putpwent(&cur, tfp) == -1) {
                        weprintf("putpwent:");
                        fclose(tfp);
                        fclose(fp);
                        return -1;
                }
        }
        if (!wrote) {
                weprintf("passwd: no matching entry\n");
                fclose(tfp);
                fclose(fp);
                return -1;
        }
        fflush(tfp);
        rewind(tfp);
        rewind(fp);
        fconcat(tfp, "tmpfile", fp, path);
        ftruncate(fileno(fp), ftell(tfp));
        {
                int fd = fileno(fp);
                if (fd >= 0)
                        fsync(fd);
        }
        fclose(tfp);
        fclose(fp);
        return 0;
}

#if defined(HAVE_MASTER_PASSWD)
static int
update_master_passwd(const char *name, const char *newhash)
{
        int r;
        pid_t pid;
        int st;

        r = update_passwd_file(ARUU_BSD_PATH_MASTER_PASSWD, name, newhash);
        if (r != 0)
                return r;
        pid = fork();
        if (pid == 0) {
                execl(ARUU_BSD_PATH_PWD_MKDB, "pwd_mkdb", "-p", NULL);
                _exit(127);
        }
        if (pid > 0)
                waitpid(pid, &st, 0);
        return 0;
}
#endif

int
pwdb_update(const char *name, const char *newhash)
{
#if defined(HAVE_SHADOW)
        {
                struct spwd *spw;
                errno = 0;
                spw = getspnam(name);
                if (spw)
                        return update_shadow(name, newhash);
        }
#elif defined(HAVE_MASTER_PASSWD)
        return update_master_passwd(name, newhash);
#endif
        return update_passwd_file(ARUU_PATH_PASSWD, name, newhash);
}
