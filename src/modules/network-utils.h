#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>

static inline int pw_net_parse_address(const char *address, uint16_t port,
		struct sockaddr_storage *addr, socklen_t *len)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int res;
	char port_str[6];

	snprintf(port_str, sizeof(port_str), "%u", port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	res = getaddrinfo(address, port_str, &hints, &result);

	if (res != 0)
		return -EINVAL;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		memcpy(addr, rp->ai_addr, rp->ai_addrlen);
		*len = rp->ai_addrlen;
		break;
	}
	freeaddrinfo(result);

	return 0;
}

static inline int pw_net_get_ip(const struct sockaddr_storage *sa, char *ip, size_t len, bool *ip4, uint16_t *port)
{
	if (sa->ss_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in*)sa;
		inet_ntop(sa->ss_family, &in->sin_addr, ip, len);
		if (port)
			*port = ntohs(in->sin_port);
	} else if (sa->ss_family == AF_INET6) {
		struct sockaddr_in6 *in = (struct sockaddr_in6*)sa;
		inet_ntop(sa->ss_family, &in->sin6_addr, ip, len);
		if (port)
			*port = ntohs(in->sin6_port);
		if (in->sin6_scope_id == 0 || len <= 1)
			goto finish;

		size_t curlen = strlen(ip);
		if (len-(curlen+1) >= IFNAMSIZ) {
			ip += curlen+1;
			ip[-1] = '%';
			if (if_indextoname(in->sin6_scope_id, ip) == NULL)
				ip[-1] = 0;
		}
	} else
		return -EINVAL;
finish:
	if (ip4)
		*ip4 = sa->ss_family == AF_INET;
	return 0;
}

static inline char *pw_net_get_ip_fmt(const struct sockaddr_storage *sa, char *ip, size_t len)
{
	if (pw_net_get_ip(sa, ip, len, NULL, NULL) != 0)
		snprintf(ip, len, "invalid ip");
	return ip;
}

#endif /* NETWORK_UTILS_H */
