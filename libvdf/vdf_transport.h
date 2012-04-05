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

#ifndef __VDF_TRANSPORT_H
#define __VDF_TRANSPORT_H

#include <stdarg.h>
#include <vdf.h>
#include "vdf_private.h"
#include "list.h"

typedef int (*trans_open)(vdf_transport *trans, va_list args);
typedef int (*trans_close)(vdf_transport *trans);
typedef int (*trans_start)(vdf_transport *trans);
typedef int (*trans_stop)(vdf_transport *trans);
typedef int (*trans_nonblock)(vdf_transport *trans, int non_block);
typedef int (*trans_process)(vdf_transport *trans);
typedef int (*trans_fd)(vdf_transport *trans);

typedef struct _vdf_transdriver {
	char			*name;
	int				flags;
	size_t			extra_size;
	trans_open		open;
	trans_close		close;
	trans_start		start;
	trans_stop		stop;
	trans_nonblock	nonblock;
	trans_process	process;
	trans_fd		fd;
} vdf_transdriver;

#define VTF_RUNNING		0x01
#define VTF_ERROR		0x02
#define VTF_NONBLOCK	0x04
#define VTF_ACTIVE		0x08
#define VTF_DELETED		0x10

struct _vdf_transport {
	int				flags;
	vdf_driver		driver;
	vdf_transdriver	*td;
	vdf_drive		*drv;
	vdf_transport_cback cback;
	list_head		drivelist;
	int				err;
	char			driver_data[0];
};

#define trans_data(type, trans)			((type*)(((char*)trans) + offsetof(vdf_transport, driver_data)))
extern void trans_set_error(vdf_transport *trans, int err);
extern void trans_set_active(vdf_transport *trans, int active);
extern uint32_t trans_get_id(void);

static INLINE int trans_is_valid(vdf_transport *trans) {
	return (trans != NULL) && !(trans->flags & VTF_DELETED);
}

static INLINE int trans_cback(vdf_transport *trans, vdf_tcb_type type, void *data) {
	if(trans->cback == NULL)
		return 0;
	return trans->cback(trans, trans->drv, type, data);
}

#endif /* __VDF_TRANSPORT_H */
