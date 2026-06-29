/* See LICENSE file for copyright and license details. */


#include "util.h"
#include "arg.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct RRType {
	const char *name;
	const char *msg;
	int type;
};

static const struct RRType rrtypes[] = {
	{ "A", "has address", 1 },
	{ "NS", "name server", 2 },
	{ "CNAME", "is a nickname for", 5 },
	{ "SOA", "start of authority", 6 },
	{ "PTR", "domain name pointer", 12 },
	{ "MX", "mail is handled by", 15 },
	{ "TXT", "descriptive text", 16 },
	{ "AAAA", "has IPv6 address", 28 },
	{ "SRV", "has SRV record", 33 },
	{ "ANY", "has ANY record", 255 }
};

static void
usage(void)
{
	eprintf("usage: %s [-t type] name [server]\n", argv0);
}

static int
load_nameservers(char **ns, int max_ns)
{
	FILE *fp = fopen("/etc/resolv.conf", "r");
	char line[256];
	char *p, *end;
	int count = 0;

	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) && count < max_ns) {
		if (strncmp(line, "nameserver", 10) == 0 && isspace(line[10])) {
			p = line + 11;
			while (isspace(*p))
				p++;
			end = p;
			while (*end && !isspace(*end) && *end != '#')
				end++;
			*end = '\0';
			if (*p) {
				ns[count++] = estrdup(p);
			}
		}
	}
	fclose(fp);
	return count;
}

static char *
reverse_ip(const char *ip_str, int *type)
{
	struct in_addr addr4;
	struct in6_addr addr6;
	unsigned char *p;
	char *buf = emalloc(256);
	int i, j;

	if (inet_pton(AF_INET, ip_str, &addr4) > 0) {
		p = (unsigned char *)&addr4.s_addr;
		sprintf(buf, "%d.%d.%d.%d.in-addr.arpa", p[3], p[2], p[1], p[0]);
		*type = 12;
		return buf;
	} else if (inet_pton(AF_INET6, ip_str, &addr6) > 0) {
		p = (unsigned char *)&addr6.s6_addr;
		j = 0;
		for (i = 15; i >= 0; i--) {
			j += sprintf(buf + j, "%x.%x.", p[i] & 15, p[i] >> 4);
		}
		strcpy(buf + j, "ip6.arpa");
		*type = 12;
		return buf;
	}

	free(buf);
	return NULL;
}

static int
send_query(const char *ns, const char *name, int type, unsigned char *resp, int resp_len)
{
	struct addrinfo hints, *res;
	unsigned char query[512];
	struct timeval tv;
	int qlen;
	int sock = -1;
	int r;

	qlen = res_mkquery(0, name, 1, type, NULL, 0, NULL, query, sizeof(query));
	if (qlen < 0) {
		weprintf("res_mkquery failed for %s\n", name);
		return -1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	r = getaddrinfo(ns, "53", &hints, &res);
	if (r != 0) {
		weprintf("getaddrinfo %s: %s\n", ns, gai_strerror(r));
		return -1;
	}

	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
		freeaddrinfo(res);
		return -1;
	}

	tv.tv_sec = 5;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		close(sock);
		freeaddrinfo(res);
		return -1;
	}

	if (send(sock, query, qlen, 0) < 0) {
		close(sock);
		freeaddrinfo(res);
		return -1;
	}

	r = recv(sock, resp, resp_len, 0);
	close(sock);
	freeaddrinfo(res);

	return r;
}

static unsigned short
peek_be(const unsigned char *p)
{
	return (p[0] << 8) | p[1];
}

