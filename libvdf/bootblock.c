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
#include "vdf_read.h"

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#endif

struct boot16_32_common	{
	uint8_t			logical_num;
	uint8_t			reserved;
	uint8_t			ext_sig;
	uint32_t		serial;
	uint8_t			label[11];
	uint8_t			fstype[8];
} STRUCT_PACK;

struct fat_bootrec {
	uint8_t			jump[3];
	uint8_t			oem_name[8];
	uint16_t		bps;
	uint8_t			spc;
	uint16_t		reserved_sectors;
	uint8_t			fat_cnt;
	uint16_t		root_dir_cnt;
	uint16_t		sector_cnt;
	uint8_t			media_type;
	uint16_t		sectors_per_fat;
	uint16_t		sectors_per_track;
	uint16_t		head_cnt;
	union {
		struct {
			uint16_t		hidden_sectors;
			uint8_t			bootstrap[480];
		} STRUCT_PACK fat12;
		struct {
			uint32_t		hidden_sectors;
			uint32_t		sector_cnt;
			struct boot16_32_common	common;
			uint8_t			bootstrap[448];
		} STRUCT_PACK fat16;
		struct {
			uint32_t		hidden_sectors;
			uint32_t		sector_cnt;
			uint32_t		sectors_per_fat;
			uint16_t		mirror_flags;
			uint16_t		filesys_ver;
			uint32_t		root_start;
			uint16_t		fsinfo_sector;
			uint16_t		backupboot_sector;
			uint8_t			reserved[12];
			struct boot16_32_common	common;
			uint8_t			bootstrap[420];
		} STRUCT_PACK fat32;
	};
	uint8_t					signature[2];
} STRUCT_PACK;

struct fat32_fsinfo {
	uint32_t		signature1;
	uint8_t			reserved1[480];
	uint32_t		signature2;
	uint32_t		free_clusters;
	uint32_t		next_free_cluster;
	uint8_t			reserved2[12];
	uint32_t		sector_sig;
} STRUCT_PACK;

#define FSINFO_SIGNATURE1	0x41615252UL
#define FSINFO_SIGNATURE2	0x61417272UL
#define FSINFO_SECTORSIG	0xaa550000UL

struct part_rec {
	uint8_t			status;
	uint8_t			chs_start[3];
	uint8_t			type;
	uint8_t			chs_end[3];
	uint32_t		lba_start;
	uint32_t		sector_cnt;
} STRUCT_PACK;

struct disk_mbr {
	uint8_t			bootcode[440];
	uint32_t		disksig;
	uint16_t		nul;
	struct part_rec	part_recs[4];
	uint8_t			mbr_sig[2];
} STRUCT_PACK;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static int read_sector_fsinfo(vdf_drive *drv, uint8_t *buffer) {
	struct fat32_fsinfo *fsinfo = (struct fat32_fsinfo*)buffer;
	fsinfo->signature1 = le_32(FSINFO_SIGNATURE1);
	memset(fsinfo->reserved1, 0, sizeof(fsinfo->reserved1));
	fsinfo->signature2 = le_32(FSINFO_SIGNATURE2);
	fsinfo->free_clusters = le_32(drv->clusters - (drv->data_cluster_end - 2));
	fsinfo->next_free_cluster = le_32(drv->data_cluster_end);
	memset(fsinfo->reserved2, 0, sizeof(fsinfo->reserved2));
	fsinfo->sector_sig = le_32(FSINFO_SECTORSIG);
	return 0;
}

