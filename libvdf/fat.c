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

static INLINE vdf_file *find_file_fat_entry(vdf_drive *drv, cluster_t ent, int *range_index) {
#if 1
	int i;
	vdf_filerange *range;
	if(ent >= drv->data_cluster_end)
		return NULL;
	range = drv->ranges;
	for(i=0; i<drv->file_cnt; i++, range++) {
		if(ent < range->fatend) {
			*range_index = i;
			return range->file;
		}
	}
	*range_index = -1;
	return NULL;
#else
	/* TODO: get this working */
	int low, high, mid;
	if(ent >= drv->fatent_cnt)
		return NULL;
	low = 0;
	high = drv->file_cnt;
	while(low < high) {
		mid = low + ((high - low) / 2);
		if(drv->ranges[mid].fatend < ent)
			low = mid + 1;
		else
			high = mid;
	}
	if((low < drv->file_cnt) && (drv->ranges[low].fatend < ent)) {
		*range_index = low;
		return drv->ranges[low].file;
	}
	*range_index = -1;
	return NULL;
#endif
}

static INLINE vdf_file *fat_next_file(vdf_drive *drv, cluster_t ent, vdf_file *file, int *range_ind) {
#if 0
	file = find_file_fat_entry(drv, ent, range_ind);
#elif 0
	if(file->alllist.next == &drv->all_files)
		file = NULL;
	else
		file = list_item(file->alllist.next, vdf_file, alllist);
#else
	(*range_ind)++;
	if(*range_ind == drv->file_cnt)
		file = NULL;
	else
		file = drv->ranges[*range_ind].file;
#endif
	return file;
}


int read_sector_fat(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer) {
	cluster_t ent, clust;
	int halfent;
	int i, range_ind;
	vdf_file *file;

	scnt *= drv->bps;
	if(sector == 0) {
		ent = 2;
		switch(drv->filesys) {
			case VDF_FAT12:
				halfent = 0;
				i = 3;
				buffer[0] = 0xf8;
				buffer[1] = 0xff;
				buffer[2] = 0xff;
				break;
			case VDF_FAT16:
				i = 4;
				*(uint16_t*)buffer = le_16(0xfff8);
				*(uint16_t*)(buffer + 2) = le_16(0xffff);
				break;
			case VDF_FAT32:
				i = 8;
				*(uint32_t*)buffer = le_32(0x0ffffff8);
				*(uint32_t*)(buffer + 4) = le_32(0x0fffffff);
				break;
		}
	} else {
		i = 0;
		switch(drv->filesys) {
			case VDF_FAT12:
				ent = ((sector * 2) * drv->bps) / 3;
				halfent = ent & 1;
				ent >>= 1;
				break;
			case VDF_FAT16:
				ent = sector * (drv->bps / 2);
				break;
			case VDF_FAT32:
				ent = sector * (drv->bps / 4);
				break;
		}
	}
	buffer += i;

	file = find_file_fat_entry(drv, ent, &range_ind);
	if(file != NULL)
		//clust = file->start + 1;
		clust = ent + 1;
	else
		goto finish;
#ifdef WRITE_DEBUG
	printf("FAT: Start ent: %u\n", ent);
#endif
	switch(drv->filesys) {
		case VDF_FAT12:
			for(; i<scnt;) {
				if(ent < (file->end - 1)) {
#ifdef WRITE_DEBUG
					printf("%u: %u\n", ent, clust);
#endif
					if(halfent) {
						*buffer &= 0xf0;
						*buffer++ |= (clust << 4) & 0xf0;
						i++;
						if(i < (scnt - 1)) {
							*buffer++ = (clust >> 4) & 0xff;
							i++;
						}
					} else {
						*buffer++ = clust & 0xff;
						i++;
						if(i < (scnt - 1))
							*buffer = (clust >> 8) & 0xf;
					}
					clust++;
					ent++;
				} else {
#ifdef WRITE_DEBUG
					printf("%u: <end>\n", ent);
#endif
					if(halfent) {
						*buffer++ |= 0xf0;
						if(i < (scnt - 1))
							*buffer++ = 0xff;
						i += 2;
					} else {
						*buffer++ = 0xff;
						if(i < (scnt - 1))
							*buffer = 0xf;
						i++;
					}
					file = fat_next_file(drv, ++ent, file, &range_ind);
					if(file == NULL) {
						if(!halfent) {
							i++;
							buffer++;
						}
						goto finish;
					}
//					clust = file->start + 1;
					clust = ent + 1;
				}
				halfent = !halfent;
			}
			break;
		case VDF_FAT16:
			for(; i<scnt; i+=2, buffer+=2) {
				if(ent < (file->end - 1)) {
#ifdef WRITE_DEBUG
					printf("%u: %u\n", ent, clust);
#endif
					*(uint16_t*)buffer = le_16(clust++);
					ent++;
				} else {
#ifdef WRITE_DEBUG
					printf("%u: <end>\n", ent);
#endif
					*(uint16_t*)buffer = le_16(0xffff);
					file = fat_next_file(drv, ++ent, file, &range_ind);
					if(file == NULL) {
						buffer += 2;
						i += 2;
						goto finish;
					}
//					clust = file->start + 1;
					clust = ent + 1;
				}
			}
			break;
		case VDF_FAT32:
			for(; i<scnt; i+=4, buffer+=4) {
				if(ent < (file->end - 1)) {
#ifdef WRITE_DEBUG
					printf("%u: %u\n", ent, clust);
#endif
					*(uint32_t*)buffer = le_32(clust++);
					ent++;
				} else {
#ifdef WRITE_DEBUG
					printf("%u: <end>\n", ent);
#endif
					*(uint32_t*)buffer = le_32(0x0fffffff);
					file = fat_next_file(drv, ++ent, file, &range_ind);
					if(file == NULL) {
						buffer += 4;
						i += 4;
						goto finish;
					}
//					clust = file->start + 1;
					clust = ent + 1;
				}
			}
			break;
	}
finish:
	i = scnt - i;
	if(i != 0)
		memset(buffer, 0, i);

	return 0;
}

