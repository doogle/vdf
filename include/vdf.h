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

#ifndef __VDF_H
#define __VDF_H

#include <sys/types.h>
#include <stdio.h>
#ifdef _MSC_VER
#include <BaseTsd.h>

typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#define ssize_t SSIZE_T
#define uint32_t UINT32
#include <windows.h>
#else
#include <stdint.h>
#include <sys/socket.h>
#endif

#ifdef _MSC_VER
#ifdef LIBVDF_SRC
#define LIBFUNC	__declspec( dllexport )
#else
#define LIBFUNC	__declspec( dllimport )
#endif
#else
#define LIBFUNC
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef uint32_t	sector_t;
typedef uint32_t	cluster_t;
typedef uint32_t	sectoff_t;
typedef uint32_t	clustoff_t;
typedef uint64_t	driveoff_t;
typedef uint32_t	fileoff_t;

typedef uint32_t	sectcnt_t;
typedef uint32_t	clustcnt_t;
typedef uint64_t	drivesz_t;
typedef uint32_t	filesz_t;

typedef int32_t		ssectcnt_t;
typedef int32_t		sclustcnt_t;
typedef int64_t		sdrivesz_t;
typedef int32_t		sfilesz_t;

typedef struct _vdf_drive vdf_drive;
typedef struct _vdf_file vdf_file;
typedef struct _vdf_transport vdf_transport;

typedef enum _vdf_file_cmd {
	vfc_read,
	vfc_write
} vdf_file_cmd;

typedef int (*vdf_file_callback)(vdf_file_cmd cmd, vdf_file *file, fileoff_t off, filesz_t len, void *buf, void *param);

/* Extended parameters for vdf_createdrive_ext() */
typedef struct _vcd_driveext {
	int			flags;
	ssize_t		bytes_per_sector;
	int			sectors_per_cluster;
	int			root_dir_cnt;
} vcd_driveext;

/* Flags for vcd_driveext.flags */
#define VDE_AUTO_BPS		0x01
#define VDE_AUTO_SPC		0x02
#define VDE_AUTO_ROOTENT	0x04

/* Flags for vdf_createdrive() and related */
#define VDF_VFAT			0x0100		/* use long file names */
#define VDF_MBR				0x0200		/* disk has MBR with single partition */
#define VDF_MBR_PAD			0x0400		/* pad MBR. this is similar to how DOS partitions, but wastes space */
#define VDF_ALIGN_CLUSTER	0x0800		/* align all structures to cluster size */
#define VDF_FAIL_WARN		0x1000		/* fail if parameters are dubious (eg. too few cluster for FAT type) */
#ifdef ENABLE_CLUSTER_LIST
#define VDF_CLUSTERLIST		0x2000		/* store a full list of clusters. can use more memory for large drives */
#define VDF_ENABLE_WRITE	0x2000		/* enable writing. implies VDF_CLUSTERLIST */
#endif
#define VDF_FAT_AUTO		0x00		/* choose FAT type automatically based on size */
#define VDF_FAT_AUTO_NO32	0x10		/* choose FAT12 or FAT16 automatically, based on size */
#define VDF_FAT_SAME		0x20		/* keep current (possibly automatically chosen) FAT type. for use with vdf_recreate() and vdf_recreate_ext() */
#define VDF_FS_MASK			0xff

/* Filesystem IDs. Also used as flags for vdf_createdrive() and related */
#define VDF_FAT12			0x01
#define VDF_FAT16			0x02
#define VDF_FAT32			0x03

/* File attributes */
#define VFA_READONLY		0x01
#define VFA_HIDDEN			0x02
#define VFA_SYSTEM			0x04
#define VFA_DIR				0x10		/* cannot be changed using vdf_set_file_attr() */
#define VFA_ATTRIBUTE		0x20

typedef enum _vdf_driver {
	VTD_NBD_LOCAL,				/* Local Network Block Driver.					Params: char *dev_name				*/
	VTD_NBD_SRV,				/* NBD server (for use with nbd-client.			Params: int port					*/
	VTD_NBD_CLI_STR,			/* NBD client (for use with use nbd-devserver).	Params: char *addr, char *port		*/
	VTD_NBD_CLI_ADDR,			/* NBD client (for use with use nbd-devserver).	Params: struct sockaddr_in *addr	*/
	VTD_VDFP_STR,				/* VDF Protocol client.							Params: char *addr, char *port		*/
	VTD_VDFP_ADDR,				/* VDF Protocol client.							Params: struct sockaddr_in *addr	*/

	vdf_driver_cnt
} vdf_driver;

/* flags for vdf_find_*() */
#define VFF_SHORTONLY		0x01
#define VFF_LONGONLY		0x02

