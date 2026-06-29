/* See LICENSE file for copyright and license details. */


#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-lu] [-p localport] [host] [port]\n", argv0);
}

static int
resolve(const char *host, const char *port, int family, int socktype,
	int passive, struct sockaddr_storage *addr, socklen_t *addrlen)
{
	struct addrinfo hints, *res;
	int r;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	if (passive)
		hints.ai_flags = AI_PASSIVE;

	if ((r = getaddrinfo(host, port, &hints, &res)) != 0) {
		weprintf("getaddrinfo: %s\n", gai_strerror(r));
		return -1;
	}

	memcpy(addr, res->ai_addr, res->ai_addrlen);
	*addrlen = res->ai_addrlen;
	freeaddrinfo(res);
	return 0;
}

// ?man netcat: read and write data across network connections
// ?man arguments: host [port]
// ?man arbitrary data transmission over tcp or udp
int
main(int argc, char *argv[])
{
	struct sockaddr_storage local_addr, remote_addr;
	socklen_t local_len = sizeof(local_addr),
		  remote_len = sizeof(remote_addr);
	struct pollfd fds[2];
	int listenfd = -1, sockfd = -1;
	int lflag = 0;
	int uflag = 0;
	char *port = NULL;
	char *host = NULL;
	char *local_port = NULL;
	int socktype;
	int n, opt;
	char buf[BUFSIZ];

	ARGBEGIN
	{
	// ?man -l: list in long format
	case 'l':
		lflag = 1;
		break;
	// ?man -p:str: preserve file attributes
	case 'p':
		local_port = EARGF(usage());
		break;
	// ?man -u: unbuffered output
	case 'u':
		uflag = 1;
		break;
	default:
		usage();
	}
	ARGEND

	socktype = uflag ? SOCK_DGRAM : SOCK_STREAM;

	if (lflag) {
		/* server mode */
		if (!local_port) {
			if (argc == 1) {
				local_port = argv[0];
				argc = 0;
			} else {
				usage();
			}
		}
		memset(&local_addr, 0, sizeof(local_addr));
		if (resolve(NULL, local_port, AF_UNSPEC, socktype, 1,
			    &local_addr, &local_len) < 0)
			return 1;

		listenfd = socket(local_addr.ss_family, socktype, 0);
		if (listenfd < 0)
			eprintf("socket:");

		opt = 1;
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt,
			   sizeof(opt));

		if (bind(listenfd, (struct sockaddr *)&local_addr, local_len) <
		    0)
			eprintf("bind:");

		if (!uflag) {
			if (listen(listenfd, 5) < 0)
				eprintf("listen:");
			sockfd = accept(listenfd,
					(struct sockaddr *)&remote_addr,
					&remote_len);
			if (sockfd < 0)
				eprintf("accept:");
			close(listenfd);
		} else {
			sockfd = listenfd;
			n = recvfrom(sockfd, buf, sizeof(buf), MSG_PEEK,
				     (struct sockaddr *)&remote_addr,
				     &remote_len);
			if (n < 0)
				eprintf("recvfrom:");
			if (connect(sockfd, (struct sockaddr *)&remote_addr,
				    remote_len) < 0)
				eprintf("connect:");
		}
	} else {
		/* client mode */
		if (argc != 2)
			usage();
		host = argv[0];
		port = argv[1];

		memset(&remote_addr, 0, sizeof(remote_addr));
		if (resolve(host, port, AF_UNSPEC, socktype, 0, &remote_addr,
			    &remote_len) < 0)
			return 1;

		sockfd = socket(remote_addr.ss_family, socktype, 0);
		if (sockfd < 0)
			eprintf("socket:");

		if (local_port) {
			memset(&local_addr, 0, sizeof(local_addr));
			if (resolve(NULL, local_port, remote_addr.ss_family,
				    socktype, 1, &local_addr, &local_len) < 0)
				return 1;
			opt = 1;
			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt,
				   sizeof(opt));
			if (bind(sockfd, (struct sockaddr *)&local_addr,
				 local_len) < 0)
				eprintf("bind:");
		}

		if (connect(sockfd, (struct sockaddr *)&remote_addr,
			    remote_len) < 0)
			eprintf("connect:");
	}

	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = sockfd;
	fds[1].events = POLLIN;

	while (1) {
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			eprintf("poll:");
		}

		if (fds[0].revents & POLLIN) {
			n = read(0, buf, sizeof(buf));
			if (n < 0) {
				weprintf("read stdin:");
				break;
			}
			if (n == 0) {
				if (!uflag) {
					shutdown(sockfd, SHUT_WR);
					fds[0].fd = -1;
				} else {
					break;
				}
			} else {
				if (writeall(sockfd, buf, n) < 0) {
					weprintf("write socket:");
					break;
				}
			}
		}

		if (fds[1].revents & POLLIN) {
			n = read(sockfd, buf, sizeof(buf));
			if (n < 0) {
				weprintf("read socket:");
				break;
			}
			if (n == 0) {
				break;
			} else {
				if (writeall(1, buf, n) < 0) {
					weprintf("write stdout:");
					break;
				}
			}
		}

		if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
		    (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
			break;
		}
	}

	close(sockfd);
	return 0;
}
