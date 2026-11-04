#if defined(__linux__)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../net.h"
#include "../paths.h"
#include "../util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <netpacket/packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned int
interface_flags_to_linux(unsigned int flags)
{
  unsigned int linux_flags = 0;

  if (flags & NET_IF_UP)
    linux_flags |= IFF_UP;
  if (flags & NET_IF_BROADCAST)
    linux_flags |= IFF_BROADCAST;
  if (flags & NET_IF_DEBUG)
    linux_flags |= IFF_DEBUG;
  if (flags & NET_IF_LOOPBACK)
    linux_flags |= IFF_LOOPBACK;
  if (flags & NET_IF_POINTOPOINT)
    linux_flags |= IFF_POINTOPOINT;
  if (flags & NET_IF_RUNNING)
    linux_flags |= IFF_RUNNING;
  if (flags & NET_IF_NOARP)
    linux_flags |= IFF_NOARP;
  if (flags & NET_IF_PROMISC)
    linux_flags |= IFF_PROMISC;
  if (flags & NET_IF_ALLMULTI)
    linux_flags |= IFF_ALLMULTI;
  if (flags & NET_IF_MULTICAST)
    linux_flags |= IFF_MULTICAST;
  return linux_flags;
}

static unsigned int
interface_flags_from_linux(unsigned int linux_flags)
{
  unsigned int flags = 0;

  if (linux_flags & IFF_UP)
    flags |= NET_IF_UP;
  if (linux_flags & IFF_BROADCAST)
    flags |= NET_IF_BROADCAST;
  if (linux_flags & IFF_DEBUG)
    flags |= NET_IF_DEBUG;
  if (linux_flags & IFF_LOOPBACK)
    flags |= NET_IF_LOOPBACK;
  if (linux_flags & IFF_POINTOPOINT)
    flags |= NET_IF_POINTOPOINT;
  if (linux_flags & IFF_RUNNING)
    flags |= NET_IF_RUNNING;
  if (linux_flags & IFF_NOARP)
    flags |= NET_IF_NOARP;
  if (linux_flags & IFF_PROMISC)
    flags |= NET_IF_PROMISC;
  if (linux_flags & IFF_ALLMULTI)
    flags |= NET_IF_ALLMULTI;
  if (linux_flags & IFF_MULTICAST)
    flags |= NET_IF_MULTICAST;
  return flags;
}

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
  struct sockaddr_ll  *sll;
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
      iface->flags = interface_flags_from_linux(ifa->ifa_flags);
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
    } else if (ifa->ifa_addr->sa_family == AF_PACKET) {
      sll = (struct sockaddr_ll *)ifa->ifa_addr;
      if (sll->sll_halen == 6) {
        memcpy(iface->mac, sll->sll_addr, 6);
        iface->has_mac = 1;
      }
    }
  }

  freeifaddrs(ifaddr);
  *ifaces = list;
  *count  = found_count;
  return 0;
}

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

int
net_set_flags(const char *name, unsigned int flags, int set)
{
  struct ifreq ifr;
  unsigned int linux_flags;
  int          sock;

  linux_flags = interface_flags_to_linux(flags);
  sock        = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
    close(sock);
    return -1;
  }
  if (set)
    ifr.ifr_flags |= linux_flags;
  else
    ifr.ifr_flags &= ~linux_flags;
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
net_set_metric(const char *name, int metric)
{
  struct ifreq ifr;
  int          sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  ifr.ifr_metric = metric;
  if (ioctl(sock, SIOCSIFMETRIC, &ifr) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

int
net_set_mac(const char *name, const unsigned char *mac)
{
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
}

enum InterfaceAddress {
  INTERFACE_ADDRESS_MASK,
  INTERFACE_ADDRESS_BROADCAST,
  INTERFACE_ADDRESS_DESTINATION,
};

static struct sockaddr_in *
interface_address(struct ifreq *ifr, enum InterfaceAddress type)
{
  switch (type) {
    case INTERFACE_ADDRESS_MASK:
      return (struct sockaddr_in *)&ifr->ifr_netmask;
    case INTERFACE_ADDRESS_BROADCAST:
      return (struct sockaddr_in *)&ifr->ifr_broadaddr;
    case INTERFACE_ADDRESS_DESTINATION:
      return (struct sockaddr_in *)&ifr->ifr_dstaddr;
  }
  return NULL;
}

static int
set_interface_address(
    const char           *name,
    const char           *addr,
    enum InterfaceAddress type,
    unsigned long         request,
    unsigned int          flags
)
{
  struct ifreq        ifr;
  struct sockaddr_in *sin;
  int                 sock;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    return -1;
  memset(&ifr, 0, sizeof(ifr));
  strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
  sin             = interface_address(&ifr, type);
  sin->sin_family = AF_INET;
  if (inet_pton(AF_INET, addr, &sin->sin_addr) <= 0) {
    close(sock);
    return -1;
  }
  if (ioctl(sock, request, &ifr) < 0) {
    close(sock);
    return -1;
  }
  if (flags) {
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
      close(sock);
      return -1;
    }
    ifr.ifr_flags |= interface_flags_to_linux(flags);
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
      close(sock);
      return -1;
    }
  }
  close(sock);
  return 0;
}

