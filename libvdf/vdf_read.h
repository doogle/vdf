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

#ifndef __VDF_READ_H
#define __VDF_READ_H

#include <vdf.h>
#include "vdf_private.h"

extern int read_sector_mbr(vdf_drive *drv, uint8_t *buffer);
extern int read_sector_boot(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer);
extern int read_sector_fat(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer);
extern int read_sector_fat_clustlist(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer);
extern int read_sector_dir(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer, vdf_file *dir);
extern int read_sector_data(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer);
extern int read_sector_data_clustlist(vdf_drive *drv, sector_t sector, sectcnt_t scnt, uint8_t *buffer);

#endif /* __VDF_READ_H */
