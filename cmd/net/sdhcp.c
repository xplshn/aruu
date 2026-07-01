/* sdhcp: simple DHCP client, ported from git.2f30.org/sdhcp */
/* See LICENSE file for copyright and license details */
#include "arg.h"
#include "wexec.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

/* bootp packet layout: fields are byte arrays to avoid alignment/endian issues */
struct Bootp {
        unsigned char op      [1];
        unsigned char htype   [1];
        unsigned char hlen    [1];
        unsigned char hops    [1];
        unsigned char xid     [4];
        unsigned char secs    [2];
        unsigned char flags   [2];
        unsigned char ciaddr  [4];
        unsigned char yiaddr  [4];
        unsigned char siaddr  [4];
        unsigned char giaddr  [4];
        unsigned char chaddr  [16];
        unsigned char sname   [64];
        unsigned char file    [128];
        unsigned char magic   [4];
        unsigned char optdata [312-4];
};

enum {
        DHCP_DISCOVER =       1,
        DHCP_OFFER,
        DHCP_REQUEST,
        DHCP_DECLINE,
        DHCP_ACK,
        DHCP_NAK,
        DHCP_RELEASE,
        DHCP_INFORM,
        /* internal sentinel values returned by dhcprecv */
        TIMEOUT0 =          200,
        TIMEOUT1,
        TIMEOUT2,

        BOOT_REQUEST =        1,
        BOOT_REPLY =          2,
        /* bootp flags */
        F_BROADCAST =   1 << 15,

        /* bootp options */
        OB_PAD =              0,
        OB_MASK =             1,
        OB_ROUTER =           3,
        OB_NAMESERVER =       5,
        OB_DNSSERVER =        6,
        OB_HOSTNAME =        12,
        OB_BADDR =           28,
        /* DHCP-specific options */
        OD_IPADDR =          50, /* 0x32 */
        OD_LEASE =           51,
        OD_OVERLOAD =        52,
        OD_TYPE =            53, /* 0x35 */
        OD_SERVERID =        54, /* 0x36 */
        OD_PARAMS =          55, /* 0x37 */
        OD_MESSAGE =         56,
        OD_MAXMSG =          57,
        OD_RENEWALTIME =     58,
        OD_REBINDINGTIME =   59,
        OD_VENDORCLASS =     60,
        OD_CLIENTID =        61, /* 0x3d */
        OD_TFTPSERVER =      66,
        OD_BOOTFILE =        67,
        OB_END =            255,
};

enum { BROADCAST, UNICAST };

static struct Bootp bp;
static unsigned char magic[] = { 99, 130, 83, 99 };

/* conf */
static unsigned char xid[sizeof(bp.xid)];
static unsigned char hwaddr[16];
static char hostname[HOST_NAME_MAX + 1];
static time_t starttime;
static char *ifname = "eth0";
static unsigned char cid[16];
static char *program = "";
static int sock, timers[3];
/* negotiated lease values */
static unsigned char server[4];
static unsigned char client[4];
static unsigned char mask[4];
static unsigned char router[4];
static unsigned char dns[4];

/* flags: default on=1, foreground=0 */
static int dflag = 1;
static int iflag = 1;
static int fflag = 0;

#define IP(a, b, c, d) (unsigned char[4]){ a, b, c, d }

/* write src as n-byte big-endian into dst */
static void
hnput(unsigned char *dst, uint32_t src, size_t n)
{
        unsigned int i;

        for (i = 0; n--; i++)
                dst[i] = (src >> (n * 8)) & 0xff;
}

static struct sockaddr *
iptoaddr(struct sockaddr *ifaddr, unsigned char ip[4], int port)
{
        struct sockaddr_in *in = (struct sockaddr_in *)ifaddr;

        in->sin_family = AF_INET;
        in->sin_port = htons(port);
        memcpy(&(in->sin_addr), ip, sizeof(in->sin_addr));

        return ifaddr;
}

/* sendto UDP wrapper: sends n bytes of data to bootp server port on ip */
static ssize_t
udpsend(unsigned char ip[4], int fd, void *data, size_t n)
{
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        ssize_t sent;

        iptoaddr(&addr, ip, 67); /* bootp server */
        if ((sent = sendto(fd, data, n, 0, &addr, addrlen)) == -1)
                eprintf("sendto:");

        return sent;
}

/* recvfrom UDP wrapper: receives into data from bootp client port */
static ssize_t
udprecv(unsigned char ip[4], int fd, void *data, size_t n)
{
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        ssize_t r;

        iptoaddr(&addr, ip, 68); /* bootp client */
        if ((r = recvfrom(fd, data, n, 0, &addr, &addrlen)) == -1)
                eprintf("recvfrom:");

        return r;
}

