/* See LICENSE file for copyright and license details. */


#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

#if FEATURE_IP_ROUTE_ADD_DEL
static void
prefix_to_mask(int prefix, char *mask_str, size_t mask_str_len)
{
	unsigned long mask;
	struct in_addr addr;

	if (prefix < 0)
		prefix = 32;
	if (prefix > 32)
		eprintf("invalid prefix: %d\n", prefix);

	if (prefix == 0) {
		mask = 0;
	} else {
		mask = 0xffffffffUL << (32 - prefix);
	}
	addr.s_addr = htonl(mask);
	strlcpy(mask_str, inet_ntoa(addr), mask_str_len);
}
#endif

static void
usage(void)
{
	eprintf("usage: %s [addr | link | route] [args...]\n", argv0);
}

static void
parse_mac(const char *str, unsigned char mac[6])
{
	unsigned int val;
	int i;
	const char *p = str;

	for (i = 0; i < 6; i++) {
		if (i > 0) {
			if (*p != ':')
				eprintf("invalid mac address: %s\n", str);
			p++;
		}
		if (sscanf(p, "%2x", &val) != 1)
			eprintf("invalid mac address: %s\n", str);
		mac[i] = val;
		p += 2;
	}
}

static void
print_link_iface(const struct NetInterface *iface)
{
	printf("%d: %s: mtu %d ", iface->metric, iface->name, iface->mtu);
	if (iface->flags & IFF_UP) printf("UP ");
	if (iface->flags & IFF_LOOPBACK) printf("LOOPBACK ");
	if (iface->flags & IFF_POINTOPOINT) printf("POINTOPOINT ");
	if (iface->flags & IFF_RUNNING) printf("RUNNING ");
	printf("\n");
	if (iface->has_mac) {
		printf("    link/ether %02x:%02x:%02x:%02x:%02x:%02x\n",
		       iface->mac[0], iface->mac[1], iface->mac[2],
		       iface->mac[3], iface->mac[4], iface->mac[5]);
	}
}

static void
print_addr_iface(const struct NetInterface *iface)
{
	char ipv6_str[INET6_ADDRSTRLEN];

	print_link_iface(iface);
	if (iface->has_ipv4) {
		printf("    inet %s netmask %s\n",
		       inet_ntoa(iface->ipv4_addr.sin_addr),
		       inet_ntoa(iface->ipv4_mask.sin_addr));
	}
	if (iface->has_ipv6) {
		if (inet_ntop(AF_INET6, &iface->ipv6_addr.sin6_addr, ipv6_str, sizeof(ipv6_str))) {
			printf("    inet6 %s\n", ipv6_str);
		}
	}
}

static int
do_addr(int argc, char *argv[])
{
	struct NetInterface *ifaces = NULL;
	char *cmd, *ip, *dev, *slash;
	int count = 0;
	int prefix = -1;
	int i, r;

	if (argc == 0)
		cmd = "show";
	else
		cmd = argv[0];

	if (strcmp(cmd, "show") == 0 || strcmp(cmd, "list") == 0) {
		dev = NULL;
		if (argc > 1) {
			if (strcmp(argv[1], "dev") == 0 && argc > 2)
				dev = argv[2];
			else
				dev = argv[1];
		}
		if (net_get_interfaces(&ifaces, &count) < 0)
			eprintf("net_get_interfaces:");
		for (i = 0; i < count; i++) {
			if (!dev || strcmp(ifaces[i].name, dev) == 0)
				print_addr_iface(&ifaces[i]);
		}
		free(ifaces);
		return 0;
	}

	if (strcmp(cmd, "add") == 0 || strcmp(cmd, "del") == 0) {
		if (argc < 4)
			eprintf("usage: ip addr %s <ip>/<prefix> dev <interface>\n", cmd);
		ip = argv[1];
		dev = NULL;
		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
				dev = argv[i + 1];
				break;
			}
		}
		if (!dev)
			eprintf("missing dev parameter\n");

		slash = strchr(ip, '/');
		if (slash) {
			*slash = '\0';
			prefix = atoi(slash + 1);
		}

		if (strcmp(cmd, "add") == 0)
			r = net_add_addr(dev, ip, prefix);
		else
			r = net_del_addr(dev, ip, prefix);

		if (r < 0)
			eprintf("net_%s_addr:", cmd);
		return 0;
	}