/* transport driver flags */
#define TDF_NONBLOCK		0x01			/* supports non-blocking operation */
#define TDF_SUPPORT_FD		0x02			/* can provide a file-descriptor for poll() and select() */

typedef struct _vdf_tcb_connect {
	uint32_t			id;
	struct sockaddr		addr;
	size_t				addrlen;
} vdf_tcb_connect;

typedef struct _vdf_tcb_read {
	uint32_t			id;
	driveoff_t			off;
	drivesz_t			len;
} vdf_tcb_read;

typedef struct _vdf_tcb_error {
	int					err;
} vdf_tcb_error;

typedef enum _vdf_tcb_type {
	tcb_connect,				/* param is vdf_tcb_connect*	*/
	tcb_disconnect,				/* param is vdf_tcb_connect*	*/
	tcb_read,					/* param is vdf_tcb_read*		*/
	tcb_error,					/* param is vdf_tcb_error*		*/
} vdf_tcb_type;

/* These are sizes when using 512 bytes sectors and are not strictly enforced by VDF */
/* You will be able to generate larger drives and files by using sector sizes larger than 512 bytes but this may not be supported by other devices/OSes */
#define MAX_FAT12_DRIVESIZE			(16777216UL)			/* 16 MiB */
#define MAX_FAT16_DRIVESIZE			(4294967296ULL)			/* 4 GiB */
#define MAX_FAT16_DRIVESIZE_WIN95	(2147483648ULL)			/* 2 GiB */
#define MAX_FAT32_DRIVESIZE			(2199023255040ULL)		/* Just under 2 TiB */
#define MAX_FAT32_DRIVESIZE_WIN95	(273804034048UL)		/* Just under 255 GiB */

#define MAX_FAT12_FILESIZE			(16736256)				/* Just under 16 MiB */
#define MAX_FAT16_FILESIZE			(4294967295UL)			/* 4 GiB - 1 byte */
#define MAX_FAT32_FILESIZE			(4294967295UL)			/* 4 GiB - 1 byte */

typedef int (*vdf_transport_cback)(vdf_transport *trans, vdf_drive *drv, vdf_tcb_type type, void *data);

extern LIBFUNC const char *vdf_filesystem_name(int filesys);

extern LIBFUNC vdf_drive *vdf_drive_create(uint64_t size, int flags);
extern LIBFUNC vdf_drive *vdf_drive_create_ext(uint64_t size, int flags, vcd_driveext *ext);
extern LIBFUNC int vdf_drive_free(vdf_drive *drv);

extern LIBFUNC vdf_drive *vdf_drive_recreate(vdf_drive *drv, uint64_t size, int flags);
extern LIBFUNC vdf_drive *vdf_drive_recreate_ext(vdf_drive *drv, uint64_t size, int flags, vcd_driveext *ext);
extern LIBFUNC vdf_drive *vdf_drive_recreate_extsame(vdf_drive *drv, uint64_t size, int flags, int extflags);

extern LIBFUNC ssize_t vdf_drive_sectorsize(vdf_drive *drv);
extern LIBFUNC int vdf_drive_clustersectors(vdf_drive *drv);
extern LIBFUNC size_t vdf_drive_clustersize(vdf_drive *drv);
extern LIBFUNC int64_t vdf_drive_bytes(vdf_drive *drv);

extern LIBFUNC ssectcnt_t vdf_drive_sectors(vdf_drive *drv);
extern LIBFUNC sclustcnt_t vdf_drive_dataclusters(vdf_drive *drv);
extern LIBFUNC sclustcnt_t vdf_drive_usedclusters(vdf_drive *drv);

extern LIBFUNC int vdf_drive_flags(vdf_drive *drv);
extern LIBFUNC int vdf_drive_filesystem(vdf_drive *drv);

extern LIBFUNC ssize_t vdf_get_drive_label(vdf_drive *drv, char *label, size_t len);
extern LIBFUNC int vdf_set_drive_label(vdf_drive *drv, const char *label);
extern LIBFUNC int vdf_set_drive_serial(vdf_drive *drv, uint32_t serial);
extern LIBFUNC int32_t vdf_get_drive_serial(vdf_drive *drv);

extern LIBFUNC vdf_file *vdf_drive_root(vdf_drive *drv);

extern LIBFUNC vdf_file *vdf_add_dir(vdf_file *parent, const char *name);
extern LIBFUNC vdf_file *vdf_add_file_real(vdf_file *parent, const char *name, const char *path, int flags);
extern LIBFUNC vdf_file *vdf_add_file_virt(vdf_file *parent, const char *name, size_t len, vdf_file_callback cback, void *param, int flags);

extern LIBFUNC int vdf_delete_file(vdf_file *file);
extern LIBFUNC int vdf_move_file(vdf_file *file, vdf_file *new_parent);

