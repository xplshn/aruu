/* see LICENSE file for copyright and license details */

#include "arg.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static int keep_running = 1;

static void
usage(void)
{
  eprintf(
      "usage: %s [-c count] [-i interval] [-s size] [-t ttl] [-w "
      "deadline] [-q] host\n",
      argv0
  );
}

static void
sigint_handler(int sig)
{
  (void)sig;
  keep_running = 0;
}

static unsigned short
checksum(unsigned short *addr, int len)
{
  unsigned long sum = 0;

  while (len > 1) {
    sum += *addr++;
    len -= 2;
  }
  if (len > 0)
    sum += *(unsigned char *)addr;
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return ~sum;
}

static void
send_ping(int sock, struct sockaddr_in *dst, int seq, int size)
{
  struct icmphdr *icmp;
  char           *packet;
  struct timeval  tv;
  int             packlen = sizeof(*icmp) + size;

  packet = emalloc(packlen);
  memset(packet, 0, packlen);

  icmp                   = (struct icmphdr *)packet;
  icmp->type             = ICMP_ECHO;
  icmp->code             = 0;
  icmp->un.echo.id       = htons(getpid() & 0xffff);
  icmp->un.echo.sequence = htons(seq);

  /* store timestamp in payload if size is large enough */
  if (size >= (int)sizeof(struct timeval)) {
    gettimeofday(&tv, NULL);
    memcpy(packet + sizeof(*icmp), &tv, sizeof(tv));
  }

  icmp->checksum = checksum((unsigned short *)packet, packlen);

  if (sendto(sock, packet, packlen, 0, (struct sockaddr *)dst, sizeof(*dst)) < 0)
    eprintf("sendto:");

  free(packet);
}

static double
get_time_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static void
print_stats(
    const char *host, int sent, int received, double min_rtt, double max_rtt, double sum_rtt
)
{
  printf("\n--- %s ping statistics ---\n", host);
  printf(
      "%d packets transmitted, %d received, %d%% packet loss\n",
      sent,
      received,
      sent > 0 ? (sent - received) * 100 / sent : 0
  );
  if (received > 0) {
    printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n", min_rtt, sum_rtt / received, max_rtt);
  }
}

