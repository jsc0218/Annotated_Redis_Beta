/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyrights (C) 2007-2009 Salvatore Sanfilippo antirez at gmail dot com
 * All Rights Reserved
 *
 * This software is released under the GPL version 2 license */

#include "event.h"
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

eEventLoop *eCreateEventLoop(void)
{
    eEventLoop *eventLoop = malloc(sizeof(struct eEventLoop));
    if (!eventLoop) return NULL;
    eventLoop->fileEventHead = NULL;
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;
    eventLoop->stop = 0;
    return eventLoop;
}

void eDeleteEventLoop(eEventLoop *eventLoop)
{
    free(eventLoop);
}

int eCreateFileEvent(eEventLoop *eventLoop, int fd, int mask, eFileProc *proc,
		void *clientData, eEventFinalizerProc *finalizerProc)
{
    eFileEvent *fe = malloc(sizeof(struct eFileEvent));
    if (fe == NULL) return E_ERR;
    fe->fd = fd;
    fe->mask = mask;
    fe->fileProc = proc;
    fe->finalizerProc = finalizerProc;
    fe->clientData = clientData;
    fe->next = eventLoop->fileEventHead;
    eventLoop->fileEventHead = fe;
    return E_OK;
}

void eDeleteFileEvent(eEventLoop *eventLoop, int fd, int mask)
{
    eFileEvent *fe = eventLoop->fileEventHead, *prev = NULL;
    while (fe) {
        if (fe->fd==fd && fe->mask==mask) {
            if (prev == NULL) eventLoop->fileEventHead = fe->next;
            else prev->next = fe->next;
            if (fe->finalizerProc) fe->finalizerProc(eventLoop, fe->clientData);
            free(fe);
            return;
        }
        prev = fe;
        fe = fe->next;
    }
}

