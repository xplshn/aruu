#include "../paths.h"
#include "../util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#include <sys/sysctl.h>
#endif

static void
get_mtu_metric(const char *name, int *mtu, int *metric)
{
  struct ifreq ifr;
  int          sock;

  *mtu    = 0;
  *metric = 0;
  sock    = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  if (ioctl(sock, SIOCGIFMTU, &ifr) >= 0)
    *mtu = ifr.ifr_mtu;
  if (ioctl(sock, SIOCGIFMETRIC, &ifr) >= 0)
    *metric = ifr.ifr_metric;
  close(sock);
}

int
net_get_interfaces(struct NetInterface **ifaces, int *count)
{
  struct ifaddrs      *ifaddr, *ifa;
  struct NetInterface *list = NULL;
  struct NetInterface *iface;
  int                  found_count = 0;
  int                  i;

  if (getifaddrs(&ifaddr) < 0)
    return -1;

  for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    iface = NULL;
    for (i = 0; i < found_count; i++) {
      if (strcmp(list[i].name, ifa->ifa_name) == 0) {
        iface = &list[i];
        break;
      }
    }
    if (!iface) {
      list = reallocarray(list, found_count + 1, sizeof(*list));
      if (!list) {
        freeifaddrs(ifaddr);
        return -1;
      }
      iface = &list[found_count];
      memset(iface, 0, sizeof(*iface));
      strlcpy(iface->name, ifa->ifa_name, sizeof(iface->name));
      iface->flags = ifa->ifa_flags;
      get_mtu_metric(iface->name, &iface->mtu, &iface->metric);
      found_count++;
    }

    if (!ifa->ifa_addr)
      continue;

    if (ifa->ifa_addr->sa_family == AF_INET) {
      memcpy(&iface->ipv4_addr, ifa->ifa_addr, sizeof(struct sockaddr_in));
      iface->has_ipv4 = 1;
      if (ifa->ifa_netmask)
        memcpy(&iface->ipv4_mask, ifa->ifa_netmask, sizeof(struct sockaddr_in));
      if (ifa->ifa_dstaddr)
        memcpy(&iface->ipv4_brd, ifa->ifa_dstaddr, sizeof(struct sockaddr_in));
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      memcpy(&iface->ipv6_addr, ifa->ifa_addr, sizeof(struct sockaddr_in6));
      iface->has_ipv6 = 1;
      /* scope id could be set here from sin6_scope_id */
    }
#ifdef __linux__
    else if (ifa->ifa_addr->sa_family == AF_PACKET) {
      struct sockaddr_ll *sll = (struct sockaddr_ll *)ifa->ifa_addr;
      if (sll->sll_halen == 6) {
        memcpy(iface->mac, sll->sll_addr, 6);
        iface->has_mac = 1;
      }
    }
#else
    else if (ifa->ifa_addr->sa_family == AF_LINK) {
      struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
      if (sdl->sdl_alen == 6) {
        memcpy(iface->mac, LLADDR(sdl), 6);
        iface->has_mac = 1;
      }
    }
#endif
  }

  freeifaddrs(ifaddr);
  *ifaces = list;
  *count  = found_count;
  return 0;
}