static void
setip(unsigned char ip[4], unsigned char mask[4], unsigned char gateway[4])
{
        struct ifreq ifreq;
        struct rtentry rtreq;
        int fd;

        memset(&ifreq, 0, sizeof(ifreq));
        memset(&rtreq, 0, sizeof(rtreq));

        strlcpy(ifreq.ifr_name, ifname, IF_NAMESIZE);
        iptoaddr(&(ifreq.ifr_addr), ip, 0);
        if ((fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1)
                eprintf("can't set ip, socket:");
        ioctl(fd, SIOCSIFADDR, &ifreq);
        iptoaddr(&(ifreq.ifr_netmask), mask, 0);
        ioctl(fd, SIOCSIFNETMASK, &ifreq);
        ifreq.ifr_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST | IFF_MULTICAST;
        ioctl(fd, SIOCSIFFLAGS, &ifreq);
        /* default gw */
        rtreq.rt_flags = RTF_UP | RTF_GATEWAY;
        iptoaddr(&(rtreq.rt_gateway), gateway, 0);
        iptoaddr(&(rtreq.rt_genmask), IP(0, 0, 0, 0), 0);
        iptoaddr(&(rtreq.rt_dst),     IP(0, 0, 0, 0), 0);
        ioctl(fd, SIOCADDRT, &rtreq);

        close(fd);
}

/* copy contents of src path into dfd; silently skips missing files */
static void
appendfile(int dfd, char *src)
{
        char buf[BUFSIZ];
        int fd, n;

        if ((fd = open(src, O_RDONLY)) == -1)
                return;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
                writeall(dfd, buf, n);
        close(fd);
}

static void
setdns(unsigned char ip[4])
{
        char buf[128];
        int fd, n;

        if ((fd = creat("/etc/resolv.conf", 0644)) == -1) {
                weprintf("can't change /etc/resolv.conf:");
                return;
        }
        appendfile(fd, "/etc/resolv.conf.head");
        n = snprintf(buf, sizeof(buf), "\nnameserver %d.%d.%d.%d\n",
            ip[0], ip[1], ip[2], ip[3]);
        if (n > 0)
                writeall(fd, buf, n);
        appendfile(fd, "/etc/resolv.conf.tail");
        close(fd);
}

/* scan b's option data for opt; copy up to n bytes into data */
static void
optget(struct Bootp *b, void *data, int opt, int n)
{
        unsigned char *p = b->optdata;
        unsigned char *top = ((unsigned char *)b) + sizeof(*b);
        int code, len;

        while (p < top) {
                code = *p++;
                if (code == OB_PAD)
                        continue;
                if (code == OB_END || p == top)
                        break;
                len = *p++;
                if (len > top - p)
                        break;
                if (code == opt) {
                        memcpy(data, p, MIN(len, n));
                        break;
                }
                p += len;
        }
}

/* append a raw-bytes option to p; return new write cursor */
static unsigned char *
optput(unsigned char *p, int opt, unsigned char *data, size_t len)
{
        *p++ = opt;
        *p++ = (unsigned char)len;
        memcpy(p, data, len);

        return p + len;
}

/* append a big-endian numeric option to p; return new write cursor */
static unsigned char *
hnoptput(unsigned char *p, int opt, uint32_t data, size_t len)
{
        *p++ = opt;
        *p++ = (unsigned char)len;
        hnput(p, data, len);

        return p + len;
}

static void
dhcpsend(int type, int how)
{
        unsigned char *ip, *p;

        memset(&bp, 0, sizeof(bp));
        hnput(bp.op,    BOOT_REQUEST,              1);
        hnput(bp.htype, 1,                         1);
        hnput(bp.hlen,  6,                         1);
        memcpy(bp.xid,   xid,   sizeof(xid));
        hnput(bp.flags, F_BROADCAST, sizeof(bp.flags));
        hnput(bp.secs,  time(NULL) - starttime, sizeof(bp.secs));
        memcpy(bp.magic, magic, sizeof(bp.magic));
        memcpy(bp.chaddr, hwaddr, sizeof(bp.chaddr));
        p = bp.optdata;
        p = hnoptput(p, OD_TYPE,     type,                 1);
        p = optput(p,   OD_CLIENTID, cid,   sizeof(cid));
        p = optput(p,   OB_HOSTNAME, (unsigned char *)hostname, strlen(hostname));

        switch (type) {
        case DHCP_DISCOVER:
                break;
        case DHCP_REQUEST:
                p = optput(p, OD_IPADDR,   client, sizeof(client));
                p = optput(p, OD_SERVERID, server, sizeof(server));
                break;
        case DHCP_RELEASE:
                memcpy(bp.ciaddr, client, sizeof(client));
                p = optput(p, OD_IPADDR,   client, sizeof(client));
                p = optput(p, OD_SERVERID, server, sizeof(server));
                break;
        }
        *p++ = OB_END;

        ip = (how == BROADCAST) ? IP(255, 255, 255, 255) : server;
        udpsend(ip, sock, &bp, p - (unsigned char *)&bp);
}

/* block on poll; return DHCP message type or a TIMEOUT* sentinel */
static int
dhcprecv(void)
{
        unsigned char type = 0;
        uint64_t n;
        struct pollfd pfd[] = {
                { .fd = sock,      .events = POLLIN },
                { .fd = timers[0], .events = POLLIN },
                { .fd = timers[1], .events = POLLIN },
                { .fd = timers[2], .events = POLLIN },
        };

        if (poll(pfd, LEN(pfd), -1) == -1)
                eprintf("poll:");
        if (pfd[0].revents) {
                memset(&bp, 0, sizeof(bp));
                udprecv(IP(255, 255, 255, 255), sock, &bp, sizeof(bp));
                optget(&bp, &type, OD_TYPE, sizeof(type));
                return type;
        }
        if (pfd[1].revents) {
                type = TIMEOUT0;
                read(timers[0], &n, sizeof(n));
        }
        if (pfd[2].revents) {
                type = TIMEOUT1;
                read(timers[1], &n, sizeof(n));
        }
        if (pfd[3].revents) {
                type = TIMEOUT2;
                read(timers[2], &n, sizeof(n));
        }
        return type;
}

static void
acceptlease(void)
{
        char buf[128];

        if (iflag)
                setip(client, mask, router);
        if (dflag)
                setdns(dns);
        if (*program) {
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                    server[0], server[1], server[2], server[3]);
                setenv("SERVER", buf, 1);
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                    client[0], client[1], client[2], client[3]);
                setenv("CLIENT", buf, 1);
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                    mask[0], mask[1], mask[2], mask[3]);
                setenv("MASK", buf, 1);
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                    router[0], router[1], router[2], router[3]);
                setenv("ROUTER", buf, 1);
                snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                    dns[0], dns[1], dns[2], dns[3]);
                setenv("DNS", buf, 1);
                wsystem(program);
        }
}

