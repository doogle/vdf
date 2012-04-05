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
#include <stdio.h>
#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <vdf.h>
#include "vdf_drive.h"
#include "vdf_file.h"
#include "vdf_read.h"

static INLINE vdf_file *find_file_sector(vdf_drive *drv, sector_t sector) {
#if 1
	int i;
	vdf_filerange *range;
	if(sector >= drv->data_end)
		return NULL;
	range = drv->ranges;
	for(i=0; i<drv->file_cnt; i++, range++) {
		if(sector < range->sectend) {
			return range->file;
		}
	}
	return NULL;
#else
	/* TODO: test this and use it */
	int low, high, mid;
	if(sector >= drv->data_end)
		return NULL;
	low = 0;
	high = drv->file_cnt;
	while(low < high) {
		mid = low + ((high - low) / 2);
		if(drv->ranges[mid].sectend < sect)
			low = mid + 1;
		else
			high = mid;
	}
	if((low < drv->file_cnt) && (drv->ranges[low].sectend < sector)) {
		return drv->ranges[low].file;
	}
	return NULL;
#endif
}

#ifdef _MSC_VER
#define OPEN_FLAGS		_O_RDONLY | _O_BINARY
#else
#define OPEN_FLAGS		O_RDONLY
#endif

int read_sector_data(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer) {
	vdf_file *fil;
	int fd;
	ssize_t cnt;
	sectoff_t off;
	size_t want, swant;

	while(scnt) {
		fil = find_file_sector(drv, sector);
		if(fil == NULL) {
			memset(buffer, 0, scnt * drv->bps);
			return 0;
		}
		off = sector - fil->startsect;
		swant = fil->endsect - sector;
		if(swant > scnt)
			swant = scnt;
		if(vdf_file_is_dir(fil)) {
#ifdef WRITE_DEBUG
			printf("%s dir @ sector %u (off=%u , swant=%u , scnt=%u\n", fil->shortname, sector, off, swant, scnt);
#endif
			if(read_sector_dir(drv, off, swant, buffer, fil) == -1)
				return -1;
			sector += swant;
			scnt -= swant;
			buffer += swant * drv->bps;
			continue;
		}
		off = off * drv->bps;
		if(off >= fil->size) {
			want = swant * drv->bps;
			memset(buffer, 0, want);
			sector += swant;
			scnt -= swant;
			buffer += want;
			continue;
		}
		want = fil->size - off;
		if(want > (scnt * drv->bps))
			want = scnt * drv->bps;
		if(vdf_file_is_virt(fil)) {
			cnt = fil->virt.cback(vfc_write, fil, off, want, buffer, fil->virt.param);
		} else {
			fd = open(fil->real.path, OPEN_FLAGS);
			if(fd == -1)
				return -1;
			lseek(fd, off, SEEK_SET);
			cnt = read(fd, buffer, want);
			close(fd);
		}
#ifdef WRITE_DEBUG
		printf("%s file @ sector %u (off=%u , want=%u, swant=%u , scnt=%u\n", fil->shortname, sector, off, want, swant, scnt);
#endif
		if(cnt == -1)
			return -1;
		if(cnt > want)
			cnt = want;
		buffer += cnt;
		cnt = (swant * drv->bps) - cnt;
		if(cnt > 0) {
			memset(buffer, 0, cnt);
			buffer += cnt;
		}
		sector += swant;
		scnt -= swant;
	}
	return 0;
}

#ifdef ENABLE_CLUSTER_LIST
int _sector_data_clustlist(vdf_drive *drv, off_t clust, off_t

int read_sector_data_clustlist(vdf_drive *drv, off_t sector, size_t scnt, char *buffer) {
	vdf_file *fil;
	int fd;
	ssize_t cnt;
	off_t off, clust;
	size_t want, swant;

	if(scnt == 0)
		return 0;

	sector -= drv->data_start;
	clust = sector / drv->spc;

	swant = sector & (drv->spc - 1);
	if(swant != 0) {
		swant f
	}

	while(scnt) {
		swant = sector & (drv->spc - 1);
		swant = drv->spc - swant;
		want = drv->bps * swant;
		fil = drv->fileclusters[clust].file;
		if(fil == NULL) {
			memset(buffer, 0, want);
			scnt -= swant;
			buffer += want;
			sector += swant;
			continue;
		}
		off = drv->fileclusters[clust].fileclust;

		off = sector - fil->startsect;
		swant = fil->endsect - sector;
		if(swant > scnt)
			swant = scnt;
		if(vdf_file_is_dir(fil)) {
			//printf("%s dir @ sector %lu (off=%lu , swant=%lu , scnt=%lu\n", fil->shortname, sector, off, swant, scnt);
			if(read_sector_dir(drv, off, swant, buffer, fil) == -1)
				return -1;
			sector += swant;
			scnt -= swant;
			buffer += swant * drv->bps;
			continue;
		}
		off = off * drv->bps;
		if(off >= fil->size) {
			want = swant * drv->bps;
			memset(buffer, 0, want);
			sector += swant;
			scnt -= swant;
			buffer += want;
			continue;
		}
		want = fil->size - off;
		if(want > (scnt * drv->bps))
			want = scnt * drv->bps;
		if(vdf_file_is_virt(fil)) {
			cnt = fil->virt.cback(vfc_write, fil, off, want, buffer, fil->virt.param);
		} else {
			fd = open(fil->real.path, OPEN_FLAGS);
			if(fd == -1)
				return -1;
			lseek(fd, off, SEEK_SET);
			cnt = read(fd, buffer, want);
			close(fd);
		}
		//printf("%s file @ sector %lu (off=%lu , want=%lu, swant=%lu , scnt=%lu\n", fil->shortname, sector, off, want, swant, scnt);
		if(cnt == -1)
			return -1;
		if(cnt > want)
			cnt = want;
		buffer += cnt;
		cnt = (swant * drv->bps) - cnt;
		if(cnt > 0) {
			memset(buffer, 0, cnt);
			buffer += cnt;
		}
		sector += swant;
		scnt -= swant;
	}
	return 0;
}
#endif/*ENABLE_CLUSTER_LIST*/
