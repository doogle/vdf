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
#include <time.h>
#include "vdf_drive.h"
#include "vdf_file.h"
#include "vdf_read.h"

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#endif

struct dir_ent {
	uint8_t		name[8];
	uint8_t		ext[3];
	uint8_t		attr;
	uint8_t		unused;
	uint8_t		create_time_10ms;
	uint16_t	create_time;
	uint16_t	create_date;
	uint16_t	access_date;
	uint16_t	high_clust;			/* FAT32 only */
	uint16_t	modified_time;
	uint16_t	modified_date;
	uint16_t	cluster;
	uint32_t	size;
} STRUCT_PACK;

struct dir_ent_vfat {
	uint8_t		seq;
	uint16_t	name1[5];
	uint8_t		attr;
	uint8_t		type;
	uint8_t		chk;
	uint16_t	name2[6];
	uint16_t	cluster;
	uint16_t	name3[2];
} STRUCT_PACK;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static int vfat_char_offsets[] = {
	1, 3, 5, 7, 9,				/* name1 */
	14, 16, 18, 20, 22, 24,		/* name2 */
	28, 30						/* name3 */
};

int fill_dirent(vdf_drive *drv, uint8_t *buffer, vdf_file *fil, int entnum, int maxcnt, const char *force_name) {
	int cnt = 0, i, e, seq;
	struct dir_ent *ent;
	struct dir_ent_vfat *vf;
	char *s, *d;
	struct tm *t;
	uint8_t chk;

	if((force_name == NULL) && (drv->flags & VDF_VFAT) && (fil->flags & VFF_LONGNAME)) {
		chk = 0;
		for(i=0, s=fil->shortname; (*s) && (*s != '.') && (i < 8); i++, s++)
			chk = (chk >> 1) + ((chk & 1) << 7) + *s;
		for(; i<8; i++)
			chk = (chk >> 1) + ((chk & 1) << 7) + ' ';
		if(*s++ == '.') {
			for(; (i<(8 + 3)) && (*s); i++, s++)
				chk = (chk >> 1) + ((chk & 1) << 7) + *s;
		}
		for(; i<(8 + 3); i++)
			chk = (chk >> 1) + ((chk & 1) << 7) + ' ';

		s = fil->name + (fil->fent_cnt * 13);
		entnum -= fil->fent_start;
		for(e=0, seq=fil->fent_cnt; seq > 0; seq--, e++) {
			s -= 13;
			if(e < entnum)
				continue;
			vf = (struct dir_ent_vfat*)buffer;
			vf->seq = seq;
			if(seq == fil->fent_cnt)
				vf->seq |= 1 << 6;
			vf->attr = 0xf;
			vf->type = 0;
			vf->chk = chk;
			vf->cluster = 0;

			for(i=0; (i<13) && (s[i]); i++) {
				*(uint16_t*)(buffer + vfat_char_offsets[i]) = le_16(s[i]);
			}
			for(; i<13; i++) {
				*(uint16_t*)(buffer + vfat_char_offsets[i]) = 0;	/* this should be 0xffff but Linux doesn't seem to like that */
			}
			if(++cnt == maxcnt)
				return cnt;
			buffer += sizeof(struct dir_ent_vfat);
		}
	}
	ent = (struct dir_ent*)buffer;
	if(force_name != NULL) {
		/* we only use 'force_name' for "." and ".." entries */
		i = strlen(force_name);
		memcpy(ent->name, force_name, i);
		memset(ent->name + i, ' ', (8 + 3) - i);
	} else {
		for(i=0, s=fil->shortname, d=ent->name; (*s) && (*s != '.') && (i < 8); i++, s++, d++)
			*d = *s;
		for(; i<8; i++, d++)
			*d = ' ';
		if(*s++ == '.') {
			for(; (i<(8 + 3)) && (*s); i++, s++, d++)
				*d = *s;
		}
		for(; i<(8 + 3); i++, d++)
			*d = ' ';
	}
	ent->attr = fil->attr;
	ent->unused = 0;
	t = gmtime(&fil->date);
	ent->modified_time = le_16(
		((t->tm_hour & 0x1f) << 11) |
		((t->tm_min  & 0x3f) << 5)  |
		((t->tm_sec / 2) & 0x1f)
	);
	i = t->tm_year - 80;
	if(i < 0)
		i = 0;
	else if(i > 127)
		i = 127;
	ent->modified_date = le_16(
		((i & 0x7f) << 9)                |
		(((t->tm_mon + 1) & 0x0f) << 5) |
		(t->tm_mday & 0x1f)
	);
	if(fil->drv->filesys == VDF_FAT32) {
		ent->create_time_10ms = (t->tm_sec & 1) ? 5 : 0;
		ent->create_time = ent->modified_time;
		ent->access_date = ent->create_date = ent->modified_date;
	} else {
		ent->create_time_10ms = 0;
		ent->create_time = 0;
		ent->access_date = 0;
	}
	if(fil->size == 0) {
		ent->high_clust = 0;
		ent->cluster = 0;
	} else {
		if((fil->parent == NULL) && (drv->filesys == VDF_FAT32)) {
			ent->high_clust = 0;
			ent->cluster = 0;
		} else {
			if(drv->filesys == VDF_FAT32)
				ent->high_clust = le_16(fil->start >> 16);
			else
				ent->high_clust = 0;
			ent->cluster = le_16(fil->start);
		}
	}
	if(fil->attr & VFA_DIR)
		ent->size = 0;
	else
		ent->size = le_32(fil->size);
	return cnt + 1;
}

