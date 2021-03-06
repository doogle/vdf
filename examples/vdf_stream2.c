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

/*
This example will connect to a URL using libcurl and stream the data. It is designed to work with an MP3 stream.
The data will be provided as a never-ending file on the FAT drive. If data underflows, a 512-byte block of silent
MP3 is output to the file. If no data is read from the file and the data overflows, the circular buffer data is overwritten.

This is a fairly simple implementation and both dumb and unoptimised. Also, both the transport side and the curl
side simply sit in a loop until the process is killed rather than only making the connections when needed.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vdf.h>
#ifdef _WIN32
#include <windows.h>
#define sleep(x)	Sleep(x * 1000)
#define usleep(x)	Sleep(x / 1000)
#define LINE_MAX	4096
#else
#include <pthread.h>
#include <unistd.h>
#endif
#include <curl/curl.h>
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "libcurl.lib")
#endif

static const uint8_t silent_mp3[512];

#define DRIVE_SIZE		MAX_FAT16_DRIVESIZE_WIN95
//#define DRIVE_SIZE		16777216UL
//#define DRIVE_FLAGS		VDF_FAT_AUTO_NO32 | VDF_FAIL_WARN | VDF_VFAT
//#define DRIVE_FLAGS		VDF_FAT_AUTO | VDF_FAIL_WARN | VDF_VFAT
#define DRIVE_FLAGS		VDF_FAT32 | VDF_FAIL_WARN | VDF_VFAT
#define TRACK_NAME		"stream.mp3"

#define BUFFCNT			128		/* should be adjusted to get a balance of latency and responsiveness */

uint8_t *buffer;				/* our circular buffer */
size_t bufflen;					/* total size of buffer */
sectoff_t bstart, bend;			/* circular buffer positions (in sectors) */
sectoff_t writeoff;				/* current offset we're writing into */

size_t bps;						/* cached bytes-per-sector */
vdf_drive *drv;
vdf_file *fil;
vdf_transport *trans;

filesz_t filesize;

CURL *curl_handle;
CURLM *curl_multi;

int paused, stopped;

static ssize_t file_cback(vdf_file_cmd cmd, vdf_file *file, fileoff_t off, filesz_t len, void *buf, void *param) {
	uint8_t *b = buf;
	size_t total = 0;

	printf("file_cback(off=%u, len=%u)\n", off, len);
	/* we only deal with sectors */
	off /= bps;
	total = len;
	len /= bps;
	printf("file_cback sectors(off=%u, len=%u)\n", off, len);
	/* dumbly just unload the circular buffer */
	while(len != 0) {
		if((bstart == bend) || paused || stopped) {
			/* we've run out of data so feed in silence */
			memcpy(b, silent_mp3, bps);
		} else {
			memcpy(b, buffer + (bstart * bps), bps);
			if(++bstart == BUFFCNT)
				bstart = 0;
		}
		b += bps;
		len--;
	}
	return total;
}

int will_overflow(void) {
	int n = bend + 1;
	if(n == BUFFCNT)
		n = 0;
	return n == bstart;
}

static size_t curl_cback(void *ptr, size_t size, size_t nmemb, void *data) {
	uint8_t *b = ptr;
	size_t want;
	int timeout;

	size *= nmemb;
	nmemb = size;

	/* we don't check for overrun and just overwrite existing data */
	while(size != 0) {
		timeout = 0;
		while((will_overflow() && (timeout++ < 20)) || paused || stopped) {
			if(stopped)
				return 0;
			usleep(100000);
		}
		want = bps - writeoff;
		if(want > size)
			want = size;
		memcpy(buffer + (bend * bps) + writeoff, b, want);
		writeoff += want;
		if(writeoff == bps) {
			if(++bend == BUFFCNT)
				bend = 0;
			writeoff = 0;
		}
		b += want;
		size -= want;
	}
	return nmemb;
}

