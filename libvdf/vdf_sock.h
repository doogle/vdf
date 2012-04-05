/*
	Copyright (C) 2012 David Steinberg <doogle2600@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __VDF_SOCK_H
#define __VDF_SOCK_H

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#endif
#include "vdf_private.h"

#ifdef _WIN32
typedef SOCKET	vdf_sock;
#else
typedef int		vdf_sock;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET	(-1)
#endif
#endif

extern int sock_write(vdf_sock sock, void *buff, size_t cnt);
extern int sock_read(vdf_sock sock, void *buff, size_t cnt);
extern int sock_close(vdf_sock sock);
extern int sock_non_block(vdf_sock sock, int non_block);

#if 0
static INLINE uint64_t htonll(uint64_t val) {
	short word = 0x4321;
	if((*(char *)&word) != 0x21)
		return val;
	return (((uint64_t)htonl(val)) << 32) | htonl(val >> 32);
}
#else
#ifdef WORDS_BIGENDIAN
static INLINE uint64_t INLINE ntohll(uint64_t a) {
	return a;
}
#else
static INLINE uint64_t ntohll(uint64_t a) {
	uint64_t lo = a & 0xffffffffULL;
	uint64_t hi = a >> 32U;
	lo = ntohl(lo);
	hi = ntohl(hi);
	return ((uint64_t) lo) << 32U | hi;
}
#endif
#endif
#define htonll ntohll

static INLINE int sock_write8(vdf_sock sock, uint8_t val) {
	return sock_write(sock, &val, sizeof(val));
}

static INLINE int sock_write16(vdf_sock sock, uint16_t val) {
	val = htons(val);
	return sock_write(sock, &val, sizeof(val));
}

static INLINE int sock_write32(vdf_sock sock, uint32_t val) {
	val = htonl(val);
	return sock_write(sock, &val, sizeof(val));
}

static INLINE int sock_write64(vdf_sock sock, uint64_t val) {
	val = htonll(val);
	return sock_write(sock, &val, sizeof(val));
}

static INLINE int sock_read8(vdf_sock sock, uint8_t *val) {
	if(sock_read(sock, &val, sizeof(*val)) != sizeof(*val))
		return -1;
	return 0;
}

static INLINE int sock_read16(vdf_sock sock, uint16_t *val) {
	if(sock_read(sock, &val, sizeof(*val)) != sizeof(*val))
		return -1;
	*val = ntohs(*val);
	return 0;
}

static INLINE int sock_read32(vdf_sock sock, uint32_t *val) {
	if(sock_read(sock, &val, sizeof(*val)) != sizeof(*val))
		return -1;
	*val = ntohl(*val);
	return 0;
}

static INLINE int sock_read64(vdf_sock sock, uint64_t *val) {
	if(sock_read(sock, &val, sizeof(*val)) != sizeof(*val))
		return -1;
	*val = ntohll(*val);
	return 0;
}

static INLINE void sock_init(void) {
#ifdef _WIN32
	WSADATA WSAData;
	(void)WSAStartup(0x101, &WSAData); 
#endif
}

#endif /* __VDF_SOCK_H */
