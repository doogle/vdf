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

typedef struct _cluster_size_range {
	drivesz_t max_size;
	size_t cluster_size;
} cluster_size_range;

static cluster_size_range fat12_cluster_ranges[] = {
	{ MiB(15),		KiB(8) },
	{ 0,			KiB(4) }
};

static cluster_size_range fat16_cluster_ranges[] = {
	{ MiB(16),		KiB(4) },
	{ MiB(128),		KiB(2) },
	{ MiB(256),		KiB(4) },
	{ MiB(512),		KiB(8) },
	{ GiB(1),		KiB(16) },
	{ GiB(2),		KiB(32) },
	{ GiB(4),		KiB(64) },
	{ GiB(8),		KiB(128) },
	{ 0,			KiB(256) }
};

static cluster_size_range fat32_cluster_ranges[] = {
	{ MiB(260),		512 },
	{ GiB(8),		KiB(4) },
	{ GiB(16),		KiB(8) },
	{ GiB(32),		KiB(16) },
	{ 0,			KiB(32) }
};

static vcd_driveext driveext_auto = { VDE_AUTO_BPS | VDE_AUTO_SPC | VDE_AUTO_ROOTENT, -1, -1 };

static vdf_drive *createdrive_ext(vdf_drive *drv, drivesz_t size, int flags, vcd_driveext *ext);

LIBFUNC const char *vdf_filesystem_name(int filesys) {
	switch(filesys) {
		case VDF_FAT_AUTO:
			return "Auto";
		case VDF_FAT_AUTO_NO32:
			return "Auto (no FAT32)";
		case VDF_FAT12:
			return "FAT12";
		case VDF_FAT16:
			return "FAT16";
		case VDF_FAT32:
			return "FAT32";
		default:
			return "Invalid";
	}
}

static int find_spc(cluster_size_range *ranges, drivesz_t size, size_t bps) {
	int i;
	size_t cluster_size;
	for(i=0; ranges[i].max_size != 0; i++) {
		if(size < ranges[i].max_size)
			break;
	}
	cluster_size = ranges[i].cluster_size;
	if(cluster_size <= bps)
		return 1;
	return cluster_size / bps;
}

LIBFUNC vdf_drive *vdf_drive_create(uint64_t size, int flags) {
	return vdf_drive_create_ext(size, flags, &driveext_auto);
}

LIBFUNC vdf_drive *vdf_drive_create_ext(uint64_t size, int flags, vcd_driveext *ext) {
	return createdrive_ext(NULL, size, flags, ext);
}

LIBFUNC vdf_drive *vdf_drive_recreate(vdf_drive *drv, uint64_t size, int flags) {
	return vdf_drive_recreate_ext(drv, size, flags, &driveext_auto);
}

LIBFUNC vdf_drive *vdf_drive_recreate_ext(vdf_drive *drv, uint64_t size, int flags, vcd_driveext *ext) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return NULL;
	}
	if(vdf_drive_is_locked(drv)) {
		errno = EPERM;
		return NULL;
	}
	if((flags & VDF_FS_MASK) == VDF_FAT_SAME) {
		flags &= ~VDF_FS_MASK;
		flags |= drv->filesys;
	}
	return createdrive_ext(drv, size, flags, ext);
}

LIBFUNC vdf_drive *vdf_drive_recreate_extsame(vdf_drive *drv, uint64_t size, int flags, int extflags) {
	vcd_driveext ext = {extflags, drv->bps, drv->spc, drv->root_dir_cnt};
	return vdf_drive_recreate_ext(drv, size, flags, &ext);
}

