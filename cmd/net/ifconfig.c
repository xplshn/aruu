/* see LICENSE file for copyright and license details */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-a] [interface [action ...]]\n", argv0);
}

static void
display_interface(const struct NetInterface *iface)
{
  struct NetStats stats;
  char            ipv6_str[INET6_ADDRSTRLEN];

  printf("%-9s Link encap:", iface->name);
  if (iface->has_mac) {
    printf(
        "Ethernet  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
        iface->mac[0],
        iface->mac[1],
        iface->mac[2],
        iface->mac[3],
        iface->mac[4],
        iface->mac[5]
    );
  } else if (iface->flags & NET_IF_LOOPBACK) {
    printf("Local Loopback\n");
  } else {
    printf("UNSPEC\n");
  }

  if (iface->has_ipv4) {
    printf("        inet addr:%s", inet_ntoa(iface->ipv4_addr.sin_addr));
    printf("  Bcast:%s", inet_ntoa(iface->ipv4_brd.sin_addr));
    printf("  Mask:%s\n", inet_ntoa(iface->ipv4_mask.sin_addr));
  }

  if (iface->has_ipv6) {
    if (inet_ntop(AF_INET6, &iface->ipv6_addr.sin6_addr, ipv6_str, sizeof(ipv6_str))) {
      printf("        inet6 addr: %s\n", ipv6_str);
    }
  }

  printf("        ");
  if (iface->flags & NET_IF_UP)
    printf("UP ");
  if (iface->flags & NET_IF_BROADCAST)
    printf("BROADCAST ");
  if (iface->flags & NET_IF_DEBUG)
    printf("DEBUG ");
  if (iface->flags & NET_IF_LOOPBACK)
    printf("LOOPBACK ");
  if (iface->flags & NET_IF_POINTOPOINT)
    printf("POINTOPOINT ");
  if (iface->flags & NET_IF_RUNNING)
    printf("RUNNING ");
  if (iface->flags & NET_IF_NOARP)
    printf("NOARP ");
  if (iface->flags & NET_IF_PROMISC)
    printf("PROMISC ");
  if (iface->flags & NET_IF_ALLMULTI)
    printf("ALLMULTI ");
  if (iface->flags & NET_IF_MULTICAST)
    printf("MULTICAST ");

  printf(" MTU:%d  Metric:%d\n", iface->mtu, iface->metric);

  if (net_get_stats(iface->name, &stats) >= 0) {
    printf(
        "        RX packets:%llu errors:%llu dropped:%llu "
        "overruns:0 frame:0\n",
        stats.rx_packets,
        stats.rx_errs,
        stats.rx_drop
    );
    printf(
        "        TX packets:%llu errors:%llu dropped:%llu "
        "overruns:0 carrier:0\n",
        stats.tx_packets,
        stats.tx_errs,
        stats.tx_drop
    );
    printf("        RX bytes:%llu  TX bytes:%llu\n", stats.rx_bytes, stats.tx_bytes);
  }
  printf("\n");
}

static void
list_interfaces(int all)
{
  struct NetInterface *ifaces = NULL;
  int                  count  = 0;
  int                  i;

  if (net_get_interfaces(&ifaces, &count) < 0)
    eprintf("net_get_interfaces:");

  for (i = 0; i < count; i++) {
    if (all || (ifaces[i].flags & NET_IF_UP)) {
      display_interface(&ifaces[i]);
    }
  }
  free(ifaces);
}