int read_sector_boot(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer) {
	struct fat_bootrec *boot;
	struct boot16_32_common	*common;
	char *s, *d;
	uint8_t *bootstrap;
	size_t bootstrap_len;
	int i;

	while(scnt--) {
		if((drv->filesys == VDF_FAT32) && (sector == 1)) {
			read_sector_fsinfo(drv, buffer);
			sector++;
			buffer += drv->bps;
			continue;
		}
		if(!((drv->filesys == VDF_FAT32) && (sector == 6)) && (sector != 0)) {
			memset(buffer, 0, drv->bps);
			sector++;
			buffer += drv->bps;
			continue;
		}
		boot = (struct fat_bootrec*)buffer;
		boot->jump[0] = boot->jump[1] = boot->jump[2] = 0;		/* should we have actual boot code? */
		memcpy(boot->oem_name, "MSWIN4.1", 8);
		boot->bps = le_16(drv->bps);
		boot->spc = drv->spc;
		boot->reserved_sectors = le_16(drv->fat1_start);
		if(drv->filesys == VDF_FAT32) {
			boot->root_dir_cnt = 0;
			boot->sector_cnt = 0;
			boot->sectors_per_fat = 0;
		} else {
			boot->root_dir_cnt = le_16(drv->root_dir_cnt);
//			if(vdf_drive_bytes(drv) < MiB(32))
			if(drv->sectors < 0x10000)
				boot->sector_cnt = le_16(drv->sectors);
			else
				boot->sector_cnt = 0;
			boot->sectors_per_fat = le_16(drv->fat_sectors);
		}
		boot->sectors_per_track = le_16(1);
		boot->fat_cnt = 2;
		boot->media_type = 0xf8;
		boot->sectors_per_track = le_16(1);
		boot->head_cnt = le_16(1);

		switch(drv->filesys) {
			case VDF_FAT12:
				boot->fat12.hidden_sectors = 0;
				common = NULL;
				bootstrap = boot->fat12.bootstrap;
				bootstrap_len = sizeof(boot->fat12.bootstrap);
				break;
			case VDF_FAT16:
				boot->fat16.hidden_sectors = 0;
#if 0
				if(drv->sectors >= 0x10000)
					boot->fat16.sector_cnt = le_32(drv->sectors);
				else
					boot->fat16.sector_cnt = 0;
#else
				if(boot->sector_cnt == 0)
					boot->fat16.sector_cnt = le_32(drv->sectors);
#endif
				common = &boot->fat16.common;
				memcpy(common->fstype, "FAT16   ", sizeof(common->fstype));
				bootstrap = boot->fat16.bootstrap;
				bootstrap_len = sizeof(boot->fat16.bootstrap);
				break;
			case VDF_FAT32:
				boot->fat16.hidden_sectors = 0;
				boot->fat32.sectors_per_fat = le_32(drv->fat_sectors);
				boot->fat32.sector_cnt = le_32(drv->sectors);
				boot->fat32.mirror_flags = 0;
				boot->fat32.filesys_ver = 0;
				boot->fat32.root_start = le_32(drv->root_dir->start);
				boot->fat32.fsinfo_sector = le_16(1);
				boot->fat32.backupboot_sector = le_16(6);
				memset(boot->fat32.reserved, 0, sizeof(boot->fat32.reserved));
				common = &boot->fat32.common;
				memcpy(common->fstype, "FAT32   ", sizeof(common->fstype));
				bootstrap = boot->fat32.bootstrap;
				bootstrap_len = sizeof(boot->fat32.bootstrap);
				break;
		}
		if(common != NULL) {
			common->logical_num = 0x80;
			common->reserved = 0;
			common->ext_sig = 0x29;
			common->serial = le_32(drv->serial);
			if(drv->label != NULL) {
				for(i=0,s=drv->label,d=common->label; *s && i<=sizeof(common->label); i++,s++)
					stuff_dosname(&d, *s);
				for(; i<=sizeof(common->label); i++,d++)
					*d = ' ';								/* space padded or nul terminated? */
			} else
				memset(common->label, ' ', sizeof(common->label));
		}
		memset(bootstrap, 0, bootstrap_len);
		boot->signature[0] = 0xaa;
		boot->signature[1] = 0x55;
		sector++;
		buffer += drv->bps;
	}
 	return 0;
}

int read_sector_mbr(vdf_drive *drv, uint8_t *buffer) {
	struct disk_mbr *mbr = (struct disk_mbr*)buffer;

	memset(mbr->bootcode, 0, sizeof(mbr->bootcode));
	mbr->disksig = le_32(drv->serial);					/* we use the serial as the signature. is this a bad idea? */
	mbr->nul = 0;
	mbr->part_recs[0].status = 0x80;
	switch(drv->filesys) {
		case VDF_FAT12:
			mbr->part_recs[0].type = 0x01;				/* FAT12 in as primary partition in first 32MB of disk */
			/* NOTE: this probably won't work, as this partition type is CHS addressed, which we don't support */
			break;
		case VDF_FAT16:
#if 0
			if(drv->sectors < 0x10000)
				mbr->part_recs[0].type = 0x04;			/* FAT16 with less than 65536 sectors */
			else
				mbr->part_recs[0].type = 0x06;			/* FAT16 with 65536 or more sectors */
#else
			mbr->part_recs[0].type = 0x0e;				/* FAT16X with LBA */
#endif
			break;
		case VDF_FAT32:
			mbr->part_recs[0].type = 0x0c;				/* FAT32X with LBA */
			break;
	}
	memset(mbr->part_recs[0].chs_start, 0, sizeof(mbr->part_recs[0].chs_start));
	memset(mbr->part_recs[0].chs_end, 0, sizeof(mbr->part_recs[0].chs_end));
	mbr->part_recs[0].lba_start = le_32(drv->mbr_sectors);
	mbr->part_recs[0].sector_cnt = le_32(drv->sectors);
	memset(mbr->part_recs + 1, 0, sizeof(struct part_rec) * 3);
	mbr->mbr_sig[0] = 0x55;
	mbr->mbr_sig[1] = 0xaa;
	return 0;
}