static void
settimeout(int n, const struct itimerspec *ts)
{
        if (timerfd_settime(timers[n], 0, ts, NULL) < 0)
                eprintf("timerfd_settime:");
}

/* set ts to expire halfway to the remaining time on timer n, minimum 60s */
static void
calctimeout(int n, struct itimerspec *ts)
{
        if (timerfd_gettime(timers[n], ts) < 0)
                eprintf("timerfd_gettime:");
        ts->it_value.tv_nsec /= 2;
        if (ts->it_value.tv_sec % 2)
                ts->it_value.tv_nsec += 500000000;
        ts->it_value.tv_sec /= 2;
        if (ts->it_value.tv_sec < 60) {
                ts->it_value.tv_sec = 60;
                ts->it_value.tv_nsec = 0;
        }
}

/* RFC 2131 DHCP state machine */
static void
run(void)
{
        int forked = 0, t;
        struct itimerspec timeout = { 0 };
        uint32_t renewaltime, rebindingtime, lease;

Init:
        dhcpsend(DHCP_DISCOVER, BROADCAST);
        timeout.it_value.tv_sec = 1;
        timeout.it_value.tv_nsec = 0;
        settimeout(0, &timeout);
        goto Selecting;
Selecting:
        for (;;) {
                switch (dhcprecv()) {
                case DHCP_OFFER:
                        memcpy(client, bp.yiaddr, sizeof(client));
                        optget(&bp, server, OD_SERVERID, sizeof(server));
                        goto Requesting;
                case TIMEOUT0:
                        goto Init;
                }
        }
Requesting:
        for (t = 4; t <= 64; t *= 2) {
                dhcpsend(DHCP_REQUEST, BROADCAST);
                timeout.it_value.tv_sec = t;
                settimeout(0, &timeout);
                for (;;) {
                        switch (dhcprecv()) {
                        case DHCP_ACK:
                                goto Bound;
                        case DHCP_NAK:
                                goto Init;
                        case TIMEOUT0:
                                break;
                        default:
                                continue;
                        }
                        break;
                }
        }
        /* no ACK after several DHCPREQUEST attempts */
        goto Init;
Bound:
        optget(&bp, mask,           OB_MASK,          sizeof(mask));
        optget(&bp, router,         OB_ROUTER,        sizeof(router));
        optget(&bp, dns,            OB_DNSSERVER,     sizeof(dns));
        optget(&bp, &renewaltime,   OD_RENEWALTIME,   sizeof(renewaltime));
        optget(&bp, &rebindingtime, OD_REBINDINGTIME, sizeof(rebindingtime));
        optget(&bp, &lease,         OD_LEASE,         sizeof(lease));
        renewaltime   = ntohl(renewaltime);
        rebindingtime = ntohl(rebindingtime);
        lease         = ntohl(lease);
        acceptlease();
        puts("Bound. Network configured.");
        if (!fflag && !forked) {
                if (fork())
                        exit(0);
                forked = 1;
        }
        timeout.it_value.tv_sec = renewaltime;
        settimeout(0, &timeout);
        timeout.it_value.tv_sec = rebindingtime;
        settimeout(1, &timeout);
        timeout.it_value.tv_sec = lease;
        settimeout(2, &timeout);
        for (;;) {
                switch (dhcprecv()) {
                case TIMEOUT0: /* t1: enter renewing */
                        goto Renewing;
                case TIMEOUT1: /* t2: enter rebinding */
                        goto Rebinding;
                case TIMEOUT2: /* lease expired */
                        goto Init;
                }
        }
Renewing:
        dhcpsend(DHCP_REQUEST, UNICAST);
        calctimeout(1, &timeout);
        settimeout(0, &timeout);
        for (;;) {
                switch (dhcprecv()) {
                case DHCP_ACK:
                        goto Bound;
                case TIMEOUT0: /* resend unicast */
                        goto Renewing;
                case TIMEOUT1: /* t2 elapsed */
                        goto Rebinding;
                case TIMEOUT2:
                case DHCP_NAK:
                        goto Init;
                }
        }
Rebinding:
        calctimeout(2, &timeout);
        settimeout(0, &timeout);
        dhcpsend(DHCP_REQUEST, BROADCAST);
        for (;;) {
                switch (dhcprecv()) {
                case DHCP_ACK:
                        goto Bound;
                case TIMEOUT0: /* resend broadcast */
                        goto Rebinding;
                case TIMEOUT2: /* lease expired */
                case DHCP_NAK:
                        goto Init;
                }
        }
}