// ?man ifconfig: configure network interfaces
// ?man arguments: interface [action ...]
// ?man configure network interface parameters and view stats
int
main(int argc, char *argv[])
{
  struct NetInterface       *ifaces = NULL;
  const struct NetInterface *iface  = NULL;
  char                      *name;
  char                      *arg;
  char                      *slash;
  int                        aflag = 0;
  int                        i     = 1;
  int                        prefix;
  int                        count = 0;

  ARGBEGIN
  {
    // ?man -a: print or show all entries
    case 'a':
      aflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (argc == 0) {
    list_interfaces(aflag);
    return 0;
  }

  name = argv[0];

  if (net_get_interfaces(&ifaces, &count) < 0)
    eprintf("net_get_interfaces:");

  for (prefix = 0; prefix < count; prefix++) {
    if (strcmp(ifaces[prefix].name, name) == 0) {
      iface = &ifaces[prefix];
      break;
    }
  }

  if (argc == 1) {
    if (!iface)
      eprintf("interface %s not found\n", name);
    display_interface(iface);
    free(ifaces);
    return 0;
  }

  while (i < argc) {
    arg = argv[i];

    if (strcmp(arg, "up") == 0) {
      if (net_set_flags(name, NET_IF_UP | NET_IF_RUNNING, 1) < 0)
        eprintf("net_set_flags up:");
      i++;
    } else if (strcmp(arg, "down") == 0) {
      if (net_set_flags(name, NET_IF_UP, 0) < 0)
        eprintf("net_set_flags down:");
      i++;
    } else if (strcmp(arg, "netmask") == 0) {
      if (i + 1 >= argc)
        eprintf("netmask needs an address\n");
      if (net_set_mask(name, argv[i + 1]) < 0)
        eprintf("net_set_mask:");
      i += 2;
    } else if (strcmp(arg, "broadcast") == 0) {
      if (i + 1 >= argc)
        eprintf("broadcast needs an address\n");
      if (net_set_broadcast(name, argv[i + 1]) < 0)
        eprintf("net_set_broadcast:");
      i += 2;
    } else if (strcmp(arg, "mtu") == 0) {
      if (i + 1 >= argc)
        eprintf("mtu needs a value\n");
      if (net_set_mtu(name, atoi(argv[i + 1])) < 0)
        eprintf("net_set_mtu:");
      i += 2;
    } else if (strcmp(arg, "metric") == 0) {
      if (i + 1 >= argc)
        eprintf("metric needs a value\n");
      if (net_set_metric(name, atoi(argv[i + 1])) < 0)
        eprintf("net_set_metric:");
      i += 2;
    } else if (strcmp(arg, "txqueuelen") == 0) {
      if (i + 1 >= argc)
        eprintf("txqueuelen needs a value\n");
      if (net_set_txqueuelen(name, atoi(argv[i + 1])) < 0)
        eprintf("net_set_txqueuelen:");
      i += 2;
    } else if (strcmp(arg, "dstaddr") == 0 || strcmp(arg, "pointopoint") == 0) {
      if (i + 1 >= argc)
        eprintf("%s needs an address\n", arg);
      if (net_set_dstaddr(name, argv[i + 1]) < 0)
        eprintf("net_set_dstaddr:");
      i += 2;
    } else if (strcmp(arg, "arp") == 0) {
      if (net_set_flags(name, NET_IF_NOARP, 0) < 0)
        eprintf("net_set_flags arp:");
      i++;
    } else if (strcmp(arg, "-arp") == 0) {
      if (net_set_flags(name, NET_IF_NOARP, 1) < 0)
        eprintf("net_set_flags -arp:");
      i++;
    } else if (strcmp(arg, "promisc") == 0) {
      if (net_set_flags(name, NET_IF_PROMISC, 1) < 0)
        eprintf("net_set_flags promisc:");
      i++;
    } else if (strcmp(arg, "-promisc") == 0) {
      if (net_set_flags(name, NET_IF_PROMISC, 0) < 0)
        eprintf("net_set_flags -promisc:");
      i++;
    } else if (strcmp(arg, "allmulti") == 0) {
      if (net_set_flags(name, NET_IF_ALLMULTI, 1) < 0)
        eprintf("net_set_flags allmulti:");
      i++;
    } else if (strcmp(arg, "-allmulti") == 0) {
      if (net_set_flags(name, NET_IF_ALLMULTI, 0) < 0)
        eprintf("net_set_flags -allmulti:");
      i++;
    } else if (strcmp(arg, "multicast") == 0) {
      if (net_set_flags(name, NET_IF_MULTICAST, 1) < 0)
        eprintf("net_set_flags multicast:");
      i++;
    } else if (strcmp(arg, "-multicast") == 0) {
      if (net_set_flags(name, NET_IF_MULTICAST, 0) < 0)
        eprintf("net_set_flags -multicast:");
      i++;
    } else {
      slash  = strchr(arg, '/');
      prefix = -1;
      if (slash) {
        *slash = '\0';
        prefix = atoi(slash + 1);
      }
      if (net_add_addr(name, arg, prefix) < 0)
        eprintf("net_add_addr:");
      i++;
    }
  }

  free(ifaces);

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    return 1;

  return 0;
}
