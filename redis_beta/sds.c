/* SDSLib, A C dynamic strings library
 *
 * Copyright (C) 2006-2009 Salvatore Sanfilippo, antirez@gmail.com
 * This softare is released under the following BSD license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sds.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

static void sdsOomAbort(void)
{
    fprintf(stderr, "SDS: Out Of Memory (SDS_ABORT_ON_OOM defined)\n");
    abort();
}

sds sdsnewlen(const void *init, size_t initlen)
{
    struct sdshdr *sh = malloc(sizeof(struct sdshdr) + initlen + 1);
#ifdef SDS_ABORT_ON_OOM
    if (sh == NULL) sdsOomAbort();
#else
    if (sh == NULL) return NULL;
#endif
    sh->len = initlen;
    sh->free = 0;
    if (initlen > 0) {
        if (init) memcpy(sh->buf, init, initlen);
        else memset(sh->buf, 0, initlen);
    }
    sh->buf[initlen] = '\0';
    return (char *)sh->buf;
}

sds sdsempty(void)
{
    return sdsnewlen("", 0);
}

sds sdsnew(const char *init)
{
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

size_t sdslen(const sds s)
{
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    return sh->len;
}

void sdsfree(sds s)
{
    if (s == NULL) return;
    free(s - sizeof(struct sdshdr));
}

size_t sdsavail(sds s)
{
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    return sh->free;
}

void sdsupdatelen(sds s)
{
    int reallen = strlen(s);
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    sh->free += (sh->len - reallen);
    sh->len = reallen;
}

static sds sdsMakeRoomFor(sds s, size_t addlen)
{
    size_t free = sdsavail(s);
    if (free >= addlen) return s;
    size_t len = sdslen(s);
    size_t newlen = (len + addlen) * 2;
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    struct sdshdr *newsh = realloc(sh, sizeof(struct sdshdr) + newlen + 1);
#ifdef SDS_ABORT_ON_OOM
    if (newsh == NULL) sdsOomAbort();
#else
    if (newsh == NULL) return NULL;
#endif
    newsh->free = newlen - len;
    return newsh->buf;
}

sds sdscatlen(sds s, void *t, size_t len)
{
    size_t curlen = sdslen(s);
    s = sdsMakeRoomFor(s, len);
    if (s == NULL) return NULL;
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    memcpy(s + curlen, t, len);
    sh->len = curlen + len;
    sh->free = sh->free - len;
    s[curlen + len] = '\0';
    return s;
}

sds sdscat(sds s, char *t)
{
    return sdscatlen(s, t, strlen(t));
}

sds sdscatprintf(sds s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *buf;
    size_t buflen = 32;
    while (1) {
        buf = malloc(buflen);
#ifdef SDS_ABORT_ON_OOM
        if (buf == NULL) sdsOomAbort();
#else
        if (buf == NULL) return NULL;
#endif
        buf[buflen-2] = '\0';
        vsnprintf(buf, buflen, fmt, ap);
        if (buf[buflen-2] != '\0') {
            free(buf);
            buflen *= 2;
            continue;
        }
        break;
    }
    va_end(ap);
    sds res = sdscat(s, buf);
    free(buf);
    return res;
}

sds sdstrim(sds s, const char *cset)
{
    char *start, *end, *sp, *ep;
    sp = start = s;
    ep = end = s + sdslen(s) - 1;
    while (sp<=end && strchr(cset, *sp)) sp++;
    while (ep>=start && strchr(cset, *ep)) ep--;
    size_t len = (sp > ep) ? 0 : (ep-sp+1);
    struct sdshdr *sh = (void*) (s - sizeof(struct sdshdr));
    if (sh->buf != sp) memmove(sh->buf, sp, len);
    sh->buf[len] = '\0';
    sh->free = sh->free + (sh->len - len);
    sh->len = len;
    return s;
}

sds sdsrange(sds s, long start, long end)
{
    size_t len = sdslen(s);
    if (len == 0) return s;
    if (start < 0) {
        start += len;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end += len;
        if (end < 0) end = 0;
    }
    size_t newlen = (start > end) ? 0 : end-start+1;
    if (newlen != 0) {
        if (start >= (signed)len) start = len - 1;
        if (end >= (signed)len) end = len - 1;
        newlen = (start > end) ? 0 : end-start+1;
    } else {
        start = 0;
    }
    struct sdshdr *sh = (void *) (s - sizeof(struct sdshdr));
    if (start != 0) memmove(sh->buf, sh->buf+start, newlen);
    sh->buf[newlen] = '\0';
    sh->free = sh->free + (sh->len - newlen);
    sh->len = newlen;
    return s;
}

void sdstolower(sds s)
{
    int len = sdslen(s);
    for (int j = 0; j < len; j++) s[j] = tolower(s[j]);
}

int sdscmp(sds s1, sds s2)
{
    size_t l1 = sdslen(s1);
    size_t l2 = sdslen(s2);
    size_t minlen = (l1 < l2) ? l1 : l2;
    int cmp = memcmp(s1, s2, minlen);
    if (cmp == 0) return l1 - l2;
    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */
sds *sdssplitlen(char *s, int len, char *sep, int seplen, int *count)
{
    int elements = 0, slots = 5, start = 0;
    sds *tokens = malloc(sizeof(sds) * slots);
#ifdef SDS_ABORT_ON_OOM
    if (tokens == NULL) sdsOomAbort();
#endif
    if (seplen < 1 || len < 0 || tokens == NULL) return NULL;
    for (int j = 0; j < len-(seplen-1); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {
            slots *= 2;
            sds *newtokens = realloc(tokens, sizeof(sds)*slots);
            if (newtokens == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            tokens = newtokens;
        }
        /* search the separator */
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j, sep, seplen) == 0)) {
            tokens[elements] = sdsnewlen(s+start, j-start);
            if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            elements++;
            start = j + seplen;
            j = start - 1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start, len-start);
    if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
    }
    *count = ++elements;
    return tokens;

#ifndef SDS_ABORT_ON_OOM
cleanup:
    {
        for (int i = 0; i < elements; i++) sdsfree(tokens[i]);
        free(tokens);
        return NULL;
    }
#endif
}
