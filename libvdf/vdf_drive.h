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

#ifndef __VDF_DRIVE_H
#define __VDF_DRIVE_H

#include <vdf.h>
#include "vdf_private.h"
#include "list.h"

#define VDF_DIRTY			0x10000
#define VDF_DELETED			0x20000

#define DEFAULT_DRIVE_SERIAL	0x12345678

typedef struct _vdf_filerange {
	sector_t		sectstart;					/* sector start */
	sector_t		sectend;					/* sector end */
	cluster_t		fatstart;					/* FAT index start */
	cluster_t		fatend;						/* FAT index end */
	vdf_file		*file;
} vdf_filerange;

#ifdef ENABLE_CLUSTER_LIST

typedef struct _vdf_filecluster {
	vdf_file		*file;						/* file this cluster applies to (or NULL) */
	cluster_t		fileclust;					/* cluster number within the file */
	cluster_t		nextclust;					/* next cluster number or (cluster_t)~0 */
} vdf_filecluster;

#define INVALID_CLUSTER		((cluster_t)~0)

#endif/*ENABLE_CLUSTER_LIST*/

struct _vdf_drive {
	int				lockcnt;					/* lock count */
	int				flags;						/* flags */
	int				filesys;					/* one of VDF_FAT(12|16|32) */
	size_t			bps;						/* bytes per sector */
	int				spc;						/* sectors per cluster */
	size_t			bpc;						/* bytes per cluster */
	sectcnt_t		sectors;					/* total number of sectors (not including any MBR sectors) */
	clustcnt_t		clusters;					/* total number of data clusters */
	drivesz_t		bytes;						/* total number of bytes (including MBR if applicable) */
	vdf_file		*root_dir;					/* root directory */
	int				root_dir_cnt;				/* count of root directory entries (ignored for FAT32) */
	list_head		all_files;					/* full file/dir linked list */
	list_head		transports;					/* transport linked list */
	int				file_cnt;					/* total file/dir count */
	int				nonempty_file_cnt;			/* total non-empty-file/dir count */
	vdf_filerange	*ranges;					/* file/dir range list */
#ifdef ENABLE_CLUSTER_LIST
	vdf_filecluster	*fileclusters;				/* cluster definition list (only used when writing is enabled) */
#endif
	char			*label;						/* label or NULL */
	uint32_t		serial;						/* drive serial */

	sectcnt_t		mbr_sectors;				/* number of sectors before partition start */
	int				dirent_per_sector;			/* number of directory entries per sector */
	sectcnt_t		fat_sectors;				/* number of sectors per FAT */
	sector_t		fat1_start;					/* first sector of FAT 1 */
	sector_t		fat2_start;					/* first sector of FAT 2 */
	sector_t		root_start;					/* first sector of root directory (ignored for FAT32) */
	sectcnt_t		root_sectors;				/* number of sectors for root directory (ignored for FAT32) */
	sector_t		data_start;					/* first sector of data */
	sectcnt_t		data_end;					/* end of actual data in sectors */
	cluster_t		data_cluster_end;			/* cluster number of end of actual data (will be >= 2) */
};

extern void set_drive_dirty(vdf_drive *drv);

static INLINE int drive_is_valid(vdf_drive *drv) {
	return (drv != NULL) && !(drv->flags & VDF_DELETED);
}

#endif /* __VDF_DRIVE_H */
