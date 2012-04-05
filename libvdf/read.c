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
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vdf.h>
#include "vdf_drive.h"
#include "vdf_file.h"
#include "vdf_read.h"

LIBFUNC int vdf_read_sector(vdf_drive *drv, sector_t sector, void *buffer) {
	return vdf_read_sectors(drv, sector, 1, buffer);
}

LIBFUNC int vdf_read_sectors(vdf_drive *drv, sector_t sector, sectcnt_t cnt, void *buffer) {
	ssize_t want, rem;
	uint8_t *buff = buffer;

	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(cnt == 0)
		return 0;
	rem = cnt;
	cnt = 0;
	if(drv->flags & VDF_MBR) {
		if(sector == 0) {
			if(read_sector_mbr(drv, buff) == -1)
				return -1;
			if(--rem == 0)
				return 1;
			buff += drv->bps;
			cnt = 1;
			sector++;
		}
		if(sector < drv->mbr_sectors) {
			want = rem;
			if((sector + rem) >= drv->mbr_sectors)
				want = drv->mbr_sectors - sector;
			memset(buff, 0, drv->bps * want);
			rem -= want;
			cnt += want;
			if(rem == 0)
				return cnt;
			sector += want;
			buff += want * drv->bps;
		}
		sector -= drv->mbr_sectors;
	}
	if(sector < drv->fat1_start) {
		want = rem;
		if((sector + rem) >= drv->fat1_start)
			want = drv->fat1_start - sector;
		if(read_sector_boot(drv, sector, want, buff) == -1)
			return -1;
		rem -= want;
		cnt += want;
		if(rem == 0)
			return cnt;
		sector += want;
		buff += want * drv->bps;
	}
	if(sector < drv->fat2_start) {
		want = rem;
		if((sector + rem) >= drv->fat2_start)
			want = drv->fat2_start - sector;
#ifdef ENABLE_CLUSTER_LIST
		if(drv->flags & VDF_CLUSTERLIST) {
			if(read_sector_fat_clustlist(drv, sector - drv->fat1_start, want, buff) == -1)
				return -1;
		} else {
			if(read_sector_fat(drv, sector - drv->fat1_start, want, buff) == -1)
				return -1;
		}
#else
		if(read_sector_fat(drv, sector - drv->fat1_start, want, buff) == -1)
			return -1;
#endif
		rem -= want;
		cnt += want;
		if(rem == 0)
			return cnt;
		sector += want;
		buff += want * drv->bps;
	}
	if(sector < (drv->filesys == VDF_FAT32 ? drv->data_start : drv->root_start)) {
		want = rem;
		if(drv->filesys == VDF_FAT32) {
			if((sector + rem) >= drv->data_start)
				want = drv->data_start - sector;
		} else {
			if((sector + rem) >= drv->root_start)
				want = drv->root_start - sector;
		}
#ifdef ENABLE_CLUSTER_LIST
		if(drv->flags & VDF_CLUSTERLIST) {
			if(read_sector_fat_clustlist(drv, sector - drv->fat2_start, want, buff) == -1)
				return -1;
		} else {
			if(read_sector_fat(drv, sector - drv->fat2_start, want, buff) == -1)
				return -1;
		}
#else
		if(read_sector_fat(drv, sector - drv->fat2_start, want, buff) == -1)
			return -1;
#endif
		rem -= want;
		cnt += want;
		if(rem == 0)
			return cnt;
		sector += want;
		buff += want * drv->bps;
	}
	if((drv->filesys != VDF_FAT32) && (sector < drv->data_start)) {
		want = rem;
		if((sector + rem) >= drv->data_start)
			want = drv->data_start - sector;
		if(read_sector_dir(drv, sector - drv->root_start, want, buff, drv->root_dir) == -1)
			return -1;
		rem -= want;
		cnt += want;
		if(rem == 0)
			return cnt;
		sector += want;
		buff += want * drv->bps;
	}
	want = rem;
	if((sector + rem) >= drv->sectors)
		want = drv->sectors - sector;
#ifdef ENABLE_CLUSTER_LIST
	if(drv->flags & VDF_CLUSTERLIST) {
		if(read_sector_data_clustlist(drv, sector, want, buff) == -1)
			return -1;
	} else {
		if(read_sector_data(drv, sector, want, buff) == -1)
			return -1;
	}
#else
	if(read_sector_data(drv, sector, want, buff) == -1)
		return -1;
#endif
	cnt += want;
	return cnt;
}

LIBFUNC sdrivesz_t vdf_read_bytes(vdf_drive *drv, driveoff_t off, drivesz_t cnt, void *buffer) {
	sector_t soff, doff;
	size_t r, want, wbyte;
	drivesz_t rcnt;
	char *sbuff = NULL, *buff;

	if(!drive_is_valid(drv)) {
		errno = EINVAL;
		return -1;
	}
	if(cnt == 0)
		return 0;
	buff = buffer;
	soff = off / drv->bps;
	want = off & (drv->bps - 1);
	rcnt = 0;
	if(want != 0) {
		if((sbuff = alloca(drv->bps)) == NULL)
			return -1;
		doff = want;
		want = drv->bps - want;
		if(want > cnt)
			want = cnt;
		if((r = vdf_read_sectors(drv, soff, 1, sbuff)) != 1)
			return r;
		memcpy(buff, sbuff + doff, want);
		buff += want;
		soff++;
		rcnt += want;
		cnt -= want;
	}
	want = cnt / drv->bps;
	if(want != 0) {
		r = vdf_read_sectors(drv, soff, want, buff);
		if(r == -1)
			return -1;
		if(r < want)
			return rcnt + (r * drv->bps);
		wbyte = want * drv->bps;
		buff += wbyte;
		soff += want;
		rcnt += wbyte;
		cnt -= wbyte;
	}
	if(cnt != 0) {
		if(sbuff == NULL) {
			if((sbuff = alloca(drv->bps)) == NULL)
				return -1;
		}
		r = vdf_read_sectors(drv, soff, 1, sbuff);
		if(r == -1)
			return -1;
		if(r == 0)
			return rcnt;
		memcpy(buff, sbuff, cnt);
		rcnt += cnt;
	}
	return rcnt;
}

