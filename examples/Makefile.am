AUTOMAKE_OPTIONS = gnu

AM_LDFLAGS = -lvdf -L../libvdf
AM_CFLAGS = -Wall -O3 -s -D_GNU_SOURCE -I../include

noinst_PROGRAMS = create_drive @VDF_STREAM@

#vdf_stream_LDFLAGS = @LIBCURL@ $(AM_CFLAGS)
#vdf_stream_CFLAGS = @LIBCURL_CPPFLAGS@ $(AM_LDFLAGS)
vdf_stream_LDFLAGS = @LIBCURL@ -lvdf -L../libvdf
vdf_stream_CFLAGS = @LIBCURL_CPPFLAGS@ -Wall -O3 -s -D_GNU_SOURCE -I../include

vdf_stream2_LDFLAGS = @LIBCURL@ -lvdf -L../libvdf
vdf_stream2_CFLAGS = @LIBCURL_CPPFLAGS@ -Wall -O3 -s -D_GNU_SOURCE -I../include

create_drive_SOURCES = create_drive.c

vdf_stream_SOURCES = vdf_stream.c
vdf_stream2_SOURCES = vdf_stream2.c

EXTRA_PROGRAMS=vdf_stream vdf_stream2
