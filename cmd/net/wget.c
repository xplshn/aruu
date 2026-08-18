/* see license file for copyright and license details */

#include "arg.h"
#include "tls.h"
#include "util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct Stream {
  struct TlsSocket *ts;
  char              buf[8192];
  size_t            len;
  size_t            idx;
};

static int    qflag                = 0;
static int    Sflag                = 0;
static int    cflag                = 0;
static int    spider               = 0;
static int    no_check_certificate = 0;
static int    timeout_sec          = 900;
static int    tries                = 20;
static char  *Pflag                = NULL;
static char  *Oflag                = NULL;
static char  *user_agent           = "wget/aruu";
static char  *post_data            = NULL;
static char  *post_file            = NULL;
static char **custom_headers       = NULL;
static size_t custom_headers_num   = 0;

static void
usage(void)
{
  eprintf(
      "usage: %s [-cqS] [-O file] [-P dir] [-T timeout] [-t tries] [-U "
      "user_agent] "
      "[-post-data data] [-post-file file] [-header header] "
      "[-no-check-certificate] [-spider] url\n",
      argv0
  );
}

static void
add_header(const char *hdr)
{
  custom_headers = ereallocarray(custom_headers, custom_headers_num + 1, sizeof(*custom_headers));
  custom_headers[custom_headers_num++] = estrdup(hdr);
}