#ifdef __linux__
int
net_get_stats(const char *ifname, struct NetStats *stats)
{
  FILE *fp;
  char  line[256];
  char *p, *name;
  int   found = 0;

  memset(stats, 0, sizeof(*stats));
  fp = fopen(ARUU_LINUX_PATH_PROC "/net/dev", "r");
  if (!fp)
    return -1;

  while (fgets(line, sizeof(line), fp)) {
    p = strchr(line, ':');
    if (p) {
      *p   = '\0';
      name = line;
      while (isspace(*name))
        name++;
      if (strcmp(name, ifname) == 0) {
        if (sscanf(
                p + 1,
                "%llu %llu %llu %llu %*u %*u %*u "
                "%*u %llu %llu %llu %llu",
                &stats->rx_bytes,
                &stats->rx_packets,
                &stats->rx_errs,
                &stats->rx_drop,
                &stats->tx_bytes,
                &stats->tx_packets,
                &stats->tx_errs,
                &stats->tx_drop
            )
            == 8) {
          found = 1;
        }
        break;
      }
    }
  }
  fclose(fp);
  return found ? 0 : -1;
}
#else
int
net_get_stats(const char *ifname, struct NetStats *stats)
{
  struct ifaddrs *ifaddr, *ifa;
  int             found = 0;

  memset(stats, 0, sizeof(*stats));
  if (getifaddrs(&ifaddr) < 0)
    return -1;

  for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK
        && strcmp(ifa->ifa_name, ifname) == 0) {
      struct if_data *ifd = (struct if_data *)ifa->ifa_data;
      if (ifd) {
        stats->rx_bytes   = ifd->ifi_ibytes;
        stats->rx_packets = ifd->ifi_ipackets;
        stats->rx_errs    = ifd->ifi_ierrors;
        stats->rx_drop    = ifd->ifi_iqdrops;
        stats->tx_bytes   = ifd->ifi_obytes;
        stats->tx_packets = ifd->ifi_opackets;
        stats->tx_errs    = ifd->ifi_oerrors;
        stats->tx_drop    = 0;
        found             = 1;
        break;
      }
    }
  }
  freeifaddrs(ifaddr);
  return found ? 0 : -1;
}
#endif

