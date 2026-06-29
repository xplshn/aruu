/* See LICENSE file for copyright and license details. */


#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

int net_get_interfaces(struct NetInterface **, int *);
int net_get_stats(const char *, struct NetStats *);
int net_set_txqueuelen(const char *, int);

static void
usage(void)
{
	eprintf("usage: %s [-a] [interface [action ...]]\n", argv0);
}

static void
display_interface(const struct NetInterface *iface)
{
	struct NetStats stats;
	char ipv6_str[INET6_ADDRSTRLEN];

	printf("%-9s Link encap:", iface->name);
	if (iface->has_mac) {
		printf("Ethernet  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       iface->mac[0], iface->mac[1], iface->mac[2],
		       iface->mac[3], iface->mac[4], iface->mac[5]);
	} else if (iface->flags & IFF_LOOPBACK) {
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
	if (iface->flags & IFF_UP) printf("UP ");
	if (iface->flags & IFF_BROADCAST) printf("BROADCAST ");
	if (iface->flags & IFF_DEBUG) printf("DEBUG ");
	if (iface->flags & IFF_LOOPBACK) printf("LOOPBACK ");
	if (iface->flags & IFF_POINTOPOINT) printf("POINTOPOINT ");
	if (iface->flags & IFF_RUNNING) printf("RUNNING ");
	if (iface->flags & IFF_NOARP) printf("NOARP ");
	if (iface->flags & IFF_PROMISC) printf("PROMISC ");
	if (iface->flags & IFF_ALLMULTI) printf("ALLMULTI ");
	if (iface->flags & IFF_MULTICAST) printf("MULTICAST ");

	printf(" MTU:%d  Metric:%d\n", iface->mtu, iface->metric);

	if (net_get_stats(iface->name, &stats) >= 0) {
		printf("        RX packets:%llu errors:%llu dropped:%llu overruns:0 frame:0\n",
		       stats.rx_packets, stats.rx_errs, stats.rx_drop);
		printf("        TX packets:%llu errors:%llu dropped:%llu overruns:0 carrier:0\n",
		       stats.tx_packets, stats.tx_errs, stats.tx_drop);
		printf("        RX bytes:%llu  TX bytes:%llu\n",
		       stats.rx_bytes, stats.tx_bytes);
	}
	printf("\n");
}

static void
list_interfaces(int all)
{
	struct NetInterface *ifaces = NULL;
	int count = 0;
	int i;

	if (net_get_interfaces(&ifaces, &count) < 0)
		eprintf("net_get_interfaces:");

	for (i = 0; i < count; i++) {
		if (all || (ifaces[i].flags & IFF_UP)) {
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
	struct NetInterface *ifaces = NULL;
	const struct NetInterface *iface = NULL;
	struct ifreq ifr;
	struct sockaddr_in *sin;
	char *name;
	char *arg;
	char *slash;
	int aflag = 0;
	int sock = -1;
	int i = 1;
	int prefix;
	int count = 0;
	unsigned int mask;

	ARGBEGIN {
	// ?man -a: print or show all entries
	case 'a':
		aflag = 1;
		break;
	default:
		usage();
	} ARGEND

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

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		eprintf("socket:");

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	while (i < argc) {
		arg = argv[i];

		if (strcmp(arg, "up") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "down") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_UP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "netmask") == 0) {
			if (i + 1 >= argc) eprintf("netmask needs an address\n");
			sin = (struct sockaddr_in *)&ifr.ifr_netmask;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) eprintf("ioctl SIOCSIFNETMASK:");
			i += 2;
		} else if (strcmp(arg, "broadcast") == 0) {
			if (i + 1 >= argc) eprintf("broadcast needs an address\n");
			sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFBRDADDR, &ifr) < 0) eprintf("ioctl SIOCSIFBRDADDR:");
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_BROADCAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i += 2;
		} else if (strcmp(arg, "mtu") == 0) {
			if (i + 1 >= argc) eprintf("mtu needs a value\n");
			ifr.ifr_mtu = atoi(argv[i + 1]);
			if (ioctl(sock, SIOCSIFMTU, &ifr) < 0) eprintf("ioctl SIOCSIFMTU:");
			i += 2;
		} else if (strcmp(arg, "metric") == 0) {
			if (i + 1 >= argc) eprintf("metric needs a value\n");
			ifr.ifr_metric = atoi(argv[i + 1]);
			if (ioctl(sock, SIOCSIFMETRIC, &ifr) < 0) eprintf("ioctl SIOCSIFMETRIC:");
			i += 2;
		} else if (strcmp(arg, "txqueuelen") == 0) {
			if (i + 1 >= argc) eprintf("txqueuelen needs a value\n");
			if (net_set_txqueuelen(name, atoi(argv[i + 1])) < 0)
				eprintf("net_set_txqueuelen:");
			i += 2;
		} else if (strcmp(arg, "dstaddr") == 0 || strcmp(arg, "pointopoint") == 0) {
			if (i + 1 >= argc) eprintf("%s needs an address\n", arg);
			sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFDSTADDR, &ifr) < 0) eprintf("ioctl SIOCSIFDSTADDR:");
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_POINTOPOINT;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i += 2;
		} else if (strcmp(arg, "arp") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_NOARP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-arp") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_NOARP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "promisc") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_PROMISC;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-promisc") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_PROMISC;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "allmulti") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_ALLMULTI;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-allmulti") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_ALLMULTI;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "multicast") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_MULTICAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-multicast") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_MULTICAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else {
			sin = (struct sockaddr_in *)&ifr.ifr_addr;
			sin->sin_family = AF_INET;
			slash = strchr(arg, '/');
			prefix = -1;
			if (slash) {
				*slash = '\0';
				prefix = atoi(slash + 1);
			}
			if (inet_pton(AF_INET, arg, &sin->sin_addr) <= 0) {
				eprintf("unknown action or invalid address: %s\n", arg);
			}
			if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) eprintf("ioctl SIOCSIFADDR:");

			if (prefix >= 0) {
				mask = 0;
				if (prefix > 0)
					mask = htonl(~0U << (32 - prefix));
				sin = (struct sockaddr_in *)&ifr.ifr_netmask;
				sin->sin_family = AF_INET;
				sin->sin_addr.s_addr = mask;
				if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) eprintf("ioctl SIOCSIFNETMASK:");
			}

			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");

			i++;
		}
	}

	free(ifaces);
	close(sock);

	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		return 1;

	return 0;
}
