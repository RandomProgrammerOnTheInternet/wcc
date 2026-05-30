/* Inspired by Cuik's common folder. */
#ifndef LIST_H_
#define LIST_H_

#include <stddef.h>
#include <stdlib.h>

#include "base.h"

/* function that given a list size returns a new size to fit more elements */
/* used for resizing list */
#ifndef LIST_NEW_SIZE
#define LIST_NEW_SIZE(size) (2 * (size))
#endif

#ifndef LIST_DEFAULT_SIZE
#define LIST_DEFAULT_SIZE (64)
#endif

/* list header */
typedef struct list_hdr {
	/* how many elements in the list */
	size_t size;
	/* how much allocated memory for the list (capacity) */
	size_t cap;
	/* data (can be really any type, we are using VLAs anyway) */
	uint8_t data[];
} list_hdr_t;

/* syntax sugar */
// clang-format off
#define LIST(type) type*
// clang-format on

list_hdr_t *list_hdr(void *list);
void *list_data(list_hdr_t *hdr);
void *list_donotuse_make(size_t count, size_t size);
void list_donotuse_delete(void *list);
void *list_donotuse_reserve(void *list, size_t count, size_t size);
void *list_donotuse_fit(void *list, size_t count, size_t size);

/* Makes a list with type `type` */
#define list_make(type) list_donotuse_make(LIST_DEFAULT_SIZE, sizeof(type))

/* Deletes a list */
#define list_delete(list) list_donotuse_delete((list))

/* Appends an element `elem` to list `list` */
#define list_append(list, elem)                               \
	do {                                                      \
		(list) = list_donotuse_fit((list), 1, sizeof(*list)); \
		list_hdr_t *hdr = list_hdr((list));                   \
		(list)[hdr->size++] = elem;                           \
	} while(0);

/* list_size and list_len are the same. gets size/length of list */
#define list_size(list) (list_hdr((list))->size)
#define list_len(list) (list_hdr((list))->size)
/* gets capacity of list */
#define list_cap(list) (list_hdr((list))->cap)

#endif /* LIST_H_ */

#ifdef LIST_IMPL
#undef LIST_IMPL

list_hdr_t *list_hdr(void *list)
{
	return ((list_hdr_t *)list) - 1;
}

void *list_data(list_hdr_t *hdr)
{
	return &hdr->data[0];
}

/* Makes a list. `count` is initial capacity, `size` is the size
 * of the type. */
UNUSEDA void *list_donotuse_make(size_t count, size_t size)
{
	list_hdr_t *hdr = zalloc((count * size) + sizeof(list_hdr_t));

	hdr->cap = count;
	hdr->size = 0;

	return list_data(hdr);
}

/* Deletes a list. */
UNUSEDA void list_donotuse_delete(void *list)
{
	free(list_hdr(list));
	return;
}

/* Resizes/reserves space of list, using `count` as cap */
UNUSEDA void *list_donotuse_reserve(void *list, size_t count, size_t size)
{
	list_hdr_t *newhdr = (list_hdr_t *)zrealloc(
		list_hdr(list), (count * size) + sizeof(list_hdr_t));

	newhdr->cap = count;

	return list_data(newhdr);
}

/* reserve but only if hdr->size + count > hdr->cap */
/* and reserves with LIST_NEW_SIZE(hdr->size + count) */
UNUSEDA void *list_donotuse_fit(void *list, size_t count, size_t size)
{
	list_hdr_t *hdr = list_hdr(list);
	if(hdr->size + count > hdr->cap) {
		return list_donotuse_reserve(list, LIST_NEW_SIZE(hdr->size + count),
									 size);
	}
	return list;
}

#endif
