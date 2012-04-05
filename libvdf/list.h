/*
	Copyright (C) 2012 David Steinberg <doogle2600@gmail.com>
	Based on list.h in the Linux kernel sources.

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

#ifndef __LIST_H
#define __LIST_H

#include <stddef.h>
#include "vdf_private.h"

typedef struct _list_head list_head;
struct _list_head {
	list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { (ptr)->next = (ptr); (ptr)->prev = (ptr); } while (0)

static INLINE void _list_add(list_head *item, list_head *prev, list_head *next) {
	next->prev = item;
	item->next = next;
	item->prev = prev;
	prev->next = item;
}

static INLINE void list_add(list_head *item, list_head *head) {
	_list_add(item, head, head->next);
}

static INLINE void list_add_tail(list_head *item, list_head *head) {
	_list_add(item, head->prev, head);
}

static INLINE void _list_del(list_head *prev, list_head *next) {
	next->prev = prev;
	prev->next = next;
}

static INLINE void list_del(list_head *item) {
	_list_del(item->prev, item->next);
	item->next = item->prev = NULL;
}

static INLINE int list_empty(const list_head *head) {
	return head->next == head;
}

static INLINE int list_empty_careful(const list_head *head) {
	list_head *next = head->next;
	return (next == head) && (next == head->prev);
}

#define list_item(ptr, type, member)		((type*)(((char*)ptr) - offsetof(type, member)))

#define list_foreach(pos, head)	\
	for(pos = (head)->next; pos != (head); pos = pos->next)

#define list_foreach_rev(pos, head)	\
	for(pos = (head)->prev; pos != (head); pos = pos->prev)

#define list_foreach_safe(pos, n, head)	\
	for(pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#define list_foreach_safe_rev(pos, n, head)	\
	for(pos = (head)->prev, n = pos->prev; pos != (head); pos = n, n = pos->prev)

#define list_foreach_item(type, pos, head, member)							\
	for(pos = list_item((head)->next, type, member);						\
		&pos->member != (head);												\
		pos = list_item(pos->member.next, type, member))

#define list_foreach_item_rev(type, pos, head, member)						\
	for(pos = list_item((head)->prev, type, member);						\
		&pos->member != (head);												\
		pos = list_item(pos->member.prev, type, member))

#define list_foreach_item_safe(type, pos, n, head, member)					\
	for(pos = list_item((head)->next, type, member),						\
		n = list_item(pos->member.next, type, member);						\
		&pos->member != (head);												\
		pos = n, n = list_item(n->member.next, type, member))

#define list_foreach_item_rev_safe(type, pos, n, head, member)				\
	for(pos = list_item((head)->prev, type, member),						\
		n = list_item(pos->member.prev, type, member);						\
		&pos->member != (head);												\
		pos = n, n = list_item(n->member.prev, type, member))

#endif/*__LIST_H*/