#ifdef ENABLE_CLUSTER_LIST
int read_sector_fat_clustlist(vdf_drive *drv, off_t sector, size_t scnt, char *buffer) {
	off_t ent, clust;
	int halfent;
	uint8_t *p;
	int i;
	vdf_filecluster *fc;

	scnt *= drv->bps;
	if(sector == 0) {
		ent = 2;
		switch(drv->filesys) {
			case VDF_FAT12:
				halfent = 0;
				i = 3;
				buffer[0] = 0xf8;
				buffer[1] = 0xff;
				buffer[2] = 0xff;
				break;
			case VDF_FAT16:
				i = 4;
				*(uint16_t*)buffer = le_16(0xfff8);
				*(uint16_t*)(buffer + 2) = le_16(0xffff);
				break;
			case VDF_FAT32:
				i = 8;
				*(uint32_t*)buffer = le_16(0xfffffff8);
				*(uint32_t*)(buffer + 4) = le_16(0x0fffffff);
				break;
		}
	} else {
		i = 0;
		switch(drv->filesys) {
			case VDF_FAT12:
				ent = ((sector * 2) * drv->bps) / 3;
				halfent = ent & 1;
				ent >>= 1;
				break;
			case VDF_FAT16:
				ent = sector * (drv->bps / 2);
				break;
			case VDF_FAT32:
				ent = sector * (drv->bps / 4);
				break;
		}
	}
	p = buffer + i;

	fc = drv->fileclusters + ent;
	//printf("FAT: Start ent: %lu\n", ent);
	switch(drv->filesys) {
		case VDF_FAT12:
			for(; i<scnt;) {
				if((fc->file != NULL) && (fc->nextclust != INVALID_CLUSTER)) {
					clust = fc->nextclust;
					//printf("%lu: %lu\n", clust);
					if(halfent) {
						*p &= 0xf0;
						*p++ |= (clust << 4) & 0xf0;
						if(i < (scnt - 1))
							*p++ = (clust >> 8) & 0xff;
						i += 2;
					} else {
						*p++ = clust & 0xff;
						if(i < (scnt - 1))
							*p = (clust >> 12) & 0xf;
						i++;
					}
					ent++;
				} else {
					//printf("%lu: <end>\n", ent);
					if(halfent) {
						*p++ |= 0xf0;
						if(i < (scnt - 1))
							*p++ = 0xff;
						i += 2;
					} else {
						*p++ = 0xff;
						if(i < (scnt - 1))
							*p = 0xf;
						i++;
					}
				}
				halfent = !halfent;
				fc++;
			}
			break;
		case VDF_FAT16:
			for(; i<scnt; i+=2, p+=2) {
				if((fc->file != NULL) && (fc->nextclust != INVALID_CLUSTER)) {
					clust = fc->nextclust;
					//printf("%lu: %lu\n", ent, clust);
					*(uint16_t*)p = le_16(clust);
					ent++;
				} else {
					//printf("%lu: <end>\n", ent);
					*(uint16_t*)p = le_16(0xffff);
				}
				fc++;
			}
			break;
		case VDF_FAT32:
			for(; i<scnt; i+=4, p+=4) {
				if((fc->file != NULL) && (fc->nextclust != INVALID_CLUSTER)) {
					clust = fc->nextclust;
					//printf("%lu: %lu\n", ent, clust);
					*(uint16_t*)p = le_32(clust);
					ent++;
				} else {
					//printf("%lu: <end>\n", ent);
					*(uint16_t*)p = le_32(0x0fffffff);
				}
				fc++;
			}
			break;
	}
finish:
	i = scnt - i;
	if(i != 0)
		memset(p, 0, i);

	return 0;
}
#endif/*ENABLE_CLUSTER_LIST*/