extern LIBFUNC vdf_file *vdf_find_file(vdf_file *dir, const char *name, int flags);
extern LIBFUNC vdf_file *vdf_find_file_skip(vdf_file *dir, const char *name, int flags, vdf_file *skip);
extern LIBFUNC vdf_file *vdf_find_path(vdf_file *dir, const char *path, int flags);

extern LIBFUNC vdf_file *vdf_parent_dir(vdf_file *file);
extern LIBFUNC vdf_drive *vdf_file_drive(vdf_file *file);

extern LIBFUNC int vdf_dir_count(vdf_file *dir);
extern LIBFUNC vdf_file *vdf_dir_entry(vdf_file *dir, int index);
extern LIBFUNC int vdf_get_file_index(vdf_file *file);
extern LIBFUNC int vdf_file_up(vdf_file *file);
extern LIBFUNC int vdf_file_down(vdf_file *file);
extern LIBFUNC vdf_file *vdf_file_prev(vdf_file *file);
extern LIBFUNC vdf_file *vdf_file_next(vdf_file *file);

extern LIBFUNC ssize_t vdf_get_file_name(vdf_file *file, char *name, size_t len);
extern LIBFUNC ssize_t vdf_get_file_shortname(vdf_file *file, char *name);
extern LIBFUNC int vdf_set_file_name(vdf_file *file, const char *name);
extern LIBFUNC int vdf_set_file_shortname(vdf_file *file, const char *name);

extern LIBFUNC int vdf_file_is_virt(vdf_file *file);
extern LIBFUNC int vdf_file_is_real(vdf_file *file);
extern LIBFUNC int vdf_file_is_dir(vdf_file *file);
extern LIBFUNC int vdf_dir_is_root(vdf_file *file);

extern LIBFUNC int vdf_set_file_size(vdf_file *file, filesz_t size);
extern LIBFUNC sfilesz_t vdf_get_file_size(vdf_file *file);
extern LIBFUNC int vdf_set_fat_file_size(vdf_file *file, sfilesz_t size);
extern LIBFUNC sfilesz_t vdf_get_fat_file_size(vdf_file *file);

extern LIBFUNC int vdf_set_file_date(vdf_file *file, time_t date);
extern LIBFUNC time_t vdf_get_file_date(vdf_file *file);
extern LIBFUNC int vdf_set_file_attr(vdf_file *file, int attr, int mask);
extern LIBFUNC int vdf_get_file_attr(vdf_file *file);

extern LIBFUNC int vdf_drive_is_dirty(vdf_drive *drv);
extern LIBFUNC int vdf_drive_recalc(vdf_drive *drv);
extern LIBFUNC int vdf_drive_lock(vdf_drive *drv);
extern LIBFUNC int vdf_drive_unlock(vdf_drive *drv);
extern LIBFUNC int vdf_drive_is_locked(vdf_drive *drv);

extern LIBFUNC int vdf_read_sector(vdf_drive *drv, sector_t sector, void *buffer);
extern LIBFUNC int vdf_read_sectors(vdf_drive *drv, sector_t sector, sectcnt_t cnt, void *buffer);
extern LIBFUNC sdrivesz_t vdf_read_bytes(vdf_drive *drv, driveoff_t off, drivesz_t cnt, void *buffer);
extern LIBFUNC int vdf_dump_drive(vdf_drive *drv, const char *path);
extern LIBFUNC int vdf_dump_drive_info(vdf_drive *drv, FILE *f);

extern LIBFUNC int vdf_transport_present(vdf_driver driver);
extern LIBFUNC int vdf_transport_flags(vdf_driver driver);
extern LIBFUNC ssize_t vdf_transport_name(vdf_driver driver, char *name, size_t len);
extern LIBFUNC vdf_transport *vdf_transport_open(vdf_driver driver, vdf_drive *drv, vdf_transport_cback cback, ...);
extern LIBFUNC int vdf_transport_close(vdf_transport *trans);
extern LIBFUNC int vdf_transport_start(vdf_transport *trans);
extern LIBFUNC int vdf_transport_stop(vdf_transport *trans);
extern LIBFUNC int vdf_transport_running(vdf_transport *trans);
extern LIBFUNC int vdf_transport_error(vdf_transport *trans);
extern LIBFUNC int vdf_set_transport_non_block(vdf_transport *trans, int non_block);
extern LIBFUNC int vdf_transport_process(vdf_transport *trans);
extern LIBFUNC int vdf_get_transport_fd(vdf_transport *trans);
extern LIBFUNC int vdf_transport_active(vdf_transport *trans);

extern LIBFUNC int vdf_drive_start(vdf_drive *drv);
extern LIBFUNC int vdf_drive_stop(vdf_drive *drv);
extern LIBFUNC int vdf_drive_active(vdf_drive *drv);
extern LIBFUNC int vdf_drive_running(vdf_drive *drv);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __VDF_H */
