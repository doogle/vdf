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
#ifndef _WIN32
#include <pthread.h>
#endif
#include "../vdf_sock.h"
#include "nbd.h"

#define NCF_DELETED		0x01

typedef struct _nbd_client {
	int					flags;
	uint32_t			id;
	struct sockaddr_in	addr;
	size_t				addrlen;
	list_head			list;
	vdf_sock			sock;
#ifdef _WIN32
	HANDLE				thread;
#else
	pthread_t			thread;
#endif
	vdf_transport		*trans;
} nbd_client;

typedef struct _nbds_data {
	int					port;
	vdf_sock			srvsock;
	list_head			clients;
	int					num_clients;
} nbds_data;

static void free_client(nbd_client *cli);
static void stop_client(nbd_client *cli);

static int nbdserv_open(vdf_transport *trans, va_list args) {
	nbds_data *dat = trans_data(nbds_data, trans);

	sock_init();
	dat->port = va_arg(args, int);
	INIT_LIST_HEAD(&dat->clients);
	dat->num_clients = 0;
	return 0;
}

static int nbdserv_close(vdf_transport *trans) {
	return 0;
}

static int nbdserv_start(vdf_transport *trans) {
	nbds_data *dat = trans_data(nbds_data, trans);
	struct sockaddr_in addr;

	dat->srvsock = socket(AF_INET, SOCK_STREAM, 0);
	if(dat->srvsock == INVALID_SOCKET) {
		errno = EIO;
		return -1;
	}
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
#ifdef _WIN32
	addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
#else
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
	addr.sin_port = htons(dat->port);
	dat->num_clients = 0;
	if(bind(dat->srvsock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1) {
		sock_close(dat->srvsock);
		dat->srvsock = INVALID_SOCKET;
		errno = EIO;
		return -1;
	}
	if(listen(dat->srvsock, 5) == -1) {
		sock_close(dat->srvsock);
		dat->srvsock = INVALID_SOCKET;
		errno = EIO;
		return -1;
	}
	if(trans->flags & VTF_NONBLOCK)
		sock_non_block(dat->srvsock, 1);
	return 0;
}

static int nbdserv_stop(vdf_transport *trans) {
	nbds_data *dat = trans_data(nbds_data, trans);
	nbd_client *cli, *cli_n;

	list_foreach_item_safe(nbd_client, cli, cli_n, &dat->clients, list) {
		stop_client(cli);
	}

	sock_close(dat->srvsock);
	dat->srvsock = INVALID_SOCKET;
	return 0;
}

static int nbdserv_nonblock(vdf_transport *trans, int non_block) {
	nbds_data *dat = trans_data(nbds_data, trans);
	if(dat->srvsock == INVALID_SOCKET)
		return 0;
	return sock_non_block(dat->srvsock, non_block);
}

static void free_client(nbd_client *cli) {
	vdf_transport *trans;
	nbds_data *dat;
	vdf_tcb_connect tcb_conn;

	if(cli->flags & NCF_DELETED)
		return;
	trans = cli->trans;
	dat = trans_data(nbds_data, trans);

	tcb_conn.id = cli->id;
	memcpy(&tcb_conn.addr, &cli->addr, sizeof(struct sockaddr));
	tcb_conn.addrlen = cli->addrlen;
	trans_cback(trans, tcb_disconnect, &tcb_conn);

	cli->flags |= NCF_DELETED;
	list_del(&cli->list);
	if(--dat->num_clients == 0)
		trans_set_active(trans, 0);
	if(cli->sock != INVALID_SOCKET)
		sock_close(cli->sock);
	cli->sock = INVALID_SOCKET;
	free(cli);
}

static void stop_client(nbd_client *cli) {
	vdf_transport *trans;
	nbds_data *dat;
	vdf_sock sock;

	trans = cli->trans;
	dat = trans_data(nbds_data, trans);

	sock = cli->sock;
	cli->sock = INVALID_SOCKET;
	if(sock != INVALID_SOCKET)
		sock_close(sock);
#ifdef _WIN32
	WaitForSingleObject(cli->thread, INFINITE);
#else
	pthread_join(cli->thread, NULL);
#endif
}

#ifdef _WIN32
static DWORD WINAPI client_threadfunc(LPVOID _clidata) {
#else
static void *client_threadfunc(void *_clidata) {
#endif
	nbd_client *cli = (nbd_client*)_clidata;
	vdf_transport *trans = cli->trans;
	nbds_data *dat = trans_data(nbds_data, trans);
	vdf_drive *drv = trans->drv;
	char *buff;
	size_t bufflen;
	struct nbd_request request;
	struct nbd_reply reply;
	uint16_t cmd;
	driveoff_t off;
	size_t len, read;
	vdf_tcb_read tcb_rd;

#if 1
	bufflen = drv->bpc;
#else
	bufflen = 1024;
	if(drv->bps > bufflen)
		bufflen = drv->bps;
#endif
	buff = malloc(bufflen);
	if(buff == NULL)
		goto finish;

	tcb_rd.id = cli->id;

	if(sock_write(cli->sock, "NBDMAGIC", 8) == -1)
		goto finish;
	if(sock_write64(cli->sock, 0x00420281861253LL) == -1)
		goto finish;
	if(sock_write64(cli->sock, drv->bytes) == -1)
		goto finish;
	if(sock_write32(cli->sock, NBD_FLAG_HAS_FLAGS | NBD_FLAG_READ_ONLY) == -1)
		goto finish;
	memset(buff, 0, 124);
	if(sock_write(cli->sock, buff, 124) == -1)
		goto finish;

	reply.magic = htonl(NBD_REPLY_MAGIC);
	reply.error = 0;
	while(1) {
		if(sock_read(cli->sock, &request, sizeof(request)) == -1) {
#ifdef WRITE_DEBUG
			printf("Failed read\n");
#endif
			goto finish;
		}
		off = ntohll(request.from);
		cmd = ntohl(request.type) & NBD_CMD_MASK_COMMAND;
		len = ntohl(request.len);
		if(request.magic != ntohl(NBD_REQUEST_MAGIC)) {
#ifdef WRITE_DEBUG
			printf("Incorrect magic: 0x%08x (should be 0x%08x)\n", request.magic, ntohl(NBD_REQUEST_MAGIC));
#endif
			goto finish;
		}
		reply.handle = request.handle;
		switch(cmd) {
			case NBD_CMD_DISC:
				goto finish;
			case NBD_CMD_READ:
				if(off + len > drv->bytes)
					continue;
				tcb_rd.off = off;
				tcb_rd.len = len;
				trans_cback(trans, tcb_read, &tcb_rd);
				if(sock_write(cli->sock, &reply, sizeof(reply)) == -1) {
#ifdef WRITE_DEBUG
					printf("Failed reply write\n");
#endif
					goto finish;
				}
				while(len > 0) {
					read = len;
					if(read > bufflen)
						read = bufflen;
					if(vdf_read_bytes(drv, off, read, buff) == -1) {
#ifdef WRITE_DEBUG
						printf("Failed drive read\n");
#endif
						goto finish;
					}
					if(sock_write(cli->sock, buff, read) == -1) {
#ifdef WRITE_DEBUG
						printf("Failed data write\n");
#endif
						goto finish;
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
					if(sock_read(cli->sock, buff, read) == -1)
						goto finish;
				}
			case NBD_CMD_FLUSH:
			case NBD_CMD_TRIM:
				if(sock_write(cli->sock, &reply, sizeof(reply)) == -1)
					goto finish;
				break;
			default:
				break;
		}
	}
finish:
	free_client(cli);
	return 0;
}

static int nbdserv_process(vdf_transport *trans) {
	nbds_data *dat = trans_data(nbds_data, trans);
	nbd_client *cli;
	struct sockaddr_in addr;
	size_t addrlen;
	vdf_sock sock;
	int tid;
	vdf_tcb_connect tcb_conn;

	addrlen = sizeof(struct sockaddr_in);
	sock = accept(dat->srvsock, (struct sockaddr*)&addr, &addrlen);
	if(sock != INVALID_SOCKET) {
		cli = malloc(sizeof(nbd_client));
		if(cli == NULL) {
			trans_set_error(trans, ENOMEM);
			errno = ENOMEM;
			return -1;
		}
		memcpy(&cli->addr, &addr, sizeof(addr));
		cli->flags = 0;
		cli->addrlen = addrlen;
		cli->sock = sock;
		cli->trans = trans;
		cli->id = trans_get_id();
#ifdef _WIN32
		cli->thread = CreateThread(NULL, 0, client_threadfunc, cli, 0, &tid);
		if(cli->thread == NULL) {
			errno = ENOEXEC;
#else
		tid = pthread_create(&cli->thread, NULL, client_threadfunc, cli);
		if(tid == 0) {
			errno = -tid;
#endif
			trans_set_error(trans, errno);
			sock_close(sock);
			free(cli);
			return -1;
		}
		list_add_tail(&cli->list, &dat->clients);
		dat->num_clients++;
		tcb_conn.id = cli->id;
		memcpy(&tcb_conn.addr, &addr, sizeof(struct sockaddr));
		tcb_conn.addrlen = addrlen;
		trans_cback(trans, tcb_connect, &tcb_conn);
		trans_set_active(trans, 1);
	}
	return 0;
}

#ifndef _WIN32
static int nbdserv_fd(vdf_transport *trans) {
	nbds_data *dat = trans_data(nbds_data, trans);
	return dat->srvsock;
}
#endif

vdf_transdriver transdriver_nbd_server = {
	"NBD Server",
#ifdef _WIN32
	TDF_NONBLOCK,
#else
	TDF_NONBLOCK | TDF_SUPPORT_FD,
#endif
	sizeof(nbds_data),
	nbdserv_open,
	nbdserv_close,
	nbdserv_start,
	nbdserv_stop,
	nbdserv_nonblock,
	nbdserv_process,
#ifdef _WIN32
	NULL
#else
	nbdserv_fd
#endif
};
