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

#ifndef __VDF_FILE_H
#define __VDF_FILE_H

#include <vdf.h>
#include "list.h"

#define VFF_ROOTDIR		0x01				/* root directory */
#define VFF_VIRT		0x02				/* file is virtual */
#define VFF_LONGNAME	0x04				/* signifies that the long name is not an 8.3 name */
#define VFF_DELETED		0x08				/* structure is invalid */
#define VFF_PENDINGDEL	0x10				/* pending deletion */

#define VFA_VOLLABEL	0x08
#define VFA_USER_ATTR	(VFA_READONLY | VFA_HIDDEN | VFA_SYSTEM | VFA_ATTRIBUTE)	/* attributes the user can change */

struct _vdf_file {
	vdf_drive		*drv;					/* drive that file belongs to */
	char			*name;					/* long name */
	char			shortname[13];			/* short name */
	int				flags;					/* flags */
	int				attr;					/* attributes */
	vdf_file		*parent;				/* parent (containing dir) */
	list_head		dirlist;				/* parents linked list */
	list_head		alllist;				/* all files/dirs linked list */
	time_t			date;					/* datetime */
	filesz_t		size;					/* size in bytes */
	sectcnt_t		clusters;				/* size in clusters */
	sfilesz_t		fatsize;				/* size in bytes for FAT or -1 */
	sclustcnt_t		fatclusters;			/* size in clusters for FAT or -1 */
	cluster_t		start, end;				/* start and end cluster */
	cluster_t		data_end;				/* end cluster of actual data */
	sector_t		startsect;				/* start sector */
	sector_t		endsect;				/* end sector */
	int				fent_start;				/* start file entry (from directory start) */
	int				fent_cnt;				/* number of _extra_ file entries */
	union {
		struct _vdf_file_real {
			char		*path;					/* source path */
		} real;
		struct _vdf_file_virt {
			vdf_file_callback	cback;		/* callback */
			void	*param;
		} virt;
		struct _vdf_dir {
			list_head	entries;			/* file/dir entry linked list */
			int			cnt;				/* entry count */
			int			fent_cnt;			/* number of file entries */
		} dir;
	};
};

extern vdf_file *create_root_dir(vdf_drive *drv);
extern int delete_file(vdf_file *file);
extern int stuff_dosname(char **d, char s);
extern int dir_recalc(vdf_file *file);
extern int file_recalc_dir(vdf_file *file);
extern int file_recalc_offsize(vdf_file *file);

static INLINE int file_is_valid(vdf_file *fil) {
	return (fil != NULL) && !(fil->flags & VFF_DELETED);
}

#endif /* __VDF_FILE_H */