static void eGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void eAddMillisecondsToNow(long long milliseconds, long *sec, long *ms)
{
    long cur_sec, cur_ms;
    eGetTime(&cur_sec, &cur_ms);
    long when_sec = cur_sec + milliseconds/1000;
    long when_ms = cur_ms + milliseconds%1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

long long eCreateTimeEvent(eEventLoop *eventLoop, long long milliseconds,
        eTimeProc *proc, void *clientData, eEventFinalizerProc *finalizerProc)
{
    eTimeEvent *te = malloc(sizeof(struct eTimeEvent));
    if (te == NULL) return E_ERR;
    te->id = eventLoop->timeEventNextId++;
    eAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->next = eventLoop->timeEventHead;
    eventLoop->timeEventHead = te;
    return te->id;
}

int eDeleteTimeEvent(eEventLoop *eventLoop, long long id)
{
    eTimeEvent *te = eventLoop->timeEventHead, *prev = NULL;
    while (te) {
        if (te->id == id) {
            if (prev == NULL) eventLoop->timeEventHead = te->next;
            else prev->next = te->next;
            if (te->finalizerProc) te->finalizerProc(eventLoop, te->clientData);
            free(te);
            return E_OK;
        }
        prev = te;
        te = te->next;
    }
    return E_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted. */
static eTimeEvent *eSearchNearestTimer(eEventLoop *eventLoop)
{
    eTimeEvent *te = eventLoop->timeEventHead;
    eTimeEvent *nearest = NULL;
    while (te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
        	(te->when_sec == nearest->when_sec && te->when_ms < nearest->when_ms))
        	nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int eProcessEvents(eEventLoop *eventLoop, int flags)
{
	/* Nothing to do? return ASAP */
	if (!(flags & E_TIME_EVENTS) && !(flags & E_FILE_EVENTS)) return 0;

    int maxfd = 0, numfd = 0;
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    /* Check file events */
    if (flags & E_FILE_EVENTS) {
    	eFileEvent *fe = eventLoop->fileEventHead;
        while (fe != NULL) {
            if (fe->mask & E_READABLE) FD_SET(fe->fd, &rfds);
            if (fe->mask & E_WRITABLE) FD_SET(fe->fd, &wfds);
            if (fe->mask & E_EXCEPTION) FD_SET(fe->fd, &efds);
            if (maxfd < fe->fd) maxfd = fe->fd;
            numfd++;
            fe = fe->next;
        }
    }

    int processed = 0;
    /* Note that we want call select() even if there are no file
     * events to process as long as we want to process time events,
     * in order to sleep until the next time event is ready to fire. */
    if (numfd || ((flags & E_TIME_EVENTS) && !(flags & E_DONT_WAIT))) {
        eTimeEvent *shortest = NULL;
        if ((flags & E_TIME_EVENTS) && !(flags & E_DONT_WAIT))
            shortest = eSearchNearestTimer(eventLoop);
        struct timeval tv, *tvp;
        if (shortest) {
            long now_sec, now_ms;
            /* Calculate the time missing for the nearest timer to fire. */
            eGetTime(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms) * 1000;
                tvp->tv_sec--;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
            }
        } else {
            /* If we have to check for events but need to return ASAP
             * because of E_DONT_WAIT we need to set the timeout to zero */
            if (flags & E_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block, no time events and wait */
                tvp = NULL; /* wait forever */
            }
        }

        int retval = select(maxfd+1, &rfds, &wfds, &efds, tvp);
        if (retval > 0) {
        	eFileEvent *fe = eventLoop->fileEventHead;
            while (fe != NULL) {
                int fd = (int) fe->fd;
                if ((fe->mask & E_READABLE && FD_ISSET(fd, &rfds)) ||
                    (fe->mask & E_WRITABLE && FD_ISSET(fd, &wfds)) ||
                    (fe->mask & E_EXCEPTION && FD_ISSET(fd, &efds))) {
                    int mask = 0;
                    if (fe->mask & E_READABLE && FD_ISSET(fd, &rfds)) mask |= E_READABLE;
                    if (fe->mask & E_WRITABLE && FD_ISSET(fd, &wfds)) mask |= E_WRITABLE;
                    if (fe->mask & E_EXCEPTION && FD_ISSET(fd, &efds)) mask |= E_EXCEPTION;
                    fe->fileProc(eventLoop, fe->fd, fe->clientData, mask);
                    processed++;
                    /* After an event is processed our file event list
                     * may no longer be the same, so what we do
                     * is to clear the bit for this file descriptor and
                     * restart again from the head. */
                    fe = eventLoop->fileEventHead;
                    FD_CLR(fd, &rfds);
                    FD_CLR(fd, &wfds);
                    FD_CLR(fd, &efds);
                } else {
                    fe = fe->next;
                }
            }
        }
    }
    /* Check time events */
    if (flags & E_TIME_EVENTS) {
    	eTimeEvent *te = eventLoop->timeEventHead;
    	long long maxId = eventLoop->timeEventNextId - 1;
        while (te) {
            if (te->id > maxId) {
                te = te->next;
                continue;
            }
            long now_sec, now_ms;
            eGetTime(&now_sec, &now_ms);
            if (now_sec > te->when_sec || (now_sec == te->when_sec && now_ms >= te->when_ms)) {
                long long id = te->id;
                int retval = te->timeProc(eventLoop, id, te->clientData);
                /* After an event is processed our time event list may
                 * no longer be the same, so we restart from head.
                 * Still we make sure to don't process events registered
                 * by event handlers itself in order to don't loop forever.
                 * To do so we saved the max ID we want to handle. */
                if (retval == E_NOMORE) eDeleteTimeEvent(eventLoop, id);
                else eAddMillisecondsToNow(retval, &te->when_sec, &te->when_ms);
                te = eventLoop->timeEventHead;
            } else {
                te = te->next;
            }
        }
    }
    return processed; /* return the number of processed file/time events */
}

void eMain(eEventLoop *eventLoop)
{
    eventLoop->stop = 0;
    while (!eventLoop->stop) eProcessEvents(eventLoop, E_ALL_EVENTS);
}
