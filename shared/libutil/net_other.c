#include "../net.h"

#include <errno.h>
#include <stddef.h>

#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
int
net_get_interfaces(struct NetInterface **ifaces, int *count)
{
  *ifaces = NULL;
  *count  = 0;
  errno   = ENOTSUP;
  return -1;
}

int
net_get_routes(struct NetRoute **routes, int *count)
{
  *routes = NULL;
  *count  = 0;
  errno   = ENOTSUP;
  return -1;
}

int
net_show_routes(void)
{
  errno = ENOTSUP;
  return -1;
}

int
net_get_stats(const char *ifname, struct NetStats *stats)
{
  (void)ifname;
  (void)stats;
  errno = ENOTSUP;
  return -1;
}

int
net_set_flags(const char *name, unsigned int flags, int set)
{
  (void)name;
  (void)flags;
  (void)set;
  errno = ENOTSUP;
  return -1;
}

int
net_set_mtu(const char *name, int mtu)
{
  (void)name;
  (void)mtu;
  errno = ENOTSUP;
  return -1;
}

int
net_set_metric(const char *name, int metric)
{
  (void)name;
  (void)metric;
  errno = ENOTSUP;
  return -1;
}

int
net_set_mac(const char *name, const unsigned char *mac)
{
  (void)name;
  (void)mac;
  errno = ENOTSUP;
  return -1;
}

int
net_set_mask(const char *name, const char *mask)
{
  (void)name;
  (void)mask;
  errno = ENOTSUP;
  return -1;
}

int
net_set_broadcast(const char *name, const char *broadcast)
{
  (void)name;
  (void)broadcast;
  errno = ENOTSUP;
  return -1;
}

int
net_set_dstaddr(const char *name, const char *dstaddr)
{
  (void)name;
  (void)dstaddr;
  errno = ENOTSUP;
  return -1;
}

int
net_add_addr(const char *name, const char *addr, int prefix)
{
  (void)name;
  (void)addr;
  (void)prefix;
  errno = ENOTSUP;
  return -1;
}

int
net_del_addr(const char *name, const char *addr, int prefix)
{
  (void)name;
  (void)addr;
  (void)prefix;
  errno = ENOTSUP;
  return -1;
}

int
net_add_route(const char *dst, const char *gateway, const char *dev, int prefix, int metric)
{
  (void)dst;
  (void)gateway;
  (void)dev;
  (void)prefix;
  (void)metric;
  errno = ENOTSUP;
  return -1;
}

int
net_del_route(const char *dst, const char *gateway, const char *dev, int prefix, int metric)
{
  (void)dst;
  (void)gateway;
  (void)dev;
  (void)prefix;
  (void)metric;
  errno = ENOTSUP;
  return -1;
}

int
net_flush_addrs(const char *dev)
{
  (void)dev;
  errno = ENOTSUP;
  return -1;
}

int
net_set_txqueuelen(const char *name, int qlen)
{
  (void)name;
  (void)qlen;
  errno = ENOTSUP;
  return -1;
}

int
net_set_name(const char *name, const char *newname)
{
  (void)name;
  (void)newname;
  errno = ENOTSUP;
  return -1;
}
#endif
