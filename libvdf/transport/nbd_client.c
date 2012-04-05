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

/*
	Based on nbd-server from the nbd package as well as
	the Windows nbdsrvr written by Folkert van Heusden <folkert@vanheusden.com>
*/

#define LIBVDF_SRC
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vdf.h>
#include "../vdf_drive.h"
#include "../vdf_file.h"
#include "../vdf_transport.h"
#ifdef _WIN32
#define addrinfo	ADDRINFO
#else
#include <pthread.h>
#endif
#include "../vdf_sock.h"
#include "nbd.h"

#define NCF_DELETED		0x01

typedef struct _nbdc_data {
	uint32_t			id;
	int					a_family;
	int					a_socktype;
	int					a_protocol;
	struct sockaddr_in	addr;
	size_t				addrlen;
	vdf_sock			sock;
	char				*buff;
	size_t				bufflen;
} nbdc_data;

static int nbdcli_open(vdf_transport *trans, va_list args) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
	struct sockaddr_in *ap;
#ifndef _WIN32
	struct addrinfo hints, *ai = NULL;
#else
	struct hostent *host;
#endif
	char *addrstr, *portstr;

	sock_init();
	if(trans->driver == VTD_NBD_CLI_ADDR) {
		ap = va_arg(args, struct sockaddr_in*);
		dat->addrlen = va_arg(args, int);
		memcpy(&dat->addr, ap, dat->addrlen);
		dat->a_family = AF_INET;
		dat->a_socktype = SOCK_STREAM;
		dat->a_protocol = 0;
	} else {
		addrstr = va_arg(args, char*);
		portstr = va_arg(args, char*);
#ifndef _WIN32
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_ADDRCONFIG /*| AI_NUMERICSERV*/;
		hints.ai_protocol = IPPROTO_TCP;
		if(getaddrinfo(addrstr, portstr, &hints, &ai) != 0)
			return -1;
		dat->a_family = ai->ai_family;
		dat->a_socktype = ai->ai_socktype;
		dat->a_protocol = ai->ai_protocol;
		memcpy(&dat->addr, ai->ai_addr, ai->ai_addrlen);
		dat->addrlen = ai->ai_addrlen;
		freeaddrinfo(ai);
#else
		/*	getaddrinfo() use seems impossible under Windows as
			wspiapi.h and ws2tcpip.h clash with windows.h */
		host = gethostbyname(addrstr);
		if(host == NULL) {
			return -1;
		}
		if(host->h_addr_list[0] == NULL) {
			return -1;
		}
		dat->addrlen = sizeof(struct sockaddr_in);
		memset(&dat->addr, 0, sizeof(struct sockaddr_in));
		dat->addr.sin_family = host->h_addrtype;
		memcpy(&dat->addr.sin_addr, host->h_addr_list[0], host->h_length);
		dat->addr.sin_port = htons(atoi(portstr));
		dat->a_family = host->h_addrtype;
		dat->a_socktype = SOCK_STREAM;
		dat->a_protocol = 0;
#endif
	}
	dat->sock = INVALID_SOCKET;
	dat->buff = NULL;
	dat->id = trans_get_id();
	return 0;
}

static int nbdcli_close(vdf_transport *trans) {
	return 0;
}

static int nbdcli_start(vdf_transport *trans) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
	int size;
	vdf_tcb_connect tcb_conn;

#if 1
	dat->bufflen = trans->drv->bpc;
#else
	dat->bufflen = 1024;
	if(trans->drv->bps > dat->bufflen)
		bufflen = trans->drv->bps;
#endif
	dat->buff = malloc(dat->bufflen);
	if(dat->buff == NULL)
		return -1;

	dat->sock = socket(dat->a_family, dat->a_socktype, dat->a_protocol);
	if(dat->sock == INVALID_SOCKET) {
		free(dat->buff);
		errno = EIO;
		return -1;
	}
	if(connect(dat->sock, (const struct sockaddr*)&dat->addr, dat->addrlen) == -1) {
fail:
//		perror("");
		errno = EIO;
		free(dat->buff);
		sock_close(dat->sock);
		dat->sock = INVALID_SOCKET;
		//printf("Fail\n");
		return -1;
	}
//	printf("Connected\n");
#ifdef IPPROTO_TCP
	size = 1;
	setsockopt(dat->sock, IPPROTO_TCP, TCP_NODELAY, &size, sizeof(int));
#endif
	if(sock_write(dat->sock, "NBDMAGIC", 8) == -1)
		goto fail;
	if(sock_write64(dat->sock, 0x00420281861253LL) == -1)
		goto fail;
	if(sock_write64(dat->sock, trans->drv->bytes) == -1)
		goto fail;
	if(sock_write32(dat->sock, NBD_FLAG_HAS_FLAGS | NBD_FLAG_READ_ONLY) == -1)
		goto fail;
	memset(dat->buff, 0, 124);
	if(sock_write(dat->sock, dat->buff, 124) == -1)
		goto fail;

	tcb_conn.id = dat->id;
	memcpy(&tcb_conn.addr, &dat->addr, sizeof(struct sockaddr));
	tcb_conn.addrlen = dat->addrlen;
	trans_cback(trans, tcb_connect, &tcb_conn);
	trans_set_active(trans, 1);

	return 0;
}