static int
dial(const char *host, const char *port)
{
  struct addrinfo hints, *res, *rp;
  int             fd = -1, r;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  r = getaddrinfo(host, port, &hints, &res);
  if (r != 0) {
    if (!qflag)
      weprintf("getaddrinfo %s:%s: %s\n", host, port, gai_strerror(r));
    return -1;
  }

  for (rp = res; rp; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (fd < 0)
      continue;
    if (timeout_sec > 0) {
      struct timeval tv;
      tv.tv_sec  = timeout_sec;
      tv.tv_usec = 0;
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
      break;
    close(fd);
    fd = -1;
  }

  freeaddrinfo(res);
  return fd;
}

static void
parse_url(char *url, char **host, char **port, char **path, int *is_tls)
{
  char *p, *ss;

  *is_tls = 0;
  if (strncasecmp(url, "http:// ", 7) == 0) {
    url += 7;
  } else if (strncasecmp(url, "https:// ", 8) == 0) {
    url += 8;
    *is_tls = 1;
  } else {
    eprintf("unsupported protocol or invalid url: %s\n", url);
  }

  *host = url;
  p     = strchr(url, '/');
  if (p) {
    *p    = '\0';
    *path = p + 1;
  } else {
    *path = "";
  }

  /* handle ipv6 brackets or host:port */
  if (**host == '[') {
    (*host)++;
    ss = strchr(*host, ']');
    if (ss) {
      *ss = '\0';
      ss++;
      if (*ss == ':')
        *port = ss + 1;
      else
        *port = *is_tls ? "443" : "80";
    } else {
      eprintf("invalid ipv6 literal: %s\n", *host);
    }
  } else {
    p = strrchr(*host, ':');
    if (p) {
      *p    = '\0';
      *port = p + 1;
    } else {
      *port = *is_tls ? "443" : "80";
    }
  }
}

static char *
find_header(const char *headers, const char *name)
{
  const char *p;
  size_t      len = strlen(name);

  p = headers;
  while (p && *p) {
    if (strncasecmp(p, name, len) == 0) {
      p += len;
      while (*p == ' ' || *p == '\t')
        p++;
      len = strcspn(p, "\r\n");
      return estrndup(p, len);
    }
    p = strchr(p, '\n');
    if (p)
      p++;
  }
  return NULL;
}

static int
stream_getc(struct Stream *s)
{
  ssize_t r;

  if (s->idx < s->len) {
    return (unsigned char)s->buf[s->idx++];
  }
  s->idx = 0;
  r      = tlss_read(s->ts, s->buf, sizeof(s->buf));
  if (r <= 0) {
    s->len = 0;
    return EOF;
  }
  s->len = (size_t)r;
  return (unsigned char)s->buf[s->idx++];
}

static size_t
stream_read(struct Stream *s, void *ptr, size_t size)
{
  size_t  total = 0;
  size_t  n;
  char   *p = ptr;
  ssize_t r;

  while (total < size) {
    if (s->idx < s->len) {
      n = MIN(size - total, s->len - s->idx);
      memcpy(p + total, s->buf + s->idx, n);
      s->idx += n;
      total += n;
    } else {
      s->idx = 0;
      r      = tlss_read(s->ts, s->buf, sizeof(s->buf));
      if (r <= 0) {
        s->len = 0;
        break;
      }
      s->len = (size_t)r;
    }
  }
  return total;
}

static void
read_chunked(struct Stream *s, int out_fd)
{
  char      line[128];
  char      chunk_buf[8192];
  size_t    line_len, n;
  long long chunk_size, remaining;
  int       c;

  for (;;) {
    line_len = 0;
    for (;;) {
      c = stream_getc(s);
      if (c == EOF)
        eprintf(
            "unexpected end of file reading chunk "
            "size\n"
        );
      if (c == '\n') {
        line[line_len] = '\0';
        break;
      }
      if (c != '\r' && line_len < sizeof(line) - 1) {
        line[line_len++] = c;
      }
    }

    chunk_size = strtoll(line, NULL, 16);
    if (chunk_size == 0) {
      stream_getc(s);
      stream_getc(s);
      break;
    }

    remaining = chunk_size;
    while (remaining > 0) {
      n = stream_read(s, chunk_buf, MIN(remaining, (long long)sizeof(chunk_buf)));
      if (n == 0)
        eprintf(
            "unexpected end of file in chunk "
            "data\n"
        );
      if (writeall(out_fd, chunk_buf, n) < 0)
        eprintf("write output:\n");
      remaining -= n;
    }

    stream_getc(s);
    stream_getc(s);
  }
}

static void
read_non_chunked(struct Stream *s, int out_fd, long long content_len)
{
  char      chunk_buf[8192];
  long long remaining = content_len;
  size_t    n, to_read;

  while (content_len < 0 || remaining > 0) {
    to_read = sizeof(chunk_buf);
    if (content_len >= 0)
      to_read = (size_t)MIN(remaining, (long long)sizeof(chunk_buf));
    n = stream_read(s, chunk_buf, to_read);
    if (n == 0) {
      if (content_len >= 0)
        eprintf("unexpected end of file\n");
      break;
    }
    if (writeall(out_fd, chunk_buf, n) < 0)
      eprintf("write output:\n");
    if (content_len >= 0)
      remaining -= n;
  }
}

static void
req_printf(struct TlsSocket *ts, const char *fmt, ...)
{
  va_list ap;
  char    buf[1024];
  int     len;

  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (len > 0)
    tlss_write(ts, buf, len);
}

// ?man wget: retrieve files from the web
// ?man arguments: url
// ?man download files over http or https
int
main(int argc, char *argv[])
{
  struct Stream     s;
  char             *url, *host, *port, *path, *loc;
  char             *curr_host, *curr_port, *curr_path;
  char             *new_url;
  char             *cl_str;
  char             *te_str;
  char             *header_end;
  char             *out_name;
  int               redirects     = 0;
  int               max_redirects = 20;
  int               sock_fd       = -1;
  int               out_fd        = 1;
  int               chunked;
  int               status;
  long long         content_len;
  size_t            total_read;
  ssize_t           n;
  size_t            dir_len;
  char             *last_slash;
  int               is_tls        = 0;
  struct TlsSocket *tls_sock      = NULL;
  off_t             resume_offset = 0;
  int               out_mode      = O_WRONLY | O_CREAT | O_TRUNC;
  long long         post_len      = 0;
  int               post_fd       = -1;
  size_t            i;
  int               attempt;
  int               max_tries;

  ARGBEGIN
  {
    // ?man -O:str: specify output file path
    case 'O':
      Oflag = EARGF(usage());
      break;
    // ?man -P:str: specify output directory prefix
    case 'P':
      Pflag = EARGF(usage());
      break;
    // ?man -T:num: set network read and connect timeout
    case 'T':
      timeout_sec = estrtonum(EARGF(usage()), 0, 100000);
      break;
    // ?man -t:num: set number of connection retries, 0 retries forever
    case 't':
      tries = estrtonum(EARGF(usage()), 0, 1000000);
      break;
    // ?man -U:str: set User-Agent header
    case 'U':
      user_agent = EARGF(usage());
      break;
    // ?man -c: continue retrieval of aborted transfer
    case 'c':
      cflag = 1;
      break;
    // ?man -q: quiet mode to suppress stderr output
    case 'q':
      qflag = 1;
      break;
    // ?man -S: print server response headers to stderr
    case 'S':
      Sflag = 1;
      break;
    // ?man --: specify - option
    case '-':
      if (strcmp(argv[0], "-no-check-certificate") == 0) {
        no_check_certificate = 1;
        brk_                 = 1;
      } else if (strncmp(argv[0], "-header=", 8) == 0) {
        add_header(argv[0] + 8);
        brk_ = 1;
      } else if (strcmp(argv[0], "-header") == 0) {
        brk_ = 1;
        if (!argv[1])
          usage();
        add_header(argv[1]);
        argv++;
        argc--;
      } else if (strncmp(argv[0], "-post-data=", 11) == 0) {
        post_data = argv[0] + 11;
        brk_      = 1;
      } else if (strcmp(argv[0], "-post-data") == 0) {
        brk_ = 1;
        if (!argv[1])
          usage();
        post_data = argv[1];
        argv++;
        argc--;
      } else if (strncmp(argv[0], "-post-file=", 11) == 0) {
        post_file = argv[0] + 11;
        brk_      = 1;
      } else if (strcmp(argv[0], "-post-file") == 0) {
        brk_ = 1;
        if (!argv[1])
          usage();
        post_file = argv[1];
        argv++;
        argc--;
      } else if (strcmp(argv[0], "-spider") == 0) {
        spider = 1;
        brk_   = 1;
      } else {
        usage();
      }
      break;
    default:
      usage();
  }
  ARGEND

  if (argc < 1)
    usage();

  url = estrdup(argv[0]);

  /* determine output filename early to check for resume */
  out_name = NULL;
  if (Oflag) {
    out_name = Oflag;
  } else {
    last_slash = strrchr(url, '/');
    if (last_slash && *(last_slash + 1))
      out_name = last_slash + 1;
    else
      out_name = "index.html";

    if (Pflag) {
      char *tmp = emalloc(strlen(Pflag) + 1 + strlen(out_name) + 1);
      sprintf(tmp, "%s/%s", Pflag, out_name);
      out_name = tmp;
    }
  }

  if (cflag && out_name && strcmp(out_name, "-") != 0) {
    struct stat st;
    if (stat(out_name, &st) == 0 && S_ISREG(st.st_mode)) {
      resume_offset = st.st_size;
    }
  }

  if (post_data) {
    post_len = strlen(post_data);
  } else if (post_file) {
    struct stat st;
    post_fd = open(post_file, O_RDONLY);
    if (post_fd < 0)
      eprintf("open %s:\n", post_file);
    if (fstat(post_fd, &st) < 0)
      eprintf("stat %s:\n", post_file);
    post_len = st.st_size;
  }

  /* wgets own -t: 0 means retry forever */
  max_tries = tries == 0 ? INT_MAX : tries;

  while (!tls_sock) {
    if (redirects > max_redirects)
      eprintf("too many redirects\n");

    curr_host = curr_port = curr_path = NULL;
    parse_url(url, &curr_host, &curr_port, &curr_path, &is_tls);

    host = estrdup(curr_host);
    port = estrdup(curr_port);
    path = estrdup(curr_path);

    for (attempt = 1; attempt <= max_tries; attempt++) {
      sock_fd = dial(host, port);
      if (sock_fd < 0) {
        if (attempt == max_tries)
          eprintf("failed to connect to %s:%s\n", host, port);
        continue;
      }

      tls_sock = tlss_connect(sock_fd, host, !no_check_certificate, is_tls);
      if (!tls_sock) {
        close(sock_fd);
        if (attempt == max_tries)
          eprintf("failed to establish TLS connection with %s\n", host);
        continue;
      }

      break;
    }

    /* send http request */
    const char *method = spider ? "HEAD" : ((post_data || post_file) ? "POST" : "GET");
    req_printf(tls_sock, "%s /%s HTTP/1.1\r\n", method, path);
    req_printf(tls_sock, "Host: %s\r\n", host);
    req_printf(tls_sock, "User-Agent: %s\r\n", user_agent);
    req_printf(tls_sock, "Connection: close\r\n");

    if (resume_offset > 0) {
      req_printf(tls_sock, "Range: bytes=%lld-\r\n", (long long)resume_offset);
    }

    if (post_data || post_file) {
      int has_ct = 0;
      for (i = 0; i < custom_headers_num; i++) {
        if (strncasecmp(custom_headers[i], "Content-Type:", 13) == 0) {
          has_ct = 1;
          break;
        }
      }
      if (!has_ct) {
        req_printf(
            tls_sock,
            "Content-Type: "
            "application/"
            "x-www-form-urlencoded\r\n"
        );
      }
      req_printf(tls_sock, "Content-Length: %lld\r\n", post_len);
    }

    for (i = 0; i < custom_headers_num; i++) {
      req_printf(tls_sock, "%s\r\n", custom_headers[i]);
    }

    req_printf(tls_sock, "\r\n");

    if (post_data) {
      tlss_write(tls_sock, post_data, strlen(post_data));
    } else if (post_file) {
      char    io_buf[8192];
      ssize_t r;
      while ((r = read(post_fd, io_buf, sizeof(io_buf))) > 0) {
        if (tlss_write(tls_sock, io_buf, r) < 0) {
          eprintf("failed to write post data:\n");
        }
      }
      close(post_fd);
      post_fd = -1;
    }

    /* read headers */
    total_read = 0;
    header_end = NULL;
    memset(s.buf, 0, sizeof(s.buf));
    while (total_read < sizeof(s.buf) - 1) {
      n = tlss_read(tls_sock, s.buf + total_read, sizeof(s.buf) - 1 - total_read);
      if (n <= 0) {
        if (n < 0)
          eprintf("read socket:\n");
        else
          eprintf(
              "connection closed by "
              "server\n"
          );
      }
      total_read += n;
      s.buf[total_read] = '\0';
      header_end        = strstr(s.buf, "\r\n\r\n");
      if (header_end)
        break;
    }

    if (!header_end)
      eprintf("http header too large or not found\n");

    *header_end = '\0';
    s.ts        = tls_sock;
    s.len       = total_read;
    s.idx       = (header_end + 4) - s.buf;

    if (Sflag) {
      fprintf(stderr, "%s\n\n", s.buf);
    }

    if (strncasecmp(s.buf, "HTTP/1.1 ", 9) != 0 && strncasecmp(s.buf, "HTTP/1.0 ", 9) != 0) {
      eprintf("invalid http response: %s\n", s.buf);
    }
    status = atoi(s.buf + 9);

    if (status >= 300 && status < 400) {
      loc = find_header(s.buf, "Location:");
      if (!loc)
        eprintf(
            "redirect response without location "
            "header\n"
        );

      if (strncasecmp(loc, "http:// ", 7) == 0 || strncasecmp(loc, "https://", 8) == 0) {
        new_url = estrdup(loc);
      } else if (loc[0] == '/') {
        new_url = emalloc(8 + strlen(host) + strlen(port) + strlen(loc) + 2);
        sprintf(new_url, "%s:// %s:%s%s", is_tls ? "https" : "http", host, port, loc);
      } else {
        last_slash = strrchr(path, '/');
        dir_len    = 0;
        if (last_slash)
          dir_len = last_slash - path + 1;
        new_url = emalloc(8 + strlen(host) + strlen(port) + 1 + dir_len + strlen(loc) + 2);
        sprintf(new_url, "%s:// %s:%s/", is_tls ? "https" : "http", host, port);
        if (dir_len > 0)
          strncat(new_url, path, dir_len);
        strcat(new_url, loc);
      }

      free(loc);
      free(url);
      url = new_url;
      tlss_close(tls_sock, 1);
      tls_sock = NULL;
      redirects++;
    } else if (status == 206) {
      out_mode = O_WRONLY | O_CREAT | O_APPEND;
    } else if (status == 200) {
      out_mode = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (status == 416) {
      if (!qflag)
        weprintf(
            "file already fully retrieved or "
            "range invalid\n"
        );
      tlss_close(tls_sock, 1);
      free(url);
      free(host);
      free(port);
      free(path);
      return 0;
    } else {
      eprintf("server returned status: %d\n", status);
    }

    free(host);
    free(port);
    free(path);
  }

  if (spider) {
    tlss_close(tls_sock, 1);
    free(url);
    return 0;
  }

  cl_str      = find_header(s.buf, "Content-Length:");
  content_len = -1;
  if (cl_str) {
    content_len = strtoll(cl_str, NULL, 10);
    free(cl_str);
  }

  te_str  = find_header(s.buf, "Transfer-Encoding:");
  chunked = 0;
  if (te_str) {
    if (strcasecmp(te_str, "chunked") == 0)
      chunked = 1;
    free(te_str);
  }

  if (strcmp(out_name, "-") != 0) {
    out_fd = open(out_name, out_mode, 0644);
    if (out_fd < 0)
      eprintf("open %s:\n", out_name);
  }

  if (chunked)
    read_chunked(&s, out_fd);
  else
    read_non_chunked(&s, out_fd, content_len);

  tlss_close(tls_sock, 1);
  if (out_fd != 1)
    close(out_fd);
  if (Oflag != out_name && Pflag)
    free(out_name);
  free(url);

  for (i = 0; i < custom_headers_num; i++) {
    free(custom_headers[i]);
  }
  free(custom_headers);

  return 0;
}