#if 0
int fill_dirent_label(vdf_drive *drv, char *buffer) {
	struct dir_ent *ent;
	int i;
	char *s, *d;

	/* TODO: LFN for disk label? */
#if 0
	if((drv->flags & VDF_VFAT) && (drv->flags & VDF_LONGLABEL) {
		chk = 0;
		for(i=0, s=fil->shortname; (*s) && (*s != '.') && (i < 8); i++, s++)
			chk = (chk >> 1) + ((chk & 1) << 7) + *s;
		for(; i<8; i++)
			chk = (chk >> 1) + ((chk & 1) << 7) + ' ';
		if(*s++ == '.') {
			for(; (i<(8 + 3)) && (*s); i++, s++)
				chk = (chk >> 1) + ((chk & 1) << 7) + *s;
		}
		for(; i<(8 + 3); i++)
			chk = (chk >> 1) + ((chk & 1) << 7) + ' ';

		s = fil->name + (fil->fent_cnt * 13);
		entnum -= fil->fent_start;
		for(e=0, seq=fil->fent_cnt; seq > 0; seq--, e++) {
			s -= 13;
			if(e < entnum)
				continue;
			vf = (struct dir_ent_vfat*)buffer;
			vf->seq = seq;
			if(seq == fil->fent_cnt)
				vf->seq |= 1 << 6;
			vf->attr = 0xf;
			vf->type = 0;
			vf->chk = chk;
			vf->cluster = 0;

			for(i=0; (i<13) && (s[i]); i++) {
				*(uint16_t*)(buffer + vfat_char_offsets[i]) = le_16(s[i]);
			}
			for(; i<13; i++) {
				*(uint16_t*)(buffer + vfat_char_offsets[i]) = 0;	/* this should be 0xffff but Linux doesn't seem to like that */
			}
			if(++cnt == maxcnt)
				return cnt;
			buffer += sizeof(struct dir_ent_vfat);
		}
	}
#endif

	ent = (struct dir_ent*)buffer;
	memset(ent, 0, sizeof(struct dir_ent));
	ent->attr = VFA_VOLLABEL;
	s = drv->label;
	d = ent->name;
	for(i=0; (i<(8 + 3)) && (*s); i++, s++)
		stuff_dosname(&d, *s);
	for(; i<(8+3); i++, d++)
		*d = ' ';
	return 0;
}
#endif

int read_sector_dir(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer, vdf_file *dir) {
	int i, c, ent;
	int mx;
	vdf_file *fil;

	if(sector == 0) {
		if(dir->parent != NULL) {
			fill_dirent(drv, buffer, dir, 0, 1, ".");
			fill_dirent(drv, buffer + 32, dir->parent, 0, 1, "..");
			buffer += sizeof(struct dir_ent) * 2;
			i = 2;
		} else {
#if 0
			if(drv->label != NULL) {
				fill_dirent_label(drv, buffer);
				buffer += sizeof(struct dir_ent);
				i = 1;
			} else
#endif
				i = 0;
		}
		ent = i;
	} else {
		i = 0;
		ent = sector * (drv->bps / sizeof(struct dir_ent));
	}

	scnt *= drv->dirent_per_sector;
	mx = scnt - i;
	if(ent < dir->dir.fent_cnt) {
		if(ent < dir->dir.fent_cnt) {
			list_foreach_item(vdf_file, fil, &dir->dir.entries, dirlist) {
				if((fil->fent_start + fil->fent_cnt + 1) <= ent) {
					continue;
				} else {
					c = fill_dirent(drv, buffer, fil, ent, mx, NULL);
					if(c == -1)
						return -1;
					buffer += sizeof(struct dir_ent) * c;
					i += c;
					mx -= c;
					if(i >= scnt)
						break;
				}
			}
		}
	}
	if(mx != 0)
		memset(buffer, 0, mx * sizeof(struct dir_ent));
	return 0;
}