static int nbdcli_stop(vdf_transport *trans) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
	vdf_tcb_connect tcb_conn;

	if(dat->sock != INVALID_SOCKET) {
		sock_close(dat->sock);
		dat->sock = INVALID_SOCKET;
	}
	if(dat->buff != NULL) {
		free(dat->buff);
		dat->buff = NULL;
	}

	tcb_conn.id = dat->id;
	memcpy(&tcb_conn.addr, &dat->addr, sizeof(struct sockaddr));
	tcb_conn.addrlen = dat->addrlen;
	trans_cback(trans, tcb_disconnect, &tcb_conn);
	trans_set_active(trans, 0);
	return 0;
}

static int nbdcli_nonblock(vdf_transport *trans, int non_block) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
#if 0
	if(dat->sock == INVALID_SOCKET)
		return 0;
	return sock_non_block(dat->sock, non_block);
#else
	return 0;
#endif
}

static int nbdcli_process(vdf_transport *trans) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
	vdf_drive *drv = trans->drv;
	char *buff = dat->buff;
	size_t bufflen = dat->bufflen;
	struct nbd_request request;
	struct nbd_reply reply;
	uint16_t cmd;
	driveoff_t off;
	drivesz_t len, read;
	vdf_tcb_read tcb_rd;

	tcb_rd.id = dat->id;

	reply.magic = htonl(NBD_REPLY_MAGIC);
	reply.error = 0;
	if(trans->flags & VTF_NONBLOCK)
		sock_non_block(dat->sock, 1);
	if(sock_read(dat->sock, &request, sizeof(request)) == -1) {
		if(errno == EAGAIN)
			return 0;
#ifdef WRITE_DEBUG
		printf("Failed read\n");
#endif
		goto fail;
	}
	sock_non_block(dat->sock, 0);
	off = ntohll(request.from);
	cmd = ntohl(request.type) & NBD_CMD_MASK_COMMAND;
	len = ntohl(request.len);
	if(request.magic != ntohl(NBD_REQUEST_MAGIC)) {
#ifdef WRITE_DEBUG
		printf("Incorrect magic: 0x%08x (should be 0x%08x)\n", request.magic, ntohl(NBD_REQUEST_MAGIC));
#endif
		goto fail;
	}
	reply.handle = request.handle;
	switch(cmd) {
		case NBD_CMD_DISC:
			vdf_transport_stop(trans);
			return -1;
		case NBD_CMD_READ:
			if(off + len > drv->bytes)
				return 0;
			tcb_rd.off = off;
			tcb_rd.len = len;
			trans_cback(trans, tcb_read, &tcb_rd);
			if(sock_write(dat->sock, &reply, sizeof(reply)) == -1) {
#ifdef WRITE_DEBUG
				printf("Failed reply write\n");
#endif
				goto fail;
			}
			while(len > 0) {
				read = len;
				if(read > bufflen)
					read = bufflen;
				if(vdf_read_bytes(drv, off, read, buff) == -1) {
#ifdef WRITE_DEBUG
					printf("Failed drive read\n");
#endif
					goto fail;
				}
				if(sock_write(dat->sock, buff, read) == -1) {
#ifdef WRITE_DEBUG
					printf("Failed data write\n");
#endif
					goto fail;
				}
				len -= read;
				off += read;
			}
			break;
		case NBD_CMD_WRITE:
			while(len != 0) {
				read = len;
				if(read > bufflen)
					read = bufflen;
				if(sock_read(dat->sock, buff, read) == -1)
					goto fail;
			}
		case NBD_CMD_FLUSH:
		case NBD_CMD_TRIM:
			if(sock_write(dat->sock, &reply, sizeof(reply)) == -1)
				goto fail;
			break;
		default:
			break;
	}
	return 0;
fail:
	trans_set_error(trans, EIO);
	vdf_transport_stop(trans);
	return -1;
}

#ifndef _WIN32
static int nbdcli_fd(vdf_transport *trans) {
	nbdc_data *dat = trans_data(nbdc_data, trans);
	return dat->sock;
}
#endif

vdf_transdriver transdriver_nbd_client = {
	"NBD Client",
#ifdef _WIN32
	TDF_NONBLOCK,
#else
	TDF_NONBLOCK | TDF_SUPPORT_FD,
#endif
	sizeof(nbdc_data),
	nbdcli_open,
	nbdcli_close,
	nbdcli_start,
	nbdcli_stop,
	nbdcli_nonblock,
	nbdcli_process,
#ifdef _WIN32
	NULL
#else
	nbdcli_fd
#endif
};
