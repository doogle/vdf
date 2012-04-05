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
#ifdef _MSC_VER
#define strcasecmp(s1,s2)	_strcmpi(s1,s2)
#include <malloc.h>
#else
#include <strings.h>
#include <alloca.h>
#endif
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vdf.h>
#include "vdf_private.h"
#include "vdf_drive.h"
#include "vdf_file.h"
#include "vdf_transport.h"

static vdf_file *create_file(vdf_file *parent, const char *name);
static int calc_shortname(vdf_file *fil);
static int file_is_deleted(vdf_file *fil);
static vdf_file *find_file_skip(vdf_file *dir, const char *name, int flags, vdf_file *skip);

static int filename_valid(const char *name) {
	return (name != NULL) && (*name != 0) && (name[strcspn(name, "/\\:?*\"<>|")] == 0);
}

static INLINE int file_is_deleted(vdf_file *fil) {
	return !!(fil->flags & VFF_DELETED);
}

int stuff_dosname(char **d, char s) {
	switch(s) {
		case ' ':
			return -1;
		case '+': case ',': case ';':
		case '=': case '[': case ']':
		case '"': case '/': case '.':
			**d = '_';
			break;
		default:
			**d = toupper(s);
			break;
	}
	(*d)++;
	return 0;
}

static int calc_shortname(vdf_file *fil) {
	int ind;
	char shortname[13], *s, *sp, *d, *dp;
	int i;

	sp = strrchr(fil->name, '.');
	if(sp == NULL)
		sp = strchr(fil->name, 0);

	s = fil->name;
	d = shortname;
	for(i=0; (i < 8) && (s < sp); i++)
		stuff_dosname(&d, *s++);
	dp = d - 1;
	if(*sp) {
		s = sp + 1;
		*d++ = '.';
		for(i=0; (i < 3) &&(*s != 0); i++)
			stuff_dosname(&d, *s++);
	}
	*d = 0;

	if(!strcmp(shortname, fil->name)) {
		strcpy(fil->shortname, shortname);
		fil->flags &= ~VFF_LONGNAME;
		set_drive_dirty(fil->drv);
		return 0;
	}

	if((dp - shortname) >= 7) {
		for(ind=1; ind<1000000; ind++) {
			d = dp;
			*d-- = (ind % 10) + '0';
			if(ind >= 10)
				*d-- = ((ind / 10) % 10) + '0';
			if(ind >= 100)
				*d-- = ((ind / 100) % 10) + '0';
			if(ind >= 1000)
				*d-- = ((ind / 1000) % 10) + '0';
			if(ind >= 10000)
				*d-- = ((ind / 10000) % 10) + '0';
			if(ind >= 10000)
				*d-- = ((ind / 10000) % 10) + '0';
			if(ind >= 100000)
				*d-- = ((ind / 100000) % 10) + '0';
			*d = '~';
			if(find_file_skip(vdf_parent_dir(fil), shortname, VFF_SHORTONLY, fil) == NULL)
				break;
		}
		if(ind == 1000000)
			return -1;
	}
	strcpy(fil->shortname, shortname);
	fil->flags |= VFF_LONGNAME;
	set_drive_dirty(fil->drv);
	return 0;
}

