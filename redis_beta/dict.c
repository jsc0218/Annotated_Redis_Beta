/* Hash Tables Implementation - Copyright (C) 2006-2009 Salvatore Sanfilippo
 * antirez at gmail dot com
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "dict.h"

/* ---------------------------- Utility functions --------------------------- */

static void _dictPanic(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\nDICT LIBRARY PANIC: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n\n");
    va_end(ap);
}

/* ------------------------- Heap Management Wrappers------------------------ */

static void *_dictAlloc(int size)
{
    void *p = malloc(size);
    if (p == NULL) _dictPanic("Out of memory");
    return p;
}

static void _dictFree(void *ptr)
{
    free(ptr);
}

/* -------------------------- private prototypes ---------------------------- */

/* Reset an hashtable already initialized with ht_init(). */
static void _dictReset(dict *ht)
{
    ht->table = NULL;
    ht->size = ht->sizemask = ht->used = 0;
}

/* Initialize the hash table */
static int _dictInit(dict *ht, dictType *type, void *privDataPtr)
{
    _dictReset(ht);
    ht->type = type;
    ht->privdata = privDataPtr;
    return DICT_OK;
}

/* Destroy an entire hash table */
static int _dictClear(dict *ht)
{
    /* Free all the elements */
    for (unsigned int i = 0; i < ht->size && ht->used > 0; i++) {
        if (ht->table[i] == NULL) continue;
        dictEntry *he = ht->table[i];
        while (he) {
        	dictEntry *next = he->next;
            dictFreeEntryKey(ht, he);
            dictFreeEntryVal(ht, he);
            _dictFree(he);
            ht->used--;
            he = next;
        }
    }
    /* Free the table and the allocated cache structure */
    _dictFree(ht->table);
    /* Re-initialize the table */
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(dict *ht)
{
    /* If the hash table is empty expand it to the initial size,
     * if the table is "full" double its size. */
    if (ht->size == 0) return dictExpand(ht, DICT_HT_INITIAL_SIZE);
    if (ht->used == ht->size) return dictExpand(ht, ht->size * 2);
    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned int _dictNextPower(unsigned int size)
{
    if (size >= 2147483648U) return 2147483648U;
    unsigned int i = DICT_HT_INITIAL_SIZE;
    while (1) {
        if (i >= size) return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * an hash entry for the given 'key'.
 * If the key already exists, -1 is returned. */
static int _dictKeyIndex(dict *ht, const void *key)
{
    /* Expand the hashtable if needed */
    if (_dictExpandIfNeeded(ht) == DICT_ERR) return -1;
    /* Compute the key hash value */
    unsigned int h = dictHashKey(ht, key) & ht->sizemask;
    /* Search if this slot does not already contain the given key */
    dictEntry *he = ht->table[h];
    while (he) {
        if (dictCompareHashKeys(ht, key, he->key)) return -1;
        he = he->next;
    }
    return h;
}

/* Search and remove an element */
static int _dictGenericDelete(dict *ht, const void *key, int nofree)
{
    if (ht->size == 0) return DICT_ERR;
    unsigned int h = dictHashKey(ht, key) & ht->sizemask;
    dictEntry *he = ht->table[h];
    dictEntry *prevHe = NULL;
    while (he) {
        if (dictCompareHashKeys(ht, key, he->key)) {
            /* Unlink the element from the list */
            if (prevHe) prevHe->next = he->next;
            else ht->table[h] = he->next;
            if (!nofree) {
                dictFreeEntryKey(ht, he);
                dictFreeEntryVal(ht, he);
            }
            _dictFree(he);
            ht->used--;
            return DICT_OK;
        }
        prevHe = he;
        he = he->next;
    }
    return DICT_ERR; /* not found */
}

/* ----------------------------- API implementation ------------------------- */

/* Create a new hash table */
dict *dictCreate(dictType *type, void *privDataPtr)
{
    dict *ht = _dictAlloc(sizeof(struct dict));
    _dictInit(ht, type, privDataPtr);
    return ht;
}

/* Clear & Release the hash table */
void dictRelease(dict *ht)
{
    _dictClear(ht);
    _dictFree(ht);
}

/* Expand or create the hashtable */
int dictExpand(dict *ht, unsigned int size)
{
    /* the size is invalid if it is smaller than the number of
     * elements already inside the hashtable */
    if (ht->used > size) return DICT_ERR;

    dict newht; /* the new hashtable */
    _dictInit(&newht, ht->type, ht->privdata);
    unsigned int realsize = _dictNextPower(size);
    newht.size = realsize;
    newht.sizemask = realsize - 1;
    newht.table = _dictAlloc(realsize * sizeof(dictEntry *));
    /* Initialize all the pointers to NULL */
    memset(newht.table, 0, realsize * sizeof(dictEntry *));

    /* Copy all the elements from the old to the new table:
     * note that if the old hash table is empty ht->size is zero,
     * so dictExpand just creates an hash table. */
    newht.used = ht->used;
    for (unsigned int i = 0; i < ht->size && ht->used > 0; i++) {
        if (ht->table[i] == NULL) continue;
        /* For each hash entry on this slot... */
        dictEntry *he = ht->table[i];
        while (he) {
        	dictEntry *next = he->next;
            /* Get the new element index */
            unsigned int h = dictHashKey(ht, he->key) & newht.sizemask;
            he->next = newht.table[h];
            newht.table[h] = he;
            ht->used--;
            /* Pass to the next element */
            he = next;
        }
    }
    assert(ht->used == 0);
    _dictFree(ht->table);

    /* Remap the new hashtable in the old */
    *ht = newht;
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USER/BUCKETS ration near to <= 1 */
int dictResize(dict *ht)
{
    int minimal = ht->used;
    if (minimal < DICT_HT_INITIAL_SIZE) minimal = DICT_HT_INITIAL_SIZE;
    return dictExpand(ht, minimal);
}

/* Add an element to the target hash table */
int dictAdd(dict *ht, void *key, void *val)
{
    /* Get the index of the new element, or -1 if
     * the element already exists. */
	int index = _dictKeyIndex(ht, key);
    if (index == -1) return DICT_ERR;
    /* Allocates the memory and stores key */
    dictEntry *entry = _dictAlloc(sizeof(struct dictEntry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    /* Set the hash entry fields. */
    dictSetHashKey(ht, entry, key);
    dictSetHashVal(ht, entry, val);
    ht->used++;
    return DICT_OK;
}

/* Add an element, discarding the old if the key already exists */
int dictReplace(dict *ht, void *key, void *val)
{
    /* Try to add the element. If the key
     * does not exists dictAdd will succeed. */
    if (dictAdd(ht, key, val) == DICT_OK) return DICT_OK;
    /* It already exists, get the entry */
    dictEntry *entry = dictFind(ht, key);
    /* Free the old value and set the new one */
    dictFreeEntryVal(ht, entry);
    dictSetHashVal(ht, entry, val);
    return DICT_OK;
}

int dictDelete(dict *ht, const void *key)
{
    return _dictGenericDelete(ht, key, 0);
}

int dictDeleteNoFree(dict *ht, const void *key)
{
    return _dictGenericDelete(ht, key, 1);
}

dictEntry *dictFind(dict *ht, const void *key)
{
    if (ht->size == 0) return NULL;
    unsigned int h = dictHashKey(ht, key) & ht->sizemask;
    dictEntry *he = ht->table[h];
    while (he) {
        if (dictCompareHashKeys(ht, key, he->key)) return he;
        he = he->next;
    }
    return NULL;
}

dictIterator *dictGetIterator(dict *ht)
{
    dictIterator *iter = _dictAlloc(sizeof(struct dictIterator));
    iter->ht = ht;
    iter->index = -1;
    iter->entry = iter->next = NULL;
    return iter;
}

dictEntry *dictNext(dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {
            iter->index++;
            if (iter->index >= (signed)iter->ht->size) break;
            iter->entry = iter->ht->table[iter->index];
        } else {
            iter->entry = iter->next;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->next = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
    _dictFree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
dictEntry *dictGetRandomEntry(dict *ht)
{
    if (ht->size == 0) return NULL;
    dictEntry *he;
    unsigned int h;
    do {
        h = random() & ht->sizemask;
        he = ht->table[h];
    } while (he == NULL);

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is to count the element and
     * select a random index. */
    int listlen = 0;
    while (he) {
        he = he->next;
        listlen++;
    }
    int listele = random() % listlen;
    he = ht->table[h];
    while (listele--) he = he->next;
    return he;
}

#define DICT_STATS_VECTLEN 50

void dictPrintStats(dict *ht) {
    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    unsigned int stats[DICT_STATS_VECTLEN];
    for (unsigned int i = 0; i < DICT_STATS_VECTLEN; i++) stats[i] = 0;

    unsigned int slots = 0, maxchainlen = 0, totchainlen = 0;
    for (unsigned int i = 0; i < ht->size; i++) {
        if (ht->table[i] == NULL) {
            stats[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        unsigned int chainlen = 0;
        dictEntry *he = ht->table[i];
        while (he) {
            chainlen++;
            he = he->next;
        }
        stats[(chainlen < DICT_STATS_VECTLEN) ? chainlen : DICT_STATS_VECTLEN-1]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %d\n", ht->size);
    printf(" number of elements: %d\n", ht->used);
    printf(" different slots: %d\n", slots);
    printf(" max chain length: %d\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (unsigned int i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (stats[i] == 0) continue;
        printf("   %s%d: %d (%.02f%%)\n", (i == DICT_STATS_VECTLEN-1) ? ">= " : "",
        		i, stats[i], ((float)stats[i]/ht->size)*100);
    }
}

/* -------------------------- hash functions -------------------------------- */

/* Generic hash function (a popular one from Bernstein).
 * I tested a few and this was the best. */
unsigned int dictGenHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = 5381;
    while (len--) hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
    return hash;
}
