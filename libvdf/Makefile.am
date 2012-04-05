AUTOMAKE_OPTIONS = gnu

libvdf_la_CFLAGS = -Wall -O3 -I../include

lib_LTLIBRARIES = libvdf.la

libvdf_la_SOURCES = \
	bootblock.c drive.c dump.c fat.c file.c read.c read_data.c \
	read_dir.c transport.c vdf_sock.c \
	transport/nbd_client.c transport/nbd_server.c

libvdf_la_LDFLAGS = -version-info $(VDF_LIBVERSION) \
					--retain-symbols-file libvdf.sym -pthread

noinst_HEADERS = \
	list.h vdf_private.h vdf_drive.h vdf_file.h \
	vdf_read.h vdf_sock.h vdf_transport.h \
	transport/nbd.h

libvdf_la_DEPENDENCIES = libvdf.sym

libvdf.sym: ../include/vdf.h
	sed -nr '/extern LIBFUNC/s/^.*extern LIBFUNC [a-zA-Z_]* [*]?([a-zA-Z_]*)\(.*$$/\1/g p' $< > $@

include_HEADERS = ../include/vdf.h

MOSTLYCLEANFILES = libvdf.sym