// ?man ping: send icmp echo requests
// ?man arguments: host
// ?man send icmp echo requests to verify network connectivity
int
main(int argc, char *argv[])
{
  struct addrinfo    hints, *res;
  struct sockaddr_in dst;
  struct sockaddr_in from;
  struct pollfd      pfd;
  struct timeval     sent_tv;
  struct icmphdr    *icmp;
  char              *cflag = NULL;
  char              *iflag = NULL;
  char              *sflag = NULL;
  char              *tflag = NULL;
  char              *wflag = NULL;
  char              *host;
  char              *rxbuf;
  int                qflag    = 0;
  int                count    = 0;
  int                size     = 56;
  int                ttl      = 0;
  int                deadline = 0;
  int                sock     = -1;
  int                is_raw   = 1;
  int                seq      = 1;
  int                sent     = 0;
  int                received = 0;
  int                r, optval, p_res, hlen;
  double             interval = 1.0;
  double             min_rtt  = 999999.0;
  double             max_rtt  = 0.0;
  double             sum_rtt  = 0.0;
  double    start_time, deadline_ms, next_send_ms, now, timeout_ms, time_to_deadline, rtt, sent_ms;
  ssize_t   n;
  socklen_t fromlen;

  ARGBEGIN
  {
    // ?man -c:str: print count or perform stdout action
    case 'c':
      cflag = EARGF(usage());
      break;
    // ?man -i:str: interactive mode or prompt for confirmation
    case 'i':
      iflag = EARGF(usage());
      break;
    // ?man -s:str: silent mode or print summary
    case 's':
      sflag = EARGF(usage());
      break;
    // ?man -t:str: sort or specify timestamp
    case 't':
      tflag = EARGF(usage());
      break;
    // ?man -w:str: wait for completion
    case 'w':
      wflag = EARGF(usage());
      break;
    // ?man -q: quiet mode; suppress output
    case 'q':
      qflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (argc < 1)
    usage();

  host = argv[0];

  if (cflag)
    count = estrtonum(cflag, 1, 1000000);
  if (iflag)
    interval = estrtod(iflag);
  if (sflag)
    size = estrtonum(sflag, 0, 65507);
  if (tflag)
    ttl = estrtonum(tflag, 1, 255);
  if (wflag)
    deadline = estrtonum(wflag, 1, 1000000);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_RAW;

  r = getaddrinfo(host, NULL, &hints, &res);
  if (r != 0)
    eprintf("getaddrinfo %s: %s\n", host, gai_strerror(r));

  memcpy(&dst, res->ai_addr, sizeof(dst));
  freeaddrinfo(res);

  /* try opening raw icmp socket first, fallback to dgram */
  sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (sock < 0)
      eprintf("socket:");
    is_raw = 0;
  }

  if (ttl > 0) {
    if (setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0)
      eprintf("setsockopt IP_TTL:");
  }

  /* request that recvfrom returns the ttl value */
  optval = 1;
  if (is_raw) {
    setsockopt(sock, IPPROTO_IP, IP_RECVTTL, &optval, sizeof(optval));
  }

  if (!qflag) {
    printf("PING %s (%s) %d bytes of data.\n", host, inet_ntoa(dst.sin_addr), size);
  }

  signal(SIGINT, sigint_handler);

  pfd.fd     = sock;
  pfd.events = POLLIN;

  start_time   = get_time_ms();
  deadline_ms  = deadline > 0 ? start_time + deadline * 1000.0 : 0.0;
  next_send_ms = start_time;

  rxbuf = emalloc(size + 1024);

  while (keep_running) {
    now = get_time_ms();

    if (deadline > 0 && now >= deadline_ms)
      break;

    if (count > 0 && sent >= count && received >= sent)
      break;

    if (now >= next_send_ms && (count == 0 || sent < count)) {
      send_ping(sock, &dst, seq++, size);
      sent++;
      next_send_ms = now + interval * 1000.0;
    }

    timeout_ms = next_send_ms - now;
    if (deadline > 0) {
      time_to_deadline = deadline_ms - now;
      if (time_to_deadline < timeout_ms)
        timeout_ms = time_to_deadline;
    }
    if (timeout_ms < 0)
      timeout_ms = 0;

    if (count > 0 && sent >= count)
      timeout_ms = 2000.0;

    p_res = poll(&pfd, 1, (int)timeout_ms);
    if (p_res < 0) {
      if (errno == EINTR)
        continue;
      eprintf("poll:\n");
    }

    if (p_res > 0 && (pfd.revents & POLLIN)) {
      fromlen = sizeof(from);
      n       = recvfrom(sock, rxbuf, size + 1024, 0, (struct sockaddr *)&from, &fromlen);
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        eprintf("recvfrom:\n");
      }

      hlen = 0;
      if (is_raw) {
        struct ip *ip = (struct ip *)rxbuf;
        hlen          = ip->ip_hl << 2;
        if (n < hlen + (int)sizeof(*icmp))
          continue;
      }
      icmp = (struct icmphdr *)(rxbuf + hlen);

      if (icmp->type == ICMP_ECHOREPLY
          && (!is_raw || ntohs(icmp->un.echo.id) == (getpid() & 0xffff))) {
        received++;
        rtt = get_time_ms();
        if (n - hlen - (int)sizeof(*icmp) >= (int)sizeof(struct timeval)) {
          memcpy(&sent_tv, rxbuf + hlen + sizeof(*icmp), sizeof(sent_tv));
          sent_ms = (double)sent_tv.tv_sec * 1000.0 + (double)sent_tv.tv_usec / 1000.0;
          rtt -= sent_ms;
          if (rtt < min_rtt)
            min_rtt = rtt;
          if (rtt > max_rtt)
            max_rtt = rtt;
          sum_rtt += rtt;
          if (!qflag) {
            printf(
                "%d bytes from %s: "
                "icmp_seq=%d ttl=%d "
                "time=%.3f ms\n",
                (int)n,
                inet_ntoa(from.sin_addr),
                ntohs(icmp->un.echo.sequence),
                is_raw ? ((struct ip *)rxbuf)->ip_ttl : 64,
                rtt
            );
          }
        } else {
          if (!qflag) {
            printf(
                "%d bytes from %s: "
                "icmp_seq=%d ttl=%d\n",
                (int)n,
                inet_ntoa(from.sin_addr),
                ntohs(icmp->un.echo.sequence),
                is_raw ? ((struct ip *)rxbuf)->ip_ttl : 64
            );
          }
        }
      }
    }
  }

  free(rxbuf);
  close(sock);

  print_stats(host, sent, received, min_rtt, max_rtt, sum_rtt);

  return received > 0 ? 0 : 1;
}