static void
cleanexit(int unused)
{
        (void)unused;
        dhcpsend(DHCP_RELEASE, UNICAST);
        _exit(0);
}

static void
usage(void)
{
        eprintf("usage: %s [-d] [-e program] [-f] [-i] [ifname] [clientid]\n", argv0);
}

// ?man sdhcp: minimal DHCP client
// ?man arguments: [ifname] [clientid]
// ?man obtain an IPv4 lease via DHCP on the given interface
int
main(int argc, char *argv[])
{
        struct ifreq ifreq;
        struct sockaddr addr;
        int bcast = 1;
        int rnd;
        size_t i;

        ARGBEGIN {
        case 'd': /* don't update /etc/resolv.conf */
                dflag = 0;
                break;
        case 'e': /* exec program on each lease */
                program = EARGF(usage());
                break;
        case 'f': /* stay in foreground */
                fflag = 1;
                break;
        case 'i': /* don't configure IP address */
                iflag = 0;
                break;
        default:
                usage();
                break;
        } ARGEND

        if (argc)
                ifname = argv[0];
        if (argc >= 2)
                strlcpy((char *)cid, argv[1], sizeof(cid));

        memset(&ifreq, 0, sizeof(ifreq));
        signal(SIGTERM, cleanexit);

        if (gethostname(hostname, sizeof(hostname)) == -1)
                eprintf("gethostname:");

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
                eprintf("socket:");
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) == -1)
                eprintf("setsockopt SO_BROADCAST:");

        strlcpy(ifreq.ifr_name, ifname, IF_NAMESIZE);
        ioctl(sock, SIOCGIFINDEX, &ifreq);
        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifreq, sizeof(ifreq)) == -1)
                eprintf("setsockopt SO_BINDTODEVICE:");
        iptoaddr(&addr, IP(255, 255, 255, 255), 68);
        if (bind(sock, &addr, sizeof(addr)) != 0)
                eprintf("bind:");
        ioctl(sock, SIOCGIFHWADDR, &ifreq);
        memcpy(hwaddr, ifreq.ifr_hwaddr.sa_data, sizeof(ifreq.ifr_hwaddr.sa_data));
        if (!cid[0])
                memcpy(cid, hwaddr, sizeof(cid));

        if ((rnd = open("/dev/urandom", O_RDONLY)) == -1)
                eprintf("open /dev/urandom:");
        if (read(rnd, xid, sizeof(xid)) != (ssize_t)sizeof(xid))
                eprintf("read /dev/urandom:");
        close(rnd);

        for (i = 0; i < LEN(timers); ++i) {
                timers[i] = timerfd_create(CLOCK_BOOTTIME, TFD_CLOEXEC);
                if (timers[i] == -1)
                        eprintf("timerfd_create:");
        }

        starttime = time(NULL);
        run();

        return 0;
}