static int
parse_response(const unsigned char *resp, int resp_len, const char *query_name)
{
	const unsigned char *p;
	char name[256];
	char target[256];
	char ipv6[64];
	char mname[256], rname[256];
	const char *err;
	const char *msg;
	struct in_addr a;
	int rcode, qdcount, ancount;
	int i, n, type, rdlen, pref, n1, n2;
	unsigned int ttl;

	if (resp_len < 12) {
		weprintf("response too short: %d\n", resp_len);
		return -1;
	}

	rcode = resp[3] & 0x0f;
	if (rcode != 0) {
		err = "Unknown error";
		switch (rcode) {
		case 1: err = "Format error"; break;
		case 2: err = "Server failure"; break;
		case 3: err = "Non-existent domain"; break;
		case 4: err = "Not implemented"; break;
		case 5: err = "Refused"; break;
		}
		eprintf("Host not found: %s\n", err);
	}

	qdcount = peek_be(resp + 4);
	ancount = peek_be(resp + 6);

	p = resp + 12;
	for (i = 0; i < qdcount; i++) {
		n = dn_expand(resp, resp + resp_len, p, name, sizeof(name));
		if (n < 0)
			return -1;
		p += n + 4;
	}

	if (ancount == 0) {
		printf("%s has no records\n", query_name);
		return 0;
	}

	for (i = 0; i < ancount; i++) {
		n = dn_expand(resp, resp + resp_len, p, name, sizeof(name));
		if (n < 0)
			return -1;
		p += n;
		type = peek_be(p);
		ttl = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
		rdlen = peek_be(p + 8);
		p += 10;

		(void)ttl;

		if (type == 1) {
			memcpy(&a, p, 4);
			printf("%s has address %s\n", name, inet_ntoa(a));
		} else if (type == 28) {
			inet_ntop(AF_INET6, p, ipv6, sizeof(ipv6));
			printf("%s has IPv6 address %s\n", name, ipv6);
		} else if (type == 2 || type == 5 || type == 12) {
			dn_expand(resp, resp + resp_len, p, target, sizeof(target));
			msg = "has record";
			if (type == 2) msg = "name server";
			else if (type == 5) msg = "is a nickname for";
			else if (type == 12) msg = "domain name pointer";
			printf("%s %s %s\n", name, msg, target);
		} else if (type == 15) {
			pref = peek_be(p);
			dn_expand(resp, resp + resp_len, p + 2, target, sizeof(target));
			printf("%s mail is handled by %d %s\n", name, pref, target);
		} else if (type == 16) {
			printf("%s descriptive text \"%.*s\"\n", name, (int)p[0], (char *)(p + 1));
		} else if (type == 6) {
			n1 = dn_expand(resp, resp + resp_len, p, mname, sizeof(mname));
			n2 = dn_expand(resp, resp + resp_len, p + n1, rname, sizeof(rname));
			(void)n2;
			printf("%s start of authority %s %s\n", name, mname, rname);
		} else {
			printf("%s has unsupported record type %d\n", name, type);
		}

		p += rdlen;
	}

	return 0;
}

// ?man host: dns lookup utility
// ?man arguments: name [server]
// ?man look up hostnames and IP addresses using dns
int
main(int argc, char *argv[])
{
	unsigned char resp[65536];
	char *ns[8];
	char *tflag = NULL;
	char *name;
	char *query_name;
	char *rev;
	int ns_count = 0;
	int type = 1;
	int i, r;

	ARGBEGIN {
	// ?man -t:str: sort or specify timestamp
	case 't':
		tflag = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (argc < 1)
		usage();

	query_name = argv[0];

	if (argc > 1) {
		ns[0] = argv[1];
		ns_count = 1;
	} else {
		ns_count = load_nameservers(ns, 8);
		if (ns_count == 0)
			eprintf("no nameservers found in /etc/resolv.conf\n");
	}

	rev = reverse_ip(query_name, &type);
	if (rev) {
		name = rev;
	} else {
		name = query_name;
		if (tflag) {
			type = -1;
			for (i = 0; i < (int)LEN(rrtypes); i++) {
				if (strcasecmp(tflag, rrtypes[i].name) == 0) {
					type = rrtypes[i].type;
					break;
				}
			}
			if (type == -1)
				eprintf("invalid record type: %s\n", tflag);
		}
	}

	for (i = 0; i < ns_count; i++) {
		r = send_query(ns[i], name, type, resp, sizeof(resp));
		if (r > 0) {
			parse_response(resp, r, query_name);
			break;
		}
	}

	if (rev)
		free(rev);

	if (argc == 1) {
		for (i = 0; i < ns_count; i++) {
			free(ns[i]);
		}
	}

	return r > 0 ? 0 : 1;
}