static vdf_drive *createdrive_ext(vdf_drive *drv, uint64_t size, int flags, vcd_driveext *ext) {
	size_t bps, bpc;
	sectcnt_t sectors;
	clustcnt_t clusters;
	int isnew = 0, spc;
	int fsys_flag, fsys = -1, orig_fsys, root_dir_cnt;
	sectcnt_t fat_sectors, root_sectors;
	sector_t data_start, fat1_start;
	int dirent_per_sector;

	fsys_flag = flags & VDF_FS_MASK;
#ifdef ENABLE_CLUSTER_LIST
	if(flags & VDF_ENABLE_WRITE)
		flags |= VDF_CLUSTERLIST;
#endif

	if(ext->flags & VDE_AUTO_ROOTENT)
		root_dir_cnt = 512;
	else
		root_dir_cnt = ext->root_dir_cnt;

	if(ext->flags & VDE_AUTO_BPS) {
		bps = 512;
	} else {
		switch(ext->bytes_per_sector) {
			case 512: case 1024: case 2048: case 4096:
				break;
			default:
				errno = EINVAL;
				return NULL;
		}
		bps = ext->bytes_per_sector;
	}
	size += bps - 1;
	size &= ~(uint64_t)(bps - 1);
	sectors = (size_t)(size / bps);
	if(ext->flags & VDE_AUTO_SPC) {
		if((fsys_flag == VDF_FAT_AUTO) || (fsys_flag == VDF_FAT_AUTO_NO32)) {
			if(size < MiB(16)) {
				fsys = VDF_FAT12;
			} else if((size >= MiB(260)) && (size < TiB(2)) && (fsys_flag != VDF_FAT_AUTO_NO32)) {
				fsys = VDF_FAT32;
			} else if(size < GiB(16)) {
				fsys = VDF_FAT16;
			} else {
				errno = EINVAL;
				return NULL;
			}
		} else {
			switch(fsys_flag) {
				case VDF_FAT12: case VDF_FAT16: case VDF_FAT32:
					break;
				default:
					errno = EINVAL;
					return NULL;
			}
			fsys = fsys_flag;
		}
		switch(fsys) {
			case VDF_FAT12:
				spc = find_spc(fat12_cluster_ranges, size, bps);
				break;
			case VDF_FAT16:
				spc = find_spc(fat16_cluster_ranges, size, bps);
				break;
			case VDF_FAT32:
				spc = find_spc(fat32_cluster_ranges, size, bps);
				break;
		}
		bpc = bps * spc;
	} else {
		spc = ext->sectors_per_cluster;
		switch(spc) {
			case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128:
				break;
			default:
				errno = EINVAL;
				return NULL;
		}
		bpc = bps * spc;
		clusters = (size_t)(size / bpc);
		switch(fsys_flag) {
			case VDF_FAT_AUTO:
			case VDF_FAT_AUTO_NO32:
				/* This calculation should use a cluster count that does not include anything before the first data
				   cluster. However we have a chicken-and-egg situation, as we don't know how big the FATs will be
				   until we know how many clusters we have. It shouldn't be dangerous if these calculations aren't
				   correct according to how Microsoft do things */
				if(clusters < 4085) {
					fsys = VDF_FAT12;
				} else if((clusters < 65525) || (fsys_flag == VDF_FAT_AUTO_NO32)) {
					fsys = VDF_FAT16;
				} else {
					fsys = VDF_FAT32;
				}
				break;
			case VDF_FAT12:
			case VDF_FAT16:
			case VDF_FAT32:
				fsys = fsys_flag;
				break;
		}
	}
	dirent_per_sector = bps / 32;
	switch(fsys) {
		case VDF_FAT12:
			if(flags & VDF_ALIGN_CLUSTER)
				fat1_start = spc;
			else
				fat1_start = 1;
			root_sectors = root_dir_cnt * 32;
			root_sectors += bps - 1;
			root_sectors /= bps;
			if(flags & VDF_ALIGN_CLUSTER) {
				root_sectors += spc - 1;
				root_sectors &= ~(spc - 1);
			}
			data_start = fat1_start + root_sectors;
			clusters = (sectors - data_start) / spc;
			fat_sectors = ((clusters * 3) / 2) + 1;
			fat_sectors += bps - 1;
			fat_sectors /= bps;
			if(flags & VDF_ALIGN_CLUSTER) {
				fat_sectors += spc - 1;
				fat_sectors &= ~(spc - 1);
			}
			data_start += fat_sectors * 2;
			clusters = (sectors - data_start) / spc;
			if(clusters > 4084) {
				errno = EINVAL;
				return NULL;
			}
#if 0
			if(clusters > (0xfef - 2)) {
				errno = EINVAL;
				return NULL;
			}
#endif
			break;
		case VDF_FAT16:
			if(flags & VDF_ALIGN_CLUSTER)
				fat1_start = spc;
			else
				fat1_start = 1;
			root_sectors = root_dir_cnt * 32;
			root_sectors += bps - 1;
			root_sectors /= bps;
			if(flags & VDF_ALIGN_CLUSTER) {
				root_sectors += spc - 1;
				root_sectors &= ~(spc - 1);
			}
			data_start = fat1_start + root_sectors;
			clusters = (sectors - data_start) / spc;
			fat_sectors = clusters * 2;
			fat_sectors += bps - 1;
			fat_sectors /= bps;
			if(flags & VDF_ALIGN_CLUSTER) {
				fat_sectors += spc - 1;
				fat_sectors &= ~(spc - 1);
			}
			data_start += fat_sectors * 2;
			clusters = (sectors - data_start) / spc;
			if((flags & VDF_FAIL_WARN) && (clusters < 4085)) {
				errno = EINVAL;
				return NULL;
			}
			if(clusters < 4085) {
				errno = EINVAL;
				return NULL;
			}
			if(clusters > 65524) {
				errno = EINVAL;
				return NULL;
			}
#if 0
			if(clusters > (0xffef - 2)) {
				errno = EINVAL;
				return NULL;
			}
#endif
			break;
		case VDF_FAT32:
			fat1_start = 32;
			if(flags & VDF_ALIGN_CLUSTER) {
				fat1_start += spc;
				fat1_start &= ~(spc - 1);
			}
			data_start = fat1_start;
			root_sectors = 0;
			clusters = (sectors - data_start) / spc;
			fat_sectors = clusters * 4;
			fat_sectors += bps - 1;
			fat_sectors /= bps;
			if(flags & VDF_ALIGN_CLUSTER) {
				fat_sectors += spc - 1;
				fat_sectors &= ~(spc - 1);
			}
			data_start += fat_sectors * 2;
			clusters = (sectors - data_start) / spc;
			if((flags & VDF_FAIL_WARN) && (clusters <= 65525)) {
				errno = EINVAL;
				return NULL;
			}
			if(clusters < 65525) {
				errno = EINVAL;
				return NULL;
			}
			if(clusters > (0xfffffef - 2)) {
				errno = EINVAL;
				return NULL;
			}
			break;
	}

	if(drv == NULL) {
		isnew = 1;
		drv = malloc(sizeof(vdf_drive));
		if(drv == NULL) {
			errno = ENOMEM;
			return NULL;
		}
		memset(drv, 0, sizeof(vdf_drive));
	} else
		orig_fsys = drv->filesys;
	drv->flags = flags | VDF_DIRTY;
	drv->filesys = fsys;
	drv->bps = bps;
	drv->spc = spc;
	drv->bpc = bpc;
	drv->sectors = sectors;
	drv->clusters = clusters;
	drv->root_dir_cnt = root_dir_cnt;
	drv->dirent_per_sector = dirent_per_sector;
	drv->fat_sectors = fat_sectors;
	drv->fat1_start = fat1_start;
	drv->fat2_start = fat1_start + fat_sectors;
	drv->root_start = drv->fat2_start + fat_sectors;
	drv->root_sectors = root_sectors;
	drv->data_start = drv->data_end = data_start;

	if(flags & VDF_MBR) {
		/* Note: this means that the size passed in by the user is the 'partition size' and the total device will
		   have a different, and possibly slightly strange size. */
		if(flags & VDF_MBR_PAD) {
			drv->mbr_sectors = spc;		/* TODO: DOS pads out the MBR before the start of a partition
										   However, DOS pads to a multiple of sectors-per-track, which we don't know.
										   So we'll use sectors-per-cluster for want of a more correct value */
		} else {
			drv->mbr_sectors = 1;
		}
	} else {
		drv->mbr_sectors = 0;
	}
	drv->bytes = (uint64_t)(drv->sectors + drv->mbr_sectors) * drv->bps;

	if(isnew) {
		drv->serial = DEFAULT_DRIVE_SERIAL;
		INIT_LIST_HEAD(&drv->all_files);
		INIT_LIST_HEAD(&drv->transports);

		drv->root_dir = create_root_dir(drv);
		if(drv->root_dir == NULL) {
			free(drv);
			return NULL;
		}
	} else {
		if(orig_fsys == VDF_FAT32) {
			if(fsys != VDF_FAT32) {
				list_del(&drv->root_dir->alllist);
				drv->file_cnt--;
			}
		} else {
			if(fsys == VDF_FAT32) {
				list_add_tail(&drv->root_dir->alllist, &drv->all_files);
				drv->file_cnt++;
			}
		}
	}

	return drv;
}

