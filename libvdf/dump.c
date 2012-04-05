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
#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <vdf.h>
#include "vdf_drive.h"
#include "vdf_read.h"
#include "vdf_file.h"

#ifdef _MSC_VER
#define OPEN_FLAGS		_O_WRONLY | _O_CREAT | _O_BINARY | _O_TRUNC
#define OPEN_MODE		_S_IREAD | _S_IWRITE
#else
#define OPEN_FLAGS		O_WRONLY | O_CREAT | O_TRUNC
#define OPEN_MODE		0664
#endif

extern LIBFUNC int vdf_dump_drive_info(vdf_drive *drv, FILE *oup) {
	vdf_file *fil;
	int i;

	if(vdf_drive_lock(drv) == -1)
		return -1;
	fprintf(oup, "Drive details:\n");
	fprintf(oup, "  Label:                 %s\n", drv->label ? "<none>" : drv->label);
	fprintf(oup, "  Serial number:         0x%08x\n", drv->serial);
	/* TODO: dump flags */
	fprintf(oup, "  Bytes per sector:      %u\n", drv->bps);
	fprintf(oup, "  Sectors per cluster:   %u\n", drv->spc);
	fprintf(oup, "  Bytes per cluster:     %u\n", drv->bpc);
	if(drv->flags & VDF_MBR) {
		fprintf(oup, "  Sector count (part):   %u\n", drv->sectors);
		fprintf(oup, "  Sector count (full):   %u\n", drv->sectors + drv->mbr_sectors);
		fprintf(oup, "  Cluster count (data):  %u\n", drv->clusters);
		fprintf(oup, "  Drive size (part):     %llu\n", (drivesz_t)drv->sectors * drv->bps);
		fprintf(oup, "  Drive size (full):     %llu\n", (drivesz_t)(drv->sectors + drv->mbr_sectors) * drv->bps);
		fprintf(oup, "  MBR sectors:           %u\n", drv->mbr_sectors);
	} else {
		fprintf(oup, "  Sector count:          %u\n", drv->sectors);
		fprintf(oup, "  Cluster count (data):  %u\n", drv->clusters);
		fprintf(oup, "  Drive size:            %llu\n", (drivesz_t)drv->sectors * drv->bps);
		fprintf(oup, "  MBR sectors:           <no MBR>\n");
	}
	fprintf(oup, "  Filesystem:            %s\n", vdf_filesystem_name(drv->filesys));
	fprintf(oup, "  Root dir entries:      %u\n", drv->root_dir_cnt);
	fprintf(oup, "  File count:            %u\n", drv->file_cnt);
	fprintf(oup, "  Non-empty file count:  %u\n", drv->nonempty_file_cnt);
	fprintf(oup, "  Dir entries/sector:    %u\n", drv->dirent_per_sector);
	fprintf(oup, "  Sectors per FAT:       %u\n", drv->fat_sectors);
	fprintf(oup, "  FAT 1 start:           %u\n", drv->fat1_start);
	fprintf(oup, "  FAT 2 start:           %u\n", drv->fat2_start);
	fprintf(oup, "  Root dir start sect:   %u\n", drv->root_start);
	fprintf(oup, "  Root dir sectors:      %u\n", drv->root_sectors);
	fprintf(oup, "  Data sector start:     %u\n", drv->data_start);
	fprintf(oup, "  Data sector end:       %u\n", drv->data_end);
	fprintf(oup, "  Data cluster end:      %u\n", drv->data_cluster_end);
	fprintf(oup, "  Data clusters used:    %u\n", drv->data_cluster_end - 2);
	fprintf(oup, "\nFiles:\n");
	list_foreach_item(vdf_file, fil, &drv->all_files, alllist) {
		fprintf(oup, "  %s: '%s'\n", (fil->attr & VFA_DIR) ? "Dir " : "File", fil->name);
		fprintf(oup, "    Shortname:        '%s'\n", fil->shortname);
		fprintf(oup, "    Parent:           %s\n", fil->parent ? fil->parent->name : "<NONE>");
		fprintf(oup, "    Size:             %u\n", fil->size);
		fprintf(oup, "    Clusters:         %u\n", fil->clusters);
		fprintf(oup, "    Start cluster:    %u\n", fil->start);
		fprintf(oup, "    End cluster:      %u\n", fil->end);
		fprintf(oup, "    End data cluster: %u\n", fil->data_end);
		fprintf(oup, "    Start sector:     %u\n", fil->startsect);
		fprintf(oup, "    End sector:       %u\n", fil->endsect);
		fprintf(oup, "    File entry start: %u\n", fil->fent_start);
		fprintf(oup, "    File entry count: %u\n", fil->fent_cnt + 1);
	}
	fprintf(oup, "\nRanges:\n");
	for(i=0; i<drv->nonempty_file_cnt; i++) {
		fprintf(oup, "%3u: '%s'\n", i, drv->ranges[i].file->name);
		fprintf(oup, "     Sector start:  %u\n", drv->ranges[i].sectstart);
		fprintf(oup, "     Sector end:    %u\n", drv->ranges[i].sectend);
		fprintf(oup, "     FAT start:     %u\n", drv->ranges[i].fatstart);
		fprintf(oup, "     FAT end:       %u\n", drv->ranges[i].fatend);
	}
	vdf_drive_unlock(drv);
	return 0;
}

LIBFUNC int vdf_dump_drive(vdf_drive *drv, const char *path) {
	char *buff;
	sector_t sect;
	int fd, ret = -1;
	ssize_t r;
	size_t cnt;

	if(vdf_drive_lock(drv) == -1)
		return -1;

	buff = malloc(drv->bpc);
	if(buff == NULL) {
		errno = ENOMEM;
		return -1;
	}
	fd = open(path, OPEN_FLAGS, OPEN_MODE);
	if(fd == -1)
		goto exit_err_open;
	cnt = vdf_drive_sectors(drv);
	//cnt = vdf_drive_bytes(drv);
	for(sect=0; sect<cnt;) {
		r = vdf_read_sectors(drv, sect, drv->spc, buff);
//		r = vdf_read_sector(drv, sect, buff);
//		r = vdf_read_bytes(drv, sect, (drv->bps * 3) /2, buff);
		if(r == -1)
			goto exit_err;
		sect += r;
		r *= drv->bps;
		if(write(fd, buff, r) != r)
			goto exit_err;
//		printf("%lu : read %lu\n", sect, r);
	}
	ret = 0;
exit_err:
	close(fd);
exit_err_open:
	free(buff);
	vdf_drive_unlock(drv);
	return ret;
}

