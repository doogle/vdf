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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vdf.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "ws2_32.lib")
#endif

//#define DRIVE_SIZE			(32 * 1024)
//#define DRIVE_SIZE			(1 * 1024 * 1024)				// 1 MiB
//#define DRIVE_SIZE		(8 * 1024 * 1024)				// 8 MiB
//#define DRIVE_SIZE		(20 * 1024 * 1024)				// 20 MiB
//#define DRIVE_SIZE		(2LL * 1024 * 1024 * 1024)		// 2 GiB

#define DRIVE_SIZE		MAX_FAT16_DRIVESIZE_WIN95

vcd_driveext ext = { 0 , 512, 1 , 16};

#define VDF_FLAGS		VDF_FAT_AUTO | VDF_VFAT | VDF_FAIL_WARN | VDF_ALIGN_CLUSTER /*| VDF_MBR*/
//#define VDF_FLAGS		VDF_FAT12
//#define VDF_FLAGS		VDF_FAT16 | VDF_VFAT | VDF_FAIL_WARN
//#define VDF_FLAGS		VDF_FAT32 | VDF_VFAT | VDF_FAIL_WARN

#define TEST_TEXT	"This is an example text file\n"	\
					"We can put text here\n"
//#define TEST_TEXT	"01234567"

static ssize_t file_cback(vdf_file_cmd cmd, vdf_file *file, fileoff_t off, filesz_t len, void *buf, void *param) {
	memcpy(buf, TEST_TEXT, len);
	return sizeof(TEST_TEXT);
}

static ssize_t zerofile_cback(vdf_file_cmd cmd, vdf_file *file, fileoff_t off, filesz_t len, void *buf, void *param) {
	memset(buf, 0, len);
	return len;
}

static int transport_cback(vdf_transport *trans, vdf_drive *drv, vdf_tcb_type type, void *data) {
	vdf_tcb_connect *conn = data;
	vdf_tcb_read *rd = data;
	vdf_tcb_error *er = data;
	switch(type) {
		case tcb_connect:
			printf("Connect: id = %u , addr = %s\n", conn->id, inet_ntoa(((struct sockaddr_in*)&conn->addr)->sin_addr));
			break;
		case tcb_disconnect:
			printf("Disconnect: %u\n", conn->id);
			break;
		case tcb_read:
			printf("Read: %u : off = %llu , len = %u\n", rd->id, rd->off, rd->len);
			break;
		case tcb_error:
			printf("Error: %u\n", er->err);
			break;
	}
	return 0;
}

int main(void) {
	vdf_drive *drv;
	vdf_file *fil, *dir;
	vdf_transport *trans;
	filesz_t filesize;

#if 1
	drv = vdf_drive_create(DRIVE_SIZE, VDF_FLAGS);
#else
	drv = vdf_drive_create_ext(DRIVE_SIZE, VDF_FLAGS, &ext);
#endif
	if(drv == NULL) {
		perror("vdf_createdrive");
		return 1;
	}
#if 0
	printf("Created drive with size %lu:\n", DRIVE_SIZE);
	printf("  Bytes per sector:     %u\n", vdf_drive_sectorsize(drv));
	printf("  Sectors per cluster:  %u\n", vdf_drive_clustersectors(drv));
	printf("  Bytes per cluster:    %u\n", vdf_drive_sectorsize(drv) * vdf_drive_clustersectors(drv));
	printf("  Sector count:         %lu\n", vdf_drive_sectors(drv));
	printf("  Cluster count (data): %lu\n", vdf_drive_dataclusters(drv));
	printf("  Actual size of drive: %llu\n", (long long)vdf_drive_sectors(drv) * vdf_drive_sectorsize(drv));
	printf("  Filesystem:           %s\n", vdf_filesystem_name(vdf_drive_filesystem(drv)));
#endif

	filesize = (vdf_drive_dataclusters(drv) - 16) * vdf_drive_clustersize(drv);		/* leave a bit of room */

	//	dir = vdf_add_dir(vdf_drive_root(drv), "subdirectory_withlongname");
//	fil = vdf_add_file_virt(dir, "text.txt", strlen(TEST_TEXT), file_cback, NULL, 0);
//	fil = vdf_add_file_real(vdf_drive_root(drv), NULL, "..\\include\\vdf.h", 0);
//	fil = vdf_add_file_virt(vdf_drive_root(drv), "test.txt", strlen(TEST_TEXT), file_cback, NULL, 0);
	fil = vdf_add_file_virt(vdf_drive_root(drv), "big.fil", filesize, zerofile_cback, NULL, 0);
	vdf_set_drive_label(drv, "my drive");
	vdf_drive_recalc(drv);
	vdf_dump_drive_info(drv, stdout);

	vdf_dump_drive(drv, "test.img");
	//printf("\nAfter dump\n\n");
	//vdf_dump_drive_info(drv, stdout);

	vdf_drive_free(drv);
	return 0;
//	trans = vdf_transport_open(VTD_NBD_SRV, drv, transport_cback, 9000);
	trans = vdf_transport_open(VTD_NBD_CLI_STR, drv, transport_cback, "192.168.1.151", "8000");
	if(trans == NULL) {
		perror("vdf_transport_open");
fail:
		vdf_drive_free(drv);
		return 1;
	}

	vdf_transport_start(trans);
	while(vdf_transport_running(trans) && !vdf_transport_error(trans)) {
		if(vdf_transport_process(trans) == -1) {
			perror("vdf_transport_process");
			goto fail;
		}
	}
	printf("Finished\n");
	vdf_transport_stop(trans);
	vdf_drive_free(drv);
	return 0;
}
