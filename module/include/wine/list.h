/*
 * Copyright (C) 2006  Insigma Co., Ltd
 *
 * This software has been developed while working on the Linux Unified Kernel
 * Project (http://www.longene.org) in the Insigma Research Institute,  
 * which is a subdivision of Insigma Co., Ltd (http://www.insigma.com.cn).
 * 
 * The project is sponsored by Insigma Co., Ltd.
 *
 * The authors can be reached at linux@insigma.com.cn.
 */

#ifndef _UK_LIST_H_
#define _UK_LIST_H_

#include <linux/list.h>

/* add an element after the specified one */
static inline void wine_list_add_after( struct list_head *elem, struct list_head *to_add )
{
#if 0
    to_add->next = elem->next;
    to_add->prev = elem;
    elem->next->prev = to_add;
    elem->next = to_add;
#endif
    list_add(to_add, elem);
}

/* add an element before the specified one */
static inline void wine_list_add_before( struct list_head *elem, struct list_head *to_add )
{
#if 0
    to_add->next = elem;
    to_add->prev = elem->prev;
    elem->prev->next = to_add;
    elem->prev = to_add;
#endif
    list_add_tail(to_add, elem);
}

/* add element at the head of the list */
static inline void wine_list_add_head( struct list_head *list, struct list_head *elem )
{
    wine_list_add_after( list, elem );
}

/* add element at the tail of the list*/ 
static inline void wine_list_add_tail( struct list_head *list, struct list_head *elem )
{
    wine_list_add_before( list, elem );
}

/* remove an element from its list */
static inline void list_remove( struct list_head *elem )
{
    elem->next->prev = elem->prev;
    elem->prev->next = elem->next;
}

/* get the next element */
static inline struct list_head *list_next( const struct list_head *list, const struct list_head *elem )
{
    struct list_head *ret = elem->next;
    if (elem->next == list) ret = NULL;
    return ret;
}

/* get the previous element */
static inline struct list_head *list_prev( const struct list_head *list, const struct list_head *elem )
{
    struct list_head *ret = elem->prev;
    if (elem->prev == list) ret = NULL;
    return ret;
}

/* get the first element */
static inline struct list_head *list_head( const struct list_head *list )
{
    return list_next( list, list );
}

/* get the last element */
static inline struct list_head *list_tail( const struct list_head *list )
{
    return list_prev( list, list );
}

/* check if a list is empty 
static inline int list_empty( const struct list *list )
{
    return list->next == list;
}*/

/* initialize a list */
static inline void list_init( struct list_head *list )
{
    list->next = list->prev = list;
}

/* count the elements of a list */
static inline unsigned int list_count( const struct list_head *list )
{
    unsigned count = 0;
    const struct list_head *ptr;
    for (ptr = list->next; ptr != list; ptr = ptr->next) count++;
    return count;
}

/* move all elements from src to the tail of dst*/
static inline void wine_list_move_tail( struct list_head *dst, struct list_head *src )
{
    if (list_empty(src)) return;

    dst->prev->next = src->next;
    src->next->prev = dst->prev;
    dst->prev = src->prev;
    src->prev->next = dst;
    list_init(src);
}

/* move all elements from src to the head of dst */
static inline void list_move_head( struct list_head *dst, struct list_head *src )
{
    if (list_empty(src)) return;

    dst->next->prev = src->prev;
    src->prev->next = dst->next;
    dst->next = src->next;
    src->next->prev = dst;
    list_init(src);
}

/* iterate through the list */
#define LIST_FOR_EACH(cursor,list) \
    for ((cursor) = (list)->next; (cursor) != (list); (cursor) = (cursor)->next)

/* iterate through the list, with safety against removal */
#define LIST_FOR_EACH_SAFE(cursor, cursor2, list) \
    for ((cursor) = (list)->next, (cursor2) = (cursor)->next; \
         (cursor) != (list); \
         (cursor) = (cursor2), (cursor2) = (cursor)->next)

/* iterate through the list using a list entry */
#define LIST_FOR_EACH_ENTRY(elem, list, type, field) \
    for ((elem) = LIST_ENTRY((list)->next, type, field); \
         &(elem)->field != (list); \
         (elem) = LIST_ENTRY((elem)->field.next, type, field))

/* iterate through the list using a list entry, with safety against removal */
#define LIST_FOR_EACH_ENTRY_SAFE(cursor, cursor2, list, type, field) \
    for ((cursor) = LIST_ENTRY((list)->next, type, field), \
         (cursor2) = LIST_ENTRY((cursor)->field.next, type, field); \
         &(cursor)->field != (list); \
         (cursor) = (cursor2), \
         (cursor2) = LIST_ENTRY((cursor)->field.next, type, field))

/* iterate through the list in reverse order */
#define LIST_FOR_EACH_REV(cursor,list) \
    for ((cursor) = (list)->prev; (cursor) != (list); (cursor) = (cursor)->prev)

/* iterate through the list in reverse order, with safety against removal */
#define LIST_FOR_EACH_SAFE_REV(cursor, cursor2, list) \
    for ((cursor) = (list)->prev, (cursor2) = (cursor)->prev; \
         (cursor) != (list); \
         (cursor) = (cursor2), (cursor2) = (cursor)->prev)

/* iterate through the list in reverse order using a list entry */
#define LIST_FOR_EACH_ENTRY_REV(elem, list, type, field) \
    for ((elem) = LIST_ENTRY((list)->prev, type, field); \
         &(elem)->field != (list); \
         (elem) = LIST_ENTRY((elem)->field.prev, type, field))

/* iterate through the list in reverse order using a list entry, with safety against removal */
#define LIST_FOR_EACH_ENTRY_SAFE_REV(cursor, cursor2, list, type, field) \
    for ((cursor) = LIST_ENTRY((list)->prev, type, field), \
         (cursor2) = LIST_ENTRY((cursor)->field.prev, type, field); \
         &(cursor)->field != (list); \
         (cursor) = (cursor2), \
         (cursor2) = LIST_ENTRY((cursor)->field.prev, type, field))

/* macros for statically initialized lists */
#undef LIST_INIT
#define LIST_INIT(list)  { &(list), &(list) }

/* get pointer to object containing list element */
#undef LIST_ENTRY
#define LIST_ENTRY(elem, type, field) \
    ((type *)((char *)(elem) - offsetof(type, field)))

#endif
