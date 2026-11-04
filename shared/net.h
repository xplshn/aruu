#ifndef NET_H
#define NET_H

#include <netinet/in.h>

#define NET_IFNAME_MAX       16
#define NET_ROUTE_ADDRSTRLEN 16

enum NetInterfaceFlag {
  NET_IF_UP          = 0x00000001,
  NET_IF_BROADCAST   = 0x00000002,
  NET_IF_DEBUG       = 0x00000004,
  NET_IF_LOOPBACK    = 0x00000008,
  NET_IF_POINTOPOINT = 0x00000010,
  NET_IF_RUNNING     = 0x00000020,
  NET_IF_NOARP       = 0x00000040,
  NET_IF_PROMISC     = 0x00000080,
  NET_IF_ALLMULTI    = 0x00000100,
  NET_IF_MULTICAST   = 0x00000200,
};

struct NetStats {
  unsigned long long rx_bytes;
  unsigned long long rx_packets;
  unsigned long long rx_errs;
  unsigned long long rx_drop;
  unsigned long long tx_bytes;
  unsigned long long tx_packets;
  unsigned long long tx_errs;
  unsigned long long tx_drop;
};

struct NetInterface {
  char                name[NET_IFNAME_MAX];
  unsigned int        flags;
  int                 mtu;
  int                 metric;
  unsigned char       mac[6];
  int                 has_mac;
  struct sockaddr_in  ipv4_addr;
  int                 has_ipv4;
  struct sockaddr_in  ipv4_mask;
  struct sockaddr_in  ipv4_brd;
  struct sockaddr_in6 ipv6_addr;
  int                 has_ipv6;
  unsigned int        ipv6_scope;
  unsigned int        ipv6_prefix;
};

struct NetRoute {
  char           interface[NET_IFNAME_MAX];
  struct in_addr destination;
  struct in_addr gateway;
  struct in_addr mask;
  unsigned int   flags;
  unsigned int   metric;
};

int net_get_interfaces(struct NetInterface **, int *);
int net_get_routes(struct NetRoute **, int *);
int net_show_routes(void);
int net_get_stats(const char *, struct NetStats *);
int net_set_flags(const char *, unsigned int, int);
int net_set_mtu(const char *, int);
int net_set_metric(const char *, int);
int net_set_mac(const char *, const unsigned char *);
int net_set_mask(const char *, const char *);
int net_set_broadcast(const char *, const char *);
int net_set_dstaddr(const char *, const char *);
int net_add_addr(const char *, const char *, int);
int net_del_addr(const char *, const char *, int);
int net_add_route(const char *, const char *, const char *, int, int);
int net_del_route(const char *, const char *, const char *, int, int);
int net_flush_addrs(const char *);
int net_set_txqueuelen(const char *, int);
int net_set_name(const char *, const char *);

#endif