LIBFUNC int vdf_drive_free(vdf_drive *drv) {
	vdf_transport *trans, *trans_n;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	list_foreach_item_safe(vdf_transport, trans, trans_n, &drv->transports, drivelist) {
		vdf_transport_close(trans);
	}
	delete_file(drv->root_dir);
	if(drv->ranges != NULL)
		free(drv->ranges);
	if(drv->label != NULL)
		free(drv->label);
	drv->flags |= VDF_DELETED;
	free(drv);
	return 0;
}

LIBFUNC ssize_t vdf_drive_sectorsize(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->bps;
}

LIBFUNC int vdf_drive_clustersectors(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->spc;
}

LIBFUNC size_t vdf_drive_clustersize(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->bpc;
}

LIBFUNC sdrivesz_t vdf_drive_bytes(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->bytes;
}

LIBFUNC ssectcnt_t vdf_drive_sectors(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->sectors + drv->mbr_sectors;
}

LIBFUNC sclustcnt_t vdf_drive_dataclusters(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->clusters;
}

LIBFUNC sclustcnt_t vdf_drive_usedclusters(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(drv->flags & VDF_DIRTY)
		return -1;
	return drv->data_cluster_end - 2;
}

LIBFUNC int vdf_drive_flags(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->flags;
}

LIBFUNC int vdf_drive_filesystem(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->filesys;
}