#if FEATURE_IP_ADDR_FLUSH
	if (strcmp(cmd, "flush") == 0) {
		dev = NULL;
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
				dev = argv[i + 1];
				break;
			}
		}
		if (!dev)
			eprintf("missing dev parameter for flush\n");
		if (net_flush_addrs(dev) < 0)
			eprintf("net_flush_addrs:");
		return 0;
	}
#endif

	usage();
	return 1;
}

static int
do_link(int argc, char *argv[])
{
	struct NetInterface *ifaces = NULL;
	char *cmd, *dev, *mac_str;
	int count = 0;
	int i, mtu;
	unsigned char mac[6];

	if (argc == 0)
		cmd = "show";
	else
		cmd = argv[0];

	if (strcmp(cmd, "show") == 0 || strcmp(cmd, "list") == 0) {
		dev = NULL;
		if (argc > 1) {
			if (strcmp(argv[1], "dev") == 0 && argc > 2)
				dev = argv[2];
			else
				dev = argv[1];
		}
		if (net_get_interfaces(&ifaces, &count) < 0)
			eprintf("net_get_interfaces:");
		for (i = 0; i < count; i++) {
			if (!dev || strcmp(ifaces[i].name, dev) == 0)
				print_link_iface(&ifaces[i]);
		}
		free(ifaces);
		return 0;
	}

	if (strcmp(cmd, "set") == 0) {
		if (argc < 3)
			eprintf("usage: ip link set <interface> [up | down | mtu <mtu> | address <mac>]\n");
		dev = argv[1];
		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "up") == 0) {
				if (net_set_flags(dev, IFF_UP, 1) < 0)
					eprintf("net_set_flags up:");
			} else if (strcmp(argv[i], "down") == 0) {
				if (net_set_flags(dev, IFF_UP, 0) < 0)
					eprintf("net_set_flags down:");
			} else if (strcmp(argv[i], "mtu") == 0 && i + 1 < argc) {
				mtu = atoi(argv[i + 1]);
				if (net_set_mtu(dev, mtu) < 0)
					eprintf("net_set_mtu:");
				i++;
			} else if (strcmp(argv[i], "address") == 0 && i + 1 < argc) {
				mac_str = argv[i + 1];
				parse_mac(mac_str, mac);
				if (net_set_mac(dev, mac) < 0)
					eprintf("net_set_mac:");
				i++;
#if FEATURE_IP_LINK_SET
#ifdef IFF_NOARP
			} else if (strcmp(argv[i], "arp") == 0 && i + 1 < argc) {
				int set = (strcmp(argv[i + 1], "off") == 0);
				if (net_set_flags(dev, IFF_NOARP, set) < 0)
					eprintf("net_set_flags arp:");
				i++;
#endif
#ifdef IFF_MULTICAST
			} else if (strcmp(argv[i], "multicast") == 0 && i + 1 < argc) {
				int set = (strcmp(argv[i + 1], "on") == 0);
				if (net_set_flags(dev, IFF_MULTICAST, set) < 0)
					eprintf("net_set_flags multicast:");
				i++;
#endif
#ifdef IFF_ALLMULTI
			} else if (strcmp(argv[i], "allmulticast") == 0 && i + 1 < argc) {
				int set = (strcmp(argv[i + 1], "on") == 0);
				if (net_set_flags(dev, IFF_ALLMULTI, set) < 0)
					eprintf("net_set_flags allmulticast:");
				i++;
#endif
#ifdef IFF_PROMISC
			} else if (strcmp(argv[i], "promisc") == 0 && i + 1 < argc) {
				int set = (strcmp(argv[i + 1], "on") == 0);
				if (net_set_flags(dev, IFF_PROMISC, set) < 0)
					eprintf("net_set_flags promisc:");
				i++;
#endif
			} else if (strcmp(argv[i], "name") == 0 && i + 1 < argc) {
				if (net_set_name(dev, argv[i + 1]) < 0)
					eprintf("net_set_name:");
				dev = argv[i + 1];
				i++;
			} else if (strcmp(argv[i], "txqueuelen") == 0 && i + 1 < argc) {
				int qlen = atoi(argv[i + 1]);
				if (net_set_txqueuelen(dev, qlen) < 0)
					eprintf("net_set_txqueuelen:");
				i++;
#endif
			}
		}
		return 0;
	}

	usage();
	return 1;
}

