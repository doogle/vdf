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
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vdf.h>
#include "vdf_drive.h"
#include "vdf_file.h"
#include "vdf_transport.h"

//#define TRANSPORT_NBD_LOCAL
#define TRANSPORT_NBD_SERVER
#define TRANSPORT_NBD_CLIENT
//#define TRANSPORT_VDFP

#ifdef TRANSPORT_NBD_LOCAL
	extern vdf_transdriver transdriver_nbd_local;
#endif
#ifdef TRANSPORT_NBD_SERVER
	extern vdf_transdriver transdriver_nbd_server;
#endif
#ifdef TRANSPORT_NBD_CLIENT
	extern vdf_transdriver transdriver_nbd_client;
#endif
#ifdef TRANSPORT_VDFP
	extern vdf_transdriver transdriver_vdfp;
#endif

vdf_transdriver *drivers[vdf_driver_cnt] = {
#ifdef TRANSPORT_NBD_LOCAL
	&transdriver_nbd_local,
#else
	NULL,
#endif
#ifdef TRANSPORT_NBD_SERVER
	&transdriver_nbd_server,
#else
	NULL,
#endif
#ifdef TRANSPORT_NBD_CLIENT
	&transdriver_nbd_client,
	&transdriver_nbd_client,
#else
	NULL,
	NULL,
#endif
#ifdef TRANSPORT_VDFP
	&transdriver_vdfp,
	&transdriver_vdfp,
#else
	NULL,
	NULL,
#endif
};

static uint32_t next_id = 0;

uint32_t trans_get_id(void) {
	return ++next_id;
}

LIBFUNC int vdf_transport_present(vdf_driver driver) {
	if(driver >= vdf_driver_cnt)
		return -1;
	if(drivers[driver] == NULL)
		return 0;
	return 1;
}

LIBFUNC int vdf_transport_flags(vdf_driver driver) {
	if(driver >= vdf_driver_cnt)
		return -1;
	if(drivers[driver] == NULL)
		return -1;
	return drivers[driver]->flags;
}

LIBFUNC ssize_t vdf_transport_name(vdf_driver driver, char *name, size_t len) {
	ssize_t l;
	if(driver >= vdf_driver_cnt)
		return -1;
	if(drivers[driver] == NULL)
		return -1;
	l = strlen(drivers[driver]->name);
	if(name == NULL)
		return l;
	if(len <= l) {
		errno = E2BIG;
		return -1;
	}
	strcpy(name, drivers[driver]->name);
	return l;
}

LIBFUNC vdf_transport *vdf_transport_open(vdf_driver driver, vdf_drive *drv, vdf_transport_cback cback, ...) {
	vdf_transport *trans;
	va_list args;

	if(driver >= vdf_driver_cnt) {
		errno = EINVAL;
		return NULL;
	}
	if(drivers[driver] == NULL) {
		errno = EINVAL;
		return NULL;
	}
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return NULL;
	}
	trans = malloc(sizeof(vdf_transport) + drivers[driver]->extra_size);
	if(trans == NULL)
		return NULL;
	trans->flags = 0;
	trans->driver = driver;
	trans->drv = drv;
	memset(&trans->driver_data, 0, drivers[driver]->extra_size);
	trans->td = drivers[driver];
	trans->cback = cback;
	va_start(args, cback);
	if(trans->td->open(trans, args) < 0) {
		va_end(args);
		free(trans);
		return NULL;
	}
	va_end(args);
	list_add(&trans->drivelist, &drv->transports);
	return trans;
}

LIBFUNC int vdf_transport_close(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(trans->flags & VTF_RUNNING) {
		vdf_transport_stop(trans);
	}
	trans->td->close(trans);
	trans->flags |= VTF_DELETED;
	list_del(&trans->drivelist);
	free(trans);
	return 0;
}

LIBFUNC int vdf_transport_start(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(trans->flags & VTF_RUNNING) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_lock(trans->drv) == -1)
		return -1;
	trans->flags &= ~VTF_ERROR;
	trans->err = 0;
	if(trans->td->start(trans) == -1)
		return -1;
	trans->flags |= VTF_RUNNING;
	return 0;
}

LIBFUNC int vdf_transport_stop(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(!(trans->flags & VTF_RUNNING)) {
		errno = EINVAL;
		return -1;
	}
	if(trans->td->stop(trans) == -1)
		return -1;
	if(vdf_drive_unlock(trans->drv) == -1)
		return -1;
	trans->flags &= ~(VTF_RUNNING | VTF_ACTIVE);
	return 0;
}

LIBFUNC int vdf_transport_running(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	return !!(trans->flags & VTF_RUNNING);
}

LIBFUNC int vdf_transport_error(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	return trans->err;
}

LIBFUNC int vdf_set_transport_non_block(vdf_transport *trans, int non_block) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(!(trans->td->flags & TDF_NONBLOCK)) {
		errno = EINVAL;
		return -1;
	}
	if(trans->td->nonblock(trans, non_block) == -1)
		return -1;
	if(non_block)
		trans->flags |= VTF_NONBLOCK;
	else
		trans->flags &= ~VTF_NONBLOCK;
	return 0;
}

LIBFUNC int vdf_transport_process(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(!(trans->flags & VTF_RUNNING)) {
		errno = EINVAL;
		return -1;
	}
	if(trans->flags & VTF_ERROR) {
		errno = EINVAL;
		return -1;
	}
	return trans->td->process(trans);
}

LIBFUNC int vdf_get_transport_fd(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	if(!(trans->td->flags & TDF_SUPPORT_FD)) {
		errno = EINVAL;
		return -1;
	}
	return trans->td->fd(trans);
}

LIBFUNC int vdf_transport_active(vdf_transport *trans) {
	if(!trans_is_valid(trans)) {
		errno = EINVAL;
		return -1;
	}
	return !!(trans->flags & VTF_ACTIVE);
}

void trans_set_error(vdf_transport *trans, int err) {
	trans->flags |= VTF_ERROR;
	trans->err = err;
}

void trans_set_active(vdf_transport *trans, int active) {
	if(active)
		trans->flags |= VTF_ACTIVE;
	else
		trans->flags &= ~VTF_ACTIVE;
}

LIBFUNC int vdf_drive_start(vdf_drive *drv) {
	vdf_transport *trans;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	list_foreach_item(vdf_transport, trans, &drv->transports, drivelist) {
		if(vdf_transport_start(trans))
			return -1;
	}
	return 0;
}

LIBFUNC int vdf_drive_stop(vdf_drive *drv) {
	vdf_transport *trans;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	list_foreach_item(vdf_transport, trans, &drv->transports, drivelist) {
		if(vdf_transport_stop(trans))
			return -1;
	}
	return 0;
}

LIBFUNC int vdf_drive_active(vdf_drive *drv) {
	vdf_transport *trans;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	list_foreach_item(vdf_transport, trans, &drv->transports, drivelist) {
		if(vdf_transport_active(trans))
			return 1;
	}
	return 0;
}

LIBFUNC int vdf_drive_running(vdf_drive *drv) {
	vdf_transport *trans;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	list_foreach_item(vdf_transport, trans, &drv->transports, drivelist) {
		if(vdf_transport_running(trans))
			return 1;
	}
	return 0;
}