LIBFUNC int vdf_set_drive_label(vdf_drive *drv, const char *label) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(drv)) {
		errno = EPERM;
		return -1;
	}
	if(drv->label != NULL)
		free(drv->label);
	if(label != NULL) {
		drv->label = strdup(label);
		if(drv->label == NULL) {
			errno = ENOMEM;
			return -1;
		}
	} else
		drv->label = NULL;
	set_drive_dirty(drv);
	return 0;
}

LIBFUNC ssize_t vdf_get_drive_label(vdf_drive *drv, char *label, size_t len) {
	size_t l;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(label == NULL) {
		if(drv->label == NULL)
			return 0;
		else
			return strlen(drv->label);
	}
	if(len == 0) {
		errno = EINVAL;
		return -1;
	}
	if(drv->label == NULL) {
		*label = 0;
		return 0;
	}
	l = strlen(drv->label);
	if(l >= len) {
		errno = ENOMEM;		/* TODO: this should be something else */
		return -1;
	}
	strcpy(label, drv->label);
	return l;
}

LIBFUNC int vdf_set_drive_serial(vdf_drive *drv, uint32_t serial) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	drv->serial = serial;
	return 0;
}

LIBFUNC int32_t vdf_get_drive_serial(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->serial;
}

LIBFUNC vdf_file *vdf_drive_root(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return NULL;
	}
	return drv->root_dir;
}

LIBFUNC int vdf_drive_lock(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(drv->lockcnt == 0) {
		if(vdf_drive_recalc(drv) == -1)
			return -1;
	}
	drv->lockcnt++;
	return 0;
}

LIBFUNC int vdf_drive_unlock(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(drv->lockcnt == 0) {
		errno = EINVAL;
		return -1;
	}
	drv->lockcnt--;
	return 0;
}

LIBFUNC int vdf_drive_is_locked(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return drv->lockcnt != 0;
}

void set_drive_dirty(vdf_drive *drv) {
	if(drv->flags & VDF_DIRTY)
		return;
	if(drv->ranges != NULL) {
		free(drv->ranges);
		drv->ranges = NULL;
	}
	drv->flags |= VDF_DIRTY;
}

LIBFUNC int vdf_drive_is_dirty(vdf_drive *drv) {
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	return !!(drv->flags & VDF_DIRTY);
}

static int cmp_filerange(const void *r1, const void *r2) {
	return ((const vdf_filerange*)r1)->sectstart - ((const vdf_filerange*)r2)->sectstart;
}

LIBFUNC int vdf_drive_recalc(vdf_drive *drv) {
	vdf_file *fil;
	int i;
	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(drv)) {
		errno = EPERM;
		return -1;
	}
	if(!(drv->flags & VDF_DIRTY))
		return 0;
	drv->data_end = drv->data_start;
	drv->nonempty_file_cnt = 0;
	drv->data_cluster_end = 2;
	if(dir_recalc(drv->root_dir) == -1)
		return -1;
	list_foreach_item(vdf_file, fil, &drv->all_files, alllist) {
		if(file_recalc_offsize(fil) == -1)
			return -1;
	}
	if(drv->ranges != NULL)
		free(drv->ranges);
	drv->ranges = malloc(sizeof(vdf_filerange) * drv->nonempty_file_cnt);
	i = 0;
	list_foreach_item(vdf_file, fil, &drv->all_files, alllist) {
		if(vdf_file_is_dir(fil) || (fil->size != 0)) {
			drv->ranges[i].file = fil;
			drv->ranges[i].sectstart = fil->startsect;
			drv->ranges[i].sectend = fil->startsect + (fil->clusters * drv->spc);
			drv->ranges[i].fatstart = fil->start;
			drv->ranges[i].fatend = fil->end;
			i++;
		}
	}
	qsort(drv->ranges, drv->nonempty_file_cnt, sizeof(vdf_filerange), cmp_filerange);
	drv->flags &= ~VDF_DIRTY;
	return 0;
}

