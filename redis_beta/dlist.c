/* dlist.c - A generic doubly linked list implementation
 * Copyright (C) 2006-2009 Salvatore Sanfilippo <antirez@invece.org>
 * This software is released under the GPL license version 2.0 */

#include <stdlib.h>
#include "dlist.h"

/* Create a new list. The created list can be freed with listRelease().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
list *listCreate(void)
{
    struct list *list = malloc(sizeof(struct list));
    if (list == NULL) return NULL;
    list->head = list->tail = NULL;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    list->len = 0;
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */
void listRelease(list *list)
{
    listNode *current = list->head;
    int len = list->len;
    while (len--) {
    	listNode *next = current->next;
        if (list->free) list->free(current->value);
        free(current);
        current = next;
    }
    free(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node = malloc(sizeof(struct listNode));
    if (node == NULL) return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node = malloc(sizeof(struct listNode));
    if (node == NULL) return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 *
 * This function can't fail. */
void listDelNode(list *list, listNode *node)
{
    if (node->prev) node->prev->next = node->next;
    else list->head = node->next;
    if (node->next) node->next->prev = node->prev;
    else list->tail = node->prev;
    if (list->free) list->free(node->value);
    free(node);
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNextElement() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter = malloc(sizeof(struct listIter));
    if (iter == NULL) return NULL;
    if (direction == DL_START_HEAD) {
    	iter->next = list->head;
    	iter->prev = NULL;
    } else {
    	iter->next = list->tail;
    	iter->prev = NULL;
    }
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter)
{
    free(iter);
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage
 * patter is:
 *
 * iter = listGetItarotr(list, <direction>);
 * while ((node = listNextElement(iter)) != NULL) {
 *     DoSomethingWith(listNodeValue(node));
 * }
 *
 * */
listNode *listNextElement(listIter *iter)
{
    listNode *current = iter->next;
    if (current != NULL) {
        if (iter->direction == DL_START_HEAD) iter->next = current->next;
        else iter->next = current->prev;
    }
    return current;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
listNode *listSearchKey(list *list, void *value)
{
    listIter *iter = listGetIterator(list, DL_START_HEAD);
    listNode *node;
    while((node = listNextElement(iter)) != NULL) {
        if (list->match) {
            if (list->match(node->value, value)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (value == node->value) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    listReleaseIterator(iter);
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimante
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, int index)
{
    listNode *node;
    if (index < 0) {
        index = (-index) - 1;
        node = list->tail;
        while (index-- && node) node = node->prev;
    } else {
    	node = list->head;
        while (index-- && node) node = node->next;
    }
    return node;
}
