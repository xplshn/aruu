/* See LICENSE file for copyright and license details. */

#include "arg.h"
#include "util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf("usage: %s [-e string] [-d string] [-v] [dir]\n", argv0);
}

static char *
url_decode(char *s)
{
  char        *r, *w;
  unsigned int val;

  for (r = w = s; *r;) {
    if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
      sscanf(r + 1, "%2x", &val);
      *w++ = val;
      r += 3;
    } else if (*r == '+') {
      *w++ = ' ';
      r++;
    } else {
      *w++ = *r++;
    }
  }
  *w = '\0';
  return s;
}

static char *
url_encode(const char *s)
{
  char       *buf = emalloc(strlen(s) * 3 + 1);
  char       *w   = buf;
  const char *r   = s;

  while (*r) {
    if (isalnum((unsigned char)*r) || strchr("-._~", *r)) {
      *w++ = *r++;
    } else {
      w += sprintf(w, "%%%02X", (unsigned char)*r);
      r++;
    }
  }
  *w = '\0';
  return buf;
}

static void
send_error(int status, const char *msg)
{
  printf(
      "HTTP/1.1 %d %s\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n"
      "Connection: close\r\n\r\n",
      status,
      msg
  );
  printf(
      "<html><head><title>%d %s</title></head>"
      "<body><h3>%d %s</h3></body></html>\n",
      status,
      msg,
      status,
      msg
  );
}

static int
is_under(const char *file, const char *dir)
{
  char  *rfile = realpath(file, NULL);
  char  *rdir  = realpath(dir, NULL);
  int    rc    = 0;
  size_t len;

  if (rfile && rdir) {
    len = strlen(rdir);
    if (strncmp(rfile, rdir, len) == 0) {
      if (rfile[len] == '\0' || rfile[len] == '/' || (len > 0 && rdir[len - 1] == '/'))
        rc = 1;
    }
  }
  free(rfile);
  free(rdir);
  return rc;
}

static const char *
get_mime_type(const char *file)
{
  const char *ext = strrchr(file, '.');
  if (!ext)
    return "application/octet-stream";
  ext++;

  if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0)
    return "text/html; charset=UTF-8";
  if (strcasecmp(ext, "css") == 0)
    return "text/css";
  if (strcasecmp(ext, "js") == 0)
    return "application/javascript";
  if (strcasecmp(ext, "png") == 0)
    return "image/png";
  if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
    return "image/jpeg";
  if (strcasecmp(ext, "gif") == 0)
    return "image/gif";
  if (strcasecmp(ext, "txt") == 0)
    return "text/plain; charset=UTF-8";
  if (strcasecmp(ext, "pdf") == 0)
    return "application/pdf";
  if (strcasecmp(ext, "zip") == 0)
    return "application/zip";

  return "application/octet-stream";
}

static void
handle_connection(void)
{
  struct stat    st, st_idx;
  struct dirent *de;
  DIR           *dir;
  char           line[4096];
  char           method[32], path[2048], proto[32];
  char           index_path[sizeof(path) + 32];
  char           file_buf[8192];
  char           date_buf[64];
  const char    *mime_type;
  char          *query, *p, *enc;
  ssize_t        n_read;
  size_t         len;
  int            fd;

  if (!fgets(line, sizeof(line), stdin))
    return;

  if (sscanf(line, "%31s %2047s %31s", method, path, proto) != 3) {
    send_error(400, "Bad Request");
    return;
  }

  while (fgets(line, sizeof(line), stdin)) {
    if (line[0] == '\r' || line[0] == '\n')
      break;
  }

  if (strcasecmp(method, "GET") != 0) {
    send_error(501, "Not Implemented");
    return;
  }

  url_decode(path);

  query = strchr(path, '?');
  if (query) {
    *query = '\0';
    setenv("QUERY_STRING", query + 1, 1);
  } else {
    unsetenv("QUERY_STRING");
  }

  p = path;
  while (*p == '/')
    p++;
  if (*p == '\0')
    p = ".";

  if (stat(p, &st) < 0) {
    send_error(404, "Not Found");
    return;
  }

  if (!is_under(p, ".")) {
    send_error(403, "Forbidden");
    return;
  }

  if (S_ISDIR(st.st_mode)) {
    len = strlen(path);
    if (len > 0 && path[len - 1] != '/') {
      printf(
          "HTTP/1.1 302 Found\r\n"
          "Location: %s/\r\n"
          "Connection: close\r\n\r\n",
          path
      );
      return;
    }

    snprintf(index_path, sizeof(index_path), "%s/index.html", p);
    if (stat(index_path, &st_idx) == 0 && S_ISREG(st_idx.st_mode)) {
      p  = index_path;
      st = st_idx;
      goto serve_file;
    }

    dir = opendir(p);
    if (!dir) {
      send_error(403, "Forbidden");
      return;
    }

    printf(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n\r\n"
    );
    printf("<html><head><title>Index of %s</title></head><body>\n", path);
    printf("<h3>Index of %s</h3><hr><pre>\n", path);

    while ((de = readdir(dir))) {
      if (strcmp(de->d_name, ".") == 0)
        continue;
      enc = url_encode(de->d_name);
      printf("<a href=\"%s\">%s</a>\n", enc, de->d_name);
      free(enc);
    }
    printf("</pre><hr></body></html>\n");
    closedir(dir);
    return;
  }

serve_file:
  fd = open(p, O_RDONLY);
  if (fd < 0) {
    send_error(403, "Forbidden");
    return;
  }

  mime_type = get_mime_type(p);
  strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&st.st_mtime));

  printf(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %lld\r\n"
      "Last-Modified: %s\r\n"
      "Connection: close\r\n\r\n",
      mime_type,
      (long long)st.st_size,
      date_buf
  );

  while ((n_read = read(fd, file_buf, sizeof(file_buf))) > 0) {
    writeall(1, file_buf, n_read);
  }
  close(fd);
}

// ?man httpd: simple http daemon
// ?man arguments: dir
// ?man serve static files over http
int
main(int argc, char *argv[])
{
  char *eflag = NULL;
  char *dflag = NULL;
  char *enc;

  ARGBEGIN
  {
    // ?man -e:str: specify expression or pattern
    case 'e':
      eflag = EARGF(usage());
      break;
    // ?man -d:str: specify directory
    case 'd':
      dflag = EARGF(usage());
      break;
    // ?man -v: verbose mode; show progress
    case 'v':
      break;
    default:
      usage();
  }
  ARGEND

  if (eflag) {
    enc = url_encode(eflag);
    printf("%s\n", enc);
    free(enc);
    return 0;
  }

  if (dflag) {
    printf("%s\n", url_decode(dflag));
    return 0;
  }

  if (argc > 0) {
    if (chdir(argv[0]) < 0)
      eprintf("chdir %s:\n", argv[0]);
  }

  handle_connection();
  return 0;
}
