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

#define LIBVDF_SRC
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <vdf.h>
#include "vdf_sock.h"

#ifdef _WIN32
#define SEND_FLAGS		0
#define RECV_FLAGS		0
#else
#define SEND_FLAGS		MSG_NOSIGNAL
#define RECV_FLAGS		MSG_NOSIGNAL
#endif

int sock_write(vdf_sock sock, void *buff, size_t cnt) {
	uint8_t *b = (uint8_t*)buff;
	int wrote, total = 0;
	while(total < cnt) {
		wrote = send(sock, b, cnt, SEND_FLAGS);
		if(wrote == 0)
#if 1
			return -1;		/* is this an error? */
#else
			break;
#endif
		if(wrote == -1)
			return -1;
		b += wrote;
		total += wrote;
	}
	return total;
}

int sock_read(vdf_sock sock, void *buff, size_t cnt) {
	uint8_t *b = (uint8_t*)buff;
	int read, total = 0;
	while(total < cnt) {
		read = recv(sock, b, cnt, RECV_FLAGS);
		if(read == 0)
			break;
		if(read == -1)
			return -1;
		b += read;
		total += read;
	}
	return total;
}

int sock_close(vdf_sock sock) {
#ifdef _WIN32
	closesocket(sock);
#else
	close(sock);
#endif
	return 0;
}

int sock_non_block(vdf_sock sock, int non_block) {
#ifdef _WIN32
	ULONG nb = non_block;
	return ioctlsocket(sock, FIONBIO, &nb) == 0 ? 0 : -1;
#else
	/* adapted from http://www.kegel.com/dkftpbench/nonblocking.html */
	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	/* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
	int flags = fcntl(sock, F_GETFL, 0);
	if(flags == -1)
		flags = 0;
	if(non_block)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	return fcntl(sock, F_SETFL, flags);
#else
	/* Otherwise, use the old way of doing it */
	return ioctl(sock, FIOBIO, &non_block);
#endif
#endif
}
