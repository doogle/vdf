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

#ifndef __VDF_PRIVATE_H
#define __VDF_PRIVATE_H

#define WRITE_DEBUG		/* TODO: should be in config.h */

#ifdef _MSC_VER
#define INLINE		_inline
#else
#define INLINE		inline
#endif

#define KiB(x)		((x) * 1024)
#define MiB(x)		(KiB(x) * 1024)
#define GiB(x)		(((uint64_t)MiB(x)) * 1024)
#define TiB(x)		(GiB(x) * 1024)

#ifdef _MSC_VER
#pragma warning( disable : 4996 )
#define STRUCT_PACK
#else
#define STRUCT_PACK	__attribute__ ((__packed__))
#endif

#if defined(__GNUC__) && defined(__BYTE_ORDER__)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BIGENDIAN
#else
#define LITTLEENDIAN
#endif
#endif

/* TODO: endianess test and relevant conversion macros */
#ifdef BIGENDIAN
#error TODO __BYTE_ORDER__
#else
#define le_16(x)		(x)
#define le_32(x)		(x)
#endif

#endif /* __VDF_PRIVATE_H */