static vdf_file *create_file(vdf_file *parent, const char *name) {
	vdf_file *fil;

	if(parent != NULL) {
		if(parent->flags & VFF_DELETED) {
			errno = EINVAL;
			return NULL;
		}
		if(vdf_drive_is_locked(parent->drv)) {
			errno = EPERM;
			return NULL;
		}
		if(!vdf_file_is_dir(parent)) {
			errno = EINVAL;
			return NULL;
		}
		if(parent->drv->filesys != VDF_FAT32) {
			if(parent->flags & VFF_ROOTDIR) {
				if(parent->dir.cnt == parent->drv->root_dir_cnt) {
					errno = ENOSPC;
					return NULL;
				}
			}
		}
	}
	if(name != NULL) {
		if(!filename_valid(name)) {
			errno = EINVAL;
			return NULL;
		}
		if(parent != NULL) {
			if(find_file_skip(parent, name, 0, NULL) != NULL) {
				errno = EEXIST;
				return NULL;
			}
		}
	}

	fil = malloc(sizeof(vdf_file));
	if(fil == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	memset(fil, 0, sizeof(vdf_file));
	fil->fatsize = fil->fatclusters = -1;
	fil->attr = VFA_ATTRIBUTE;

	fil->parent = parent;
	if(parent != NULL) {
		fil->drv = parent->drv;
		list_add_tail(&fil->dirlist, &parent->dir.entries);
		list_add_tail(&fil->alllist, &fil->drv->all_files);
		parent->dir.cnt++;
		fil->drv->file_cnt++;
		set_drive_dirty(fil->drv);
	} else {
		fil->alllist.next = fil->alllist.prev = NULL;
	}

	if(name != NULL) {
		fil->name = strdup(name);
		if(fil->name == NULL) {
			errno = ENOMEM;
			goto exit_err;
		}
		if(calc_shortname(fil) == -1)
			goto exit_err;
	}
	return fil;
exit_err:
	if(parent != NULL) {
		list_del(&fil->dirlist);
		list_del(&fil->alllist);
	}
	if(fil->name != NULL)
		free(fil->name);
	free(fil);
	return NULL;
}

static vdf_file *create_dir(vdf_file *parent, const char *name) {
	vdf_file *dir;
	dir = create_file(parent, name);
	if(dir == NULL)
		return NULL;
	dir->attr |= VFA_DIR;
	dir->dir.cnt = 0;
	INIT_LIST_HEAD(&dir->dir.entries);
	return dir;
}

vdf_file *create_root_dir(vdf_drive *drv) {
	vdf_file *dir = create_dir(NULL, NULL);
	if(dir == NULL)
		return NULL;
	dir->drv = drv;
	dir->flags = VFF_ROOTDIR;
	if(drv->filesys == VDF_FAT32) {
		list_add_tail(&dir->alllist, &drv->all_files);
		drv->file_cnt++;
	}
	dir->name = "ROOT";
	return dir;
}

LIBFUNC vdf_file *vdf_add_dir(vdf_file *parent, const char *name) {
	vdf_file *dir;
	if((parent == NULL) || (name == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	dir = create_dir(parent, name);
	if(dir == NULL)
		return NULL;
	vdf_set_file_date(dir, time(NULL));
	return dir;
}

LIBFUNC vdf_file *vdf_add_file_real(vdf_file *parent, const char *name, const char *path, int flags) {
	vdf_file *fil;
	struct stat st;
	char *p;
	size_t size;

	if((parent == NULL) || (path == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	if(stat(path, &st) == -1)
		return NULL;
#ifdef _MSC_VER
	if(!(st.st_mode & _S_IFREG)) {
		errno = EINVAL;
		return NULL;
	}
#else
	if(!S_ISREG(st.st_mode)) {
		errno = EINVAL;
		return NULL;
	}
#endif
	if(name == NULL) {
		name = strchr(path, 0);
		while(1) {
			if(name == path)
				break;
			switch(*name) {
#ifdef _WIN32
				case '\\':
#endif
				case '/':
					name++;
					goto found;
			}
			name--;
		}
	}
found:
	p = strdup(path);
	if(path == NULL)
		return NULL;
	fil = create_file(parent, name);
	if(fil == NULL) {
		free(p);
		return NULL;
	}
	size = st.st_size;
	fil->size = size;
	size += fil->drv->bpc - 1;
	size /= fil->drv->bpc;
	fil->clusters = size;
	vdf_set_file_date(fil, st.st_ctime);
	fil->real.path = p;
	return fil;
}

LIBFUNC vdf_file *vdf_add_file_virt(vdf_file *parent, const char *name, size_t len, vdf_file_callback cback, void *param, int flags) {
	vdf_file *fil;
	if((parent == NULL) || (name == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	fil = create_file(parent, name);
	if(fil == NULL)
		return NULL;
	fil->flags |= VFF_VIRT;
	fil->virt.cback = cback;
	fil->virt.param = param;
	vdf_set_file_size(fil, len);
	vdf_set_file_date(fil, time(NULL));
	return fil;
}

int delete_file(vdf_file *file) {
	vdf_file *sfil, *sfil_n;
	file->flags |= VFF_DELETED;
	file->drv->file_cnt--;
	if(file->alllist.next != NULL)
		list_del(&file->alllist);
	if(vdf_file_is_dir(file)) {
		list_foreach_item_safe(vdf_file, sfil, sfil_n, &file->parent->dir.entries, dirlist) {
			delete_file(sfil);
		}
	} else if(vdf_file_is_real(file)) {
		free(file->real.path);
	}
	if(!(file->flags & VFF_ROOTDIR))
		free(file->name);
	free(file);
	return 0;
}

LIBFUNC int vdf_delete_file(vdf_file *file) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	if(file == file->drv->root_dir) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	list_del(&file->dirlist);
	set_drive_dirty(file->drv);
	return delete_file(file);
}

LIBFUNC int vdf_move_file(vdf_file *file, vdf_file *new_parent) {
	if(!file_is_valid(file) || (file->parent == NULL) || !file_is_valid(new_parent)) {
		errno = EINVAL;
		return -1;
	}
	if(file->drv != new_parent->drv) {
		errno = EINVAL;
		return -1;
	}
	if(!vdf_file_is_dir(new_parent)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	if(file->parent == new_parent)
		return 0;
	if(find_file_skip(new_parent, file->name, VFF_LONGONLY, NULL) != NULL) {
		errno = EEXIST;
		return -1;
	}
	list_del(&file->dirlist);
	file->parent->dir.cnt--;
	list_add(&file->dirlist, &new_parent->dir.entries);
	new_parent->dir.cnt++;
	file->parent = new_parent;
	set_drive_dirty(file->drv);
	return calc_shortname(file);
}

static vdf_file *find_file_skip(vdf_file *dir, const char *name, int flags, vdf_file *skip) {
	vdf_file *fil;
	list_foreach_item(vdf_file, fil, &dir->dir.entries, dirlist) {
		if(fil == skip)
			continue;
		if(!(flags & VFF_SHORTONLY)) {
			if(!strcasecmp(name, fil->name))
				return fil;
		}
		if(!(flags & VFF_LONGONLY)) {
			if(!strcasecmp(name, fil->shortname))
				return fil;
		}
	}
	return NULL;
}

LIBFUNC vdf_file *vdf_find_file(vdf_file *dir, const char *name, int flags) {
	return vdf_find_file_skip(dir, name, flags, NULL);
}

LIBFUNC vdf_file *vdf_find_file_skip(vdf_file *dir, const char *name, int flags, vdf_file *skip) {
	if(!file_is_valid(dir) || !vdf_file_is_dir(dir) || (name == NULL) || (*name == 0)) {
		errno = EINVAL;
		return NULL;
	}
	return find_file_skip(dir, name, flags, skip);
}

static vdf_file *find_path(vdf_file *dir, const char *path, int flags) {
	char *p;
	const char *d;
	vdf_file *fil;
	int l;
	for(d=path; *d; d++) {
		if((*d == '/') || (*d == '\\'))
			break;
	}
	if(*d == 0)
		return find_file_skip(dir, path, flags, NULL);
	l = d - path;
	p = alloca(l + 1);
	memcpy(p, path, l);
	p[l] = 0;
	fil = find_file_skip(dir, p, flags, NULL);
	if(fil == NULL)
		return NULL;
	if(!vdf_file_is_dir(fil))
		return NULL;
	return find_path(fil, d + 1, flags);
}

LIBFUNC vdf_file *vdf_find_path(vdf_file *dir, const char *path, int flags) {
	if(!file_is_valid(dir) || !vdf_file_is_dir(dir)) {
		errno = EINVAL;
		return NULL;
	}
	return find_path(dir, path, flags);
}

LIBFUNC vdf_file *vdf_parent_dir(vdf_file *file) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return NULL;
	}
	return file->parent;
}

LIBFUNC vdf_drive *vdf_file_drive(vdf_file *file) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return NULL;
	}
	return file->drv;
}

LIBFUNC int vdf_dir_count(vdf_file *dir) {
	if(!file_is_valid(dir) || !vdf_file_is_dir(dir)) {
		errno = EINVAL;
		return -1;
	}
	return dir->dir.cnt;
}

LIBFUNC vdf_file *vdf_dir_entry(vdf_file *dir, int index) {
	vdf_file *fil;
	if(!file_is_valid(dir) || !vdf_file_is_dir(dir) || (dir->parent == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	if(index >= dir->dir.cnt) {
		errno = ERANGE;
		return NULL;
	}
	list_foreach_item(vdf_file, fil, &dir->dir.entries, dirlist) {
		if(index-- == 0)
			return fil;
	}
	errno = ERANGE;
	return NULL;
}

LIBFUNC int vdf_get_file_index(vdf_file *file) {
	vdf_file *dir, *fil;
	int ind;
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	dir = file->parent;
	ind = 0;
	list_foreach_item(vdf_file, fil, &dir->dir.entries, dirlist) {
		if(fil == file)
			break;
		ind++;
	}
	return ind;
}

LIBFUNC int vdf_file_up(vdf_file *file) {
	list_head *p, *tp, *tn;
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	p = file->dirlist.prev;
	if(p == &file->parent->dir.entries) {
		errno = ERANGE;
		return -1;
	}
	tp = p->prev;
	tn = p->next;
	p->prev = file->dirlist.prev;
	p->next = file->dirlist.next;
	file->dirlist.prev = tp;
	file->dirlist.next = tn;
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC int vdf_file_down(vdf_file *file) {
	list_head *p, *tp, *tn;
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	p = file->dirlist.next;
	if(p == &file->parent->dir.entries) {
		errno = ERANGE;
		return -1;
	}
	tp = p->prev;
	tn = p->next;
	p->prev = file->dirlist.prev;
	p->next = file->dirlist.next;
	file->dirlist.prev = tp;
	file->dirlist.next = tn;
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC vdf_file *vdf_file_prev(vdf_file *file) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	if(file->dirlist.prev == &file->parent->dir.entries)
		return NULL;
	return list_item(file->dirlist.prev, vdf_file, dirlist);
}

LIBFUNC vdf_file *vdf_file_next(vdf_file *file) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return NULL;
	}
	if(file->dirlist.next == &file->parent->dir.entries)
		return NULL;
	return list_item(file->dirlist.next, vdf_file, dirlist);
}

LIBFUNC ssize_t vdf_get_file_name(vdf_file *file, char *name, size_t len) {
	size_t l;
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	if(file->flags & VFF_ROOTDIR) {
		*name = 0;
		return 0;
	}
	l = strlen(file->name);
	if(name == NULL)
		return l;
	if(len == 0) {
		errno = EINVAL;
		return -1;
	}
	if(l >= len) {
		errno = ENOMEM;		/* TODO: this should be something else */
		return -1;
	}
	strcpy(name, file->name);
	return l;
}

LIBFUNC ssize_t vdf_get_file_shortname(vdf_file *file, char *name) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	if(file->flags & VFF_ROOTDIR) {
		*name = 0;
		return 0;
	}
	strcpy(name, file->shortname);
	return strlen(file->shortname);
}

LIBFUNC int vdf_set_file_name(vdf_file *file, const char *name) {
	char *n;
	if((name == NULL) || !file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	if(!filename_valid(name)) {
		errno = EINVAL;
		return -1;
	}
	if(find_file_skip(file->parent, name, VFF_LONGONLY, file) != NULL) {
		errno = EEXIST;
		return -1;
	}
	n = strdup(name);
	if(n == NULL) {
		errno = ENOMEM;
		return -1;
	}
	free(file->name);
	file->name = n;
	calc_shortname(file);
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC int vdf_set_file_shortname(vdf_file *file, const char *name) {
	char *n;
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	if(!filename_valid(name)) {
		errno = EINVAL;
		return -1;
	}
	n = strchr(name, '.');
	if(n == NULL) {
		if(strlen(name) > 8) {
			errno = EINVAL;
			return -1;
		}
	} else {
		if((n - name) > 8) {
			errno = EINVAL;
			return -1;
		}
		if(n[1] == 0) {				/* disallow 'NAME.' */
			errno = EINVAL;
			return -1;
		}
		if(strlen(n + 1) > 3) {
			errno = EINVAL;
			return -1;
		}
	}
	if(find_file_skip(file->parent, name, 0, file) != NULL) {
		errno = EEXIST;
		return -1;
	}
#if 1
	if(!strcasecmp(file->name, name))
#else
	if(!strcmp(file->name, name))
#endif
		file->flags &= ~VFF_LONGNAME;
	for(n=file->shortname; *name != 0; name++, n++)
		*n = toupper(*name);
	*n = 0;
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC int vdf_file_is_virt(vdf_file *file) {
	return file_is_valid(file) && (file->flags & VFF_VIRT);
}

LIBFUNC int vdf_file_is_dir(vdf_file *file) {
	return file_is_valid(file) && (file->attr & VFA_DIR);
}

LIBFUNC int vdf_file_is_real(vdf_file *file) {
	return file_is_valid(file) && !(file->flags & VFF_VIRT);
}

LIBFUNC int vdf_dir_is_root(vdf_file *file) {
	return vdf_file_is_dir(file) && (file->flags & VFF_ROOTDIR);
}

LIBFUNC int vdf_set_file_size(vdf_file *file, filesz_t size) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_file_is_real(file)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	file->size = size;
	size += file->drv->bpc - 1;
	size /= file->drv->bpc;
	file->clusters = size;
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC sfilesz_t vdf_get_file_size(vdf_file *file) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	return file->size;
}

LIBFUNC int vdf_set_fat_file_size(vdf_file *file, sfilesz_t size) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_file_is_real(file)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	if((filesz_t)size > file->size) {
		errno = EINVAL;
		return -1;
	}
	file->fatsize = size;
	if(size == -1) {
		file->fatclusters = -1;
	} else {
		size += file->drv->bpc - 1;
		size /= file->drv->bpc;
		file->fatclusters = size;
	}
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC sfilesz_t vdf_get_fat_file_size(vdf_file *file) {
	if(!file_is_valid(file) || (file->parent == NULL)) {
		errno = EINVAL;
		return -1;
	}
	return file->fatsize;
}

LIBFUNC int vdf_set_file_date(vdf_file *file, time_t date) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	file->date = date;
	set_drive_dirty(file->drv);
	return 0;
}

LIBFUNC time_t vdf_get_file_date(vdf_file *file) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	return file->date;
}

LIBFUNC int vdf_set_file_attr(vdf_file *file, int attr, int mask) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	if(vdf_drive_is_locked(file->drv)) {
		errno = EPERM;
		return -1;
	}
	mask &= ~VFA_USER_ATTR;
	attr &= mask;
	file->attr &= ~mask;
	file->attr |= attr;
	set_drive_dirty(file->drv);
	return file->attr;
}

LIBFUNC int vdf_get_file_attr(vdf_file *file) {
	if(!file_is_valid(file)) {
		errno = EINVAL;
		return -1;
	}
	return file->attr;
}

int fents_needed(vdf_file *fil) {
	if(!(fil->drv->flags & VDF_VFAT))
		return 0;
	if(!(fil->flags & VFF_LONGNAME))
		return 0;
	return (strlen(fil->name) / 13) + 1;
}

int dir_recalc(vdf_file *dir) {
	vdf_file *fil;
	int c;

	if(dir->parent == NULL) {
		if(dir->drv->label == NULL)
			c = 0;
		else
			c = 1;		/* disk label */
	} else
		c = 2;			/* '.' and '..' */
	dir->dir.fent_cnt = c;
	list_foreach_item(vdf_file, fil, &dir->dir.entries, dirlist) {
		if(vdf_file_is_dir(fil)) {
			if(dir_recalc(fil) == -1)
				return -1;
		} else {
			if(file_recalc_dir(fil) == -1)
				return -1;
		}
	}
	dir->size = dir->dir.fent_cnt * 32;
	dir->clusters = dir->size;
	dir->clusters += dir->drv->bpc - 1;
	dir->clusters /= dir->drv->bpc;
	if((dir->parent != NULL) || (dir->drv->filesys == VDF_FAT32)) {
		if(file_recalc_dir(dir) == -1)
			return -1;
	}
	return 0;
}

int file_recalc_dir(vdf_file *file) {
	if(file->parent != NULL) {
		file->fent_start = file->parent->dir.fent_cnt;
		file->fent_cnt = fents_needed(file);
		file->parent->dir.fent_cnt += file->fent_cnt + 1;
	}
	return 0;
}

int file_recalc_offsize(vdf_file *file) {
	if(file->size != 0) {
		file->drv->nonempty_file_cnt++;
		file->start = file->drv->data_cluster_end;
		if(file->fatclusters != -1)
			file->data_end = file->start + file->fatclusters;
		file->drv->data_cluster_end += file->clusters;
		file->end = file->drv->data_cluster_end;
		if(file->fatclusters == -1)
			file->data_end = file->end;
		if(file->drv->data_end >= file->drv->sectors)
			return -1;
		file->startsect = file->drv->data_end;
		file->drv->data_end += file->clusters * file->drv->spc;
		if(file->drv->data_end >= file->drv->sectors)
			return -1;
		file->endsect = file->drv->data_end;
	} else {
		file->start = 0;
	}
	return 0;
}