static int
do_route(int argc, char *argv[])
{
	char *cmd, *dst, *slash;
	const char *gateway = NULL, *dev = NULL;
	int prefix_len = -1, metric = -1, i;
	char mask_str[32];

	if (argc == 0)
		cmd = "show";
	else
		cmd = argv[0];

	if (strcmp(cmd, "show") == 0 || strcmp(cmd, "list") == 0) {
		if (net_show_routes() < 0)
			eprintf("net_show_routes:");
		return 0;
	}

#if FEATURE_IP_ROUTE_ADD_DEL
	if (strcmp(cmd, "add") == 0 || strcmp(cmd, "del") == 0) {
		if (argc < 2)
			eprintf("usage: ip route %s <prefix> [via <gateway>] [dev <device>] [metric <metric>]\n", cmd);

		dst = argv[1];
		slash = strchr(dst, '/');
		if (slash) {
			*slash = '\0';
			prefix_len = atoi(slash + 1);
		} else if (strcmp(dst, "default") == 0) {
			prefix_len = 0;
		}

		prefix_to_mask(prefix_len, mask_str, sizeof(mask_str));

		for (i = 2; i < argc; i++) {
			if (strcmp(argv[i], "via") == 0 && i + 1 < argc) {
				gateway = argv[i + 1];
				i++;
			} else if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
				dev = argv[i + 1];
				i++;
			} else if (strcmp(argv[i], "metric") == 0 && i + 1 < argc) {
				metric = atoi(argv[i + 1]);
				i++;
			}
		}

		if (strcmp(cmd, "add") == 0) {
			if (net_add_route(dst, gateway, mask_str, dev, metric) < 0)
				eprintf("net_add_route:");
		} else {
			if (net_del_route(dst, gateway, mask_str, dev, metric) < 0)
				eprintf("net_del_route:");
		}
		return 0;
	}
#endif

	usage();
	return 1;
}

// ?man ip: show or configure routing and devices
// ?man arguments: [addr | link | route] [args ...]
// ?man configure and view network devices, routing, and tunnels
// ?man ## COMMANDS
// ?man ### addr [show | list [dev <device>]]
// ?man show interface addresses
// ?man ### addr add <ip>/<prefix> dev <device>
// ?man add address to interface
// ?man ### addr del <ip>/<prefix> dev <device>
// ?man delete address from interface
// ?man ### addr flush dev <device>
// ?man flush all addresses from interface
// ?man ### link [show | list [dev <device>]]
// ?man show interface link states
// ?man ### link set <interface> [up | down | mtu <mtu> | address <mac> | arp on|off | multicast on|off | allmulticast on|off | promisc on|off | name <newname> | txqueuelen <qlen>]
// ?man configure interface settings
// ?man ### route [show | list]
// ?man show routing table
// ?man ### route add <prefix> [via <gateway>] [dev <device>] [metric <metric>]
// ?man add route to routing table
// ?man ### route del <prefix> [via <gateway>] [dev <device>] [metric <metric>]
// ?man delete route from routing table
int
main(int argc, char *argv[])
{
	char *obj;

	ARGBEGIN {
	default:
		usage();
	} ARGEND

	if (argc == 0)
		usage();

	obj = argv[0];

	if (strcmp(obj, "addr") == 0 || strcmp(obj, "address") == 0) {
		return do_addr(argc - 1, argv + 1);
	} else if (strcmp(obj, "link") == 0) {
		return do_link(argc - 1, argv + 1);
	} else if (strcmp(obj, "route") == 0) {
		return do_route(argc - 1, argv + 1);
	}

	usage();

	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		return 1;

	return 0;
}