int
net_set_mask(const char *name, const char *mask)
{
  return set_interface_address(name, mask, INTERFACE_ADDRESS_MASK, SIOCSIFNETMASK, 0);
}

int
net_set_broadcast(const char *name, const char *broadcast)
{
  return set_interface_address(
      name, broadcast, INTERFACE_ADDRESS_BROADCAST, SIOCSIFBRDADDR, NET_IF_BROADCAST
  );
}

int
net_set_dstaddr(const char *name, const char *dstaddr)
{
  return set_interface_address(
      name, dstaddr, INTERFACE_ADDRESS_DESTINATION, SIOCSIFDSTADDR, NET_IF_POINTOPOINT
  );
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
net_get_routes(struct NetRoute **routes, int *count)
{
  FILE            *fp;
  char             line[256];
  char             iface[NET_IFNAME_MAX];
  unsigned int     d, g, flags, metric, m;
  struct in_addr   dest_addr, gw_addr, mask_addr;
  struct NetRoute *list = NULL;
  struct NetRoute *route;
  int              found_count = 0;

  *routes = NULL;
  *count  = 0;
  fp      = fopen(ARUU_LINUX_PATH_PROC "/net/route", "r");
  if (!fp)
    return -1;

  if (fgets(line, sizeof(line), fp)) {
    while (fgets(line, sizeof(line), fp)) {
      if (sscanf(line, "%15s %x %x %x %*u %*u %u %x", iface, &d, &g, &flags, &metric, &m) != 6)
        continue;
      route = reallocarray(list, found_count + 1, sizeof(*route));
      if (!route) {
        free(list);
        fclose(fp);
        return -1;
      }
      list             = route;
      route            = &list[found_count];
      dest_addr.s_addr = d;
      gw_addr.s_addr   = g;
      mask_addr.s_addr = m;
      strlcpy(route->interface, iface, sizeof(route->interface));
      route->destination.s_addr = dest_addr.s_addr;
      route->gateway.s_addr     = gw_addr.s_addr;
      route->mask.s_addr        = mask_addr.s_addr;
      route->flags              = flags;
      route->metric             = metric;
      found_count++;
    }
  }
  fclose(fp);
  *routes = list;
  *count  = found_count;
  return 0;
}

static int
route_prefix_to_mask(int prefix, struct in_addr *mask)
{
  if (prefix > 32) {
    errno = EINVAL;
    return -1;
  }
  if (prefix < 0)
    prefix = 32;
  mask->s_addr = 0;
  if (prefix > 0)
    mask->s_addr = htonl(~0U << (32 - prefix));
  return 0;
}

static int
change_route(
    const char   *dst,
    const char   *gateway,
    const char   *dev,
    int           prefix,
    int           metric,
    unsigned long request
)
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

  sin             = (struct sockaddr_in *)&rt.rt_genmask;
  sin->sin_family = AF_INET;
  if (route_prefix_to_mask(prefix, &sin->sin_addr) < 0) {
    close(sock);
    return -1;
  }

  rt.rt_flags |= RTF_UP;
  if (dev)
    rt.rt_dev = (char *)dev;
  if (metric >= 0)
    rt.rt_metric = metric + 1;

  if (ioctl(sock, request, &rt) < 0) {
    close(sock);
    return -1;
  }
  close(sock);
  return 0;
}

int
net_add_route(const char *dst, const char *gateway, const char *dev, int prefix, int metric)
{
  return change_route(dst, gateway, dev, prefix, metric, SIOCADDRT);
}

int
net_del_route(const char *dst, const char *gateway, const char *dev, int prefix, int metric)
{
  return change_route(dst, gateway, dev, prefix, metric, SIOCDELRT);
}

int
net_flush_addrs(const char *dev)
{
  struct NetInterface *ifaces = NULL;
  struct sockaddr_in  *sin;
  char                 addr_str[16];
  int                  count = 0, i, r = 0;

  if (net_get_interfaces(&ifaces, &count) < 0)
    return -1;

  for (i = 0; i < count; i++) {
    if (strcmp(ifaces[i].name, dev) == 0) {
      if (ifaces[i].has_ipv4) {
        sin = &ifaces[i].ipv4_addr;
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
}
#endif