#ifdef _WIN32
static DWORD WINAPI curl_threadfunc(LPVOID _unused) {
#else
static void *curl_threadfunc(void *_unused) {
#endif
	int i;
	CURLMsg *msg;
	int timeout;
	CURLcode cd = CURLE_OK;
	while(1) {
		while(1) {
retry_ok:
			cd = CURLE_OK;
			timeout = 0;
			while((will_overflow() && (timeout++ < 20)) || (paused) || (stopped)) {
				usleep(100000);
				if(paused || stopped)
					timeout = 18;
			}
			curl_multi_perform(curl_multi, &i);
			while((msg = curl_multi_info_read(curl_multi, &i)) != NULL) {
				if(msg->msg == CURLMSG_DONE) {
					if(msg->data.result == CURLE_OK) {
						printf("Stop (URL complete)\n");
						stopped = paused = 1;
						curl_multi_remove_handle(curl_multi, curl_handle);
						goto retry_ok;
					}
					cd = msg->data.result;
					goto retry;
				}
			}
		}
retry:
		if(cd != CURLE_OK)
			printf("URL error: %s\n", curl_easy_strerror(cd));
		sleep(2);
		printf("Retrying URL connect...\n");
	}
	return 0;
}

#ifdef _WIN32
static DWORD WINAPI vdf_threadfunc(LPVOID _unused) {
#else
static void *vdf_threadfunc(void *_unused) {
#endif
	while(1) {
		if(vdf_transport_start(trans) == 0) {
			while(vdf_transport_process(trans) == 0);
		} else {
			printf("Retrying NBD connect...\n");
			sleep(2);
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
	int tid;
#else
	pthread_t thd;
#endif
	static char line[LINE_MAX], *p;

	if(argc != 3) {
		fprintf(stderr, "Usage: %s <nbd_addr> <nbd_port>\n", argv[0]);
		return 1;
	}

	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();
	//curl_easy_setopt(curl_handle, CURLOPT_URL, argv[1]);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_cback);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "vdf_stream");
	paused = stopped = 1;

	curl_multi = curl_multi_init();
//	curl_multi_add_handle(curl_multi, curl_handle);

	drv = vdf_drive_create(DRIVE_SIZE, DRIVE_FLAGS);
	if(drv == NULL) {
		perror("vdf_createdrive");
		return 1;
	}

	bps = vdf_drive_sectorsize(drv);
	filesize = (vdf_drive_dataclusters(drv) - 16) * vdf_drive_clustersize(drv);		/* leave a bit of room */

	bufflen = bps * BUFFCNT;
	buffer = malloc(bufflen);
	if(buffer == NULL) {
		perror("malloc");
		return 1;
	}
	bstart = bend = 0;
	writeoff = 0;
	paused = stopped = 1;

	fil = vdf_add_file_virt(vdf_drive_root(drv), TRACK_NAME, filesize, file_cback, NULL, 0);
	if(fil == NULL) {
		perror("vdf_add_file_virt");
		return 1;
	}

	trans = vdf_transport_open(VTD_NBD_CLI_STR, drv, NULL, argv[1], argv[2]);
	if(trans == NULL) {
		perror("vdf_transport_open");
		return 1;
	}

#ifdef _WIN32
	if(CreateThread(NULL, 0, curl_threadfunc, NULL, 0, &tid) == NULL) {
		perror("CreateThread");
		return 1;
	}
#else
	if(pthread_create(&thd, NULL, curl_threadfunc, NULL) == -1) {
		perror("pthread_create");
		return 1;
	}
#endif
#ifdef _WIN32
	if(CreateThread(NULL, 0, vdf_threadfunc, NULL, 0, &tid) == NULL) {
		perror("CreateThread");
		return 1;
	}
#else
	if(pthread_create(&thd, NULL, vdf_threadfunc, NULL) == -1) {
		perror("pthread_create");
		return 1;
	}
#endif

	while(fgets(line, LINE_MAX, stdin) != NULL) {
		if((p = strchr(line, '\r')) != NULL)
			*p = 0;
		if((p = strchr(line, '\n')) != NULL)
			*p = 0;
		switch(line[0]) {
			case '>':
				paused = 1;
				if(line[1] != 0) {
					if(!stopped)
						curl_multi_remove_handle(curl_multi, curl_handle);
					stopped = 0;
					printf("Open URL: '%s'\n", line + 1);
					curl_easy_setopt(curl_handle, CURLOPT_URL, line + 1);
					curl_multi_add_handle(curl_multi, curl_handle);
				} else {
					if(stopped)
						continue;
					printf("Resume\n");
				}
				paused = stopped = 0;
				break;
			case '!':
				printf("Stop\n");
				stopped = paused = 1;
				sleep(200);
				bend = bstart = 0;
				curl_multi_remove_handle(curl_multi, curl_handle);
				break;
			case '#':
				printf("Pause\n");
				paused = 1;
				break;
		}
	}
	return 0;
}

/* 512 bytes of mono MP3 silence */
static const uint8_t silent_mp3[] = {
	0xff,0xfa,0x92,0xc0,0xe6,0x9f,0xc5,0x03,0xc0,0x00,0x01,0xa4,0x00,0x00,0x00,0x00,
	0x00,0x00,0x34,0x80,0x00,0x00,0x00,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x4c,0x41,
	0x4d,0x45,0x33,0x2e,0x39,0x33,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0xff,0xfa,0x92,0xc0,0xfa,0xde,0xff,0x83,0xc0,0x00,0x01,0xa4,0x00,0x00,
	0x00,0x00,0x00,0x00,0x34,0x80,0x00,0x00,0x00,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
	0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
};
