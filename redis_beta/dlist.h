/* dlist.c - A simple generic doubly linked list implementation
 * Copyright (C) 2006 Salvatore Sanfilippo <antirez@invece.org>
 * This software is released under the GPL license version 2.0 */

#ifndef __DLIST_H__
#define __DLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

typedef struct listNode {
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

typedef struct list {
    listNode *head;
    listNode *tail;
    void *(*dup)(void *ptr);  // copy the value in the list's node
    void (*free)(void *ptr);  // free the value in the list's node
    int (*match)(void *ptr, void *key);  // match the value in the list's node
    int len;
} list;

typedef struct listIter {
    listNode *next;
    listNode *prev;
    int direction;
} listIter;

/* Functions implemented as macros */
#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

#define listSetDupMethod(l, m) ((l)->dup = (m))
#define listSetFreeMethod(l, m) ((l)->free = (m))
#define listSetMatchMethod(l, m) ((l)->match = (m))

#define listGetDupMethod(l) ((l)->dup)
#define listGetFreeMethod(l) ((l)->free)
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
list *listCreate(void);
void listRelease(list *list);
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
void listDelNode(list *list, listNode *node);
listIter *listGetIterator(list *list, int direction);
void listReleaseIterator(listIter *iter);
listNode *listNextElement(listIter *iter);
listNode *listSearchKey(list *list, void *value);
listNode *listIndex(list *list, int index);

/* Directions for iterators */
#define DL_START_HEAD 0
#define DL_START_TAIL 1

#endif /* __DLIST_H__ */