#ifdef __linux__
int
net_set_txqueuelen(const char *name, int qlen)
{
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  ifr.ifr_qlen = qlen;
  if (ioctl(sock, SIOCSIFTXQLEN, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}
#else
int
net_set_txqueuelen(const char *name, int qlen)
{
  (void)name;
  (void)qlen;
  return -1;
}
#endif

int
net_set_flags(const char *name, unsigned int flags, int set)
{
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
    close(sock);
    return -1;
  }
  if (set)
    ifr.ifr_flags |= flags;
  else
    ifr.ifr_flags &= ~flags;
  if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

int
net_set_mtu(const char *name, int mtu)
{
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  ifr.ifr_mtu = mtu;
  if (ioctl(sock, SIOCSIFMTU, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

int
net_set_mac(const char *name, const unsigned char *mac)
{
#ifdef __linux__
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  ifr.ifr_hwaddr.sa_family = 1;
  memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
  if (ioctl(sock, SIOCSIFHWADDR, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
#else
  (void)name;
  (void)mac;
  return -1;
#endif
}

int
net_add_addr(const char *name, const char *addr, int prefix)
{
  struct ifreq        ifr;
  struct sockaddr_in *sin;
  int                 sock;
  unsigned int        mask;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;

  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

  sin             = (struct sockaddr_in *)&ifr.ifr_addr;
  sin->sin_family = AF_INET;
  if (inet_pton(AF_INET, addr, &sin->sin_addr) <= 0) {
    close(sock);
    return -1;
  }

  if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
    close(sock);
    return -1;
  }

  if (prefix >= 0) {
    mask = 0;
    if (prefix > 0)
      mask = htonl(~0U << (32 - prefix));
    sin                  = (struct sockaddr_in *)&ifr.ifr_netmask;
    sin->sin_family      = AF_INET;
    sin->sin_addr.s_addr = mask;
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
      close(sock);
      return -1;
    }
  }

  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    ioctl(sock, SIOCSIFFLAGS, &ifr);
  }

  close(sock);
  return 0;
}

int
net_del_addr(const char *name, const char *addr, int prefix)
{
  struct ifreq        ifr;
  struct sockaddr_in *sin;
  int                 sock;

  (void)prefix;
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;

  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

  sin             = (struct sockaddr_in *)&ifr.ifr_addr;
  sin->sin_family = AF_INET;
  if (inet_pton(AF_INET, addr, &sin->sin_addr) <= 0) {
    close(sock);
    return -1;
  }

  if (ioctl(sock, SIOCDIFADDR, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

int
net_show_routes(void)
{
#ifdef __linux__
  FILE        *fp;
  char         line[256];
  char         iface[16], dest[16], gateway[16], mask[16];
  unsigned int d, g, m, flags, metric;

  fp = fopen(ARUU_LINUX_PATH_PROC "/net/route", "r");
  if (!fp)
    return -1;

  printf(
      "%-10s %-15s %-15s %-15s %-6s %-6s\n",
      "Iface",
      "Destination",
      "Gateway",
      "Genmask",
      "Flags",
      "Metric"
  );
  if (fgets(line, sizeof(line), fp)) {
    while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "%15s %x %x %x %*u %*u %u %x", iface, &d, &g, &m, &metric, &flags) == 6) {
        struct in_addr dest_addr, gw_addr, mask_addr;
        dest_addr.s_addr = d;
        gw_addr.s_addr   = g;
        mask_addr.s_addr = m;
        strlcpy(dest, inet_ntoa(dest_addr), sizeof(dest));
        strlcpy(gateway, inet_ntoa(gw_addr), sizeof(gateway));
        strlcpy(mask, inet_ntoa(mask_addr), sizeof(mask));
        printf("%-10s %-15s %-15s %-15s 0x%04X %-6u\n", iface, dest, gateway, mask, flags, metric);
      }
    }
  }
  fclose(fp);
  return 0;
#else
  printf("route listing not implemented on this OS\n");
  return 0;
#endif
}

int
net_add_route(const char *dst, const char *gateway, const char *mask, const char *dev, int metric)
{
  struct rtentry      rt;
  struct sockaddr_in *sin;
  int                 sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;

  memset(&rt, 0, sizeof(rt));

  sin             = (struct sockaddr_in *)&rt.rt_dst;
  sin->sin_family = AF_INET;
  if (strcmp(dst, "default") == 0) {
    sin->sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, dst, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
  }

  if (gateway) {
    sin             = (struct sockaddr_in *)&rt.rt_gateway;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, gateway, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
    rt.rt_flags |= RTF_GATEWAY;
  }

  if (mask) {
    sin             = (struct sockaddr_in *)&rt.rt_genmask;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, mask, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
  }

  rt.rt_flags |= RTF_UP;
  if (dev)
    rt.rt_dev = (char *)dev;
  if (metric >= 0)
    rt.rt_metric = metric + 1;

  if (ioctl(sock, SIOCADDRT, &rt) < 0) {
    close(sock);
    return -1;
  }

  close(sock);
  return 0;
}

int
net_del_route(const char *dst, const char *gateway, const char *mask, const char *dev, int metric)
{
  struct rtentry      rt;
  struct sockaddr_in *sin;
  int                 sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;

  memset(&rt, 0, sizeof(rt));

  sin             = (struct sockaddr_in *)&rt.rt_dst;
  sin->sin_family = AF_INET;
  if (strcmp(dst, "default") == 0) {
    sin->sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, dst, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
  }

  if (gateway) {
    sin             = (struct sockaddr_in *)&rt.rt_gateway;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, gateway, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
    rt.rt_flags |= RTF_GATEWAY;
  }

  if (mask) {
    sin             = (struct sockaddr_in *)&rt.rt_genmask;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, mask, &sin->sin_addr) <= 0) {
      close(sock);
      return -1;
    }
  }

  rt.rt_flags |= RTF_UP;
  if (dev)
    rt.rt_dev = (char *)dev;
  if (metric >= 0)
    rt.rt_metric = metric + 1;

  if (ioctl(sock, SIOCDELRT, &rt) < 0) {
    close(sock);
    return -1;
  }

  close(sock);
  return 0;
}

int
net_flush_addrs(const char *dev)
{
  struct NetInterface *ifaces = NULL;
  int                  count = 0, i, r = 0;

  if (net_get_interfaces(&ifaces, &count) < 0)
    return -1;

  for (i = 0; i < count; i++) {
    if (strcmp(ifaces[i].name, dev) == 0) {
      if (ifaces[i].has_ipv4) {
        char                addr_str[16];
        struct sockaddr_in *sin = &ifaces[i].ipv4_addr;
        inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
        if (net_del_addr(dev, addr_str, -1) < 0)
          r = -1;
      }
    }
  }
  free(ifaces);
  return r;
}

int
net_set_name(const char *name, const char *newname)
{
#ifdef __linux__
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  strlcpy(ifr.ifr_newname, newname, sizeof(ifr.ifr_newname));
  if (ioctl(sock, SIOCSIFNAME, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
#else
  (void)name;
  (void)newname;
  return -1;
#endif
}
