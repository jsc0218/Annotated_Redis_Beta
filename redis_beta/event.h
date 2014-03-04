#ifndef __EVENT_H__
#define __EVENT_H__

struct eEventLoop;

/* Types and data structures */
typedef void eFileProc(struct eEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int eTimeProc(struct eEventLoop *eventLoop, long long id, void *clientData);
typedef void eEventFinalizerProc(struct eEventLoop *eventLoop, void *clientData);

/* File event structure */
typedef struct eFileEvent {
    int fd;
    int mask; /* one of E_(READABLE|WRITABLE|EXCEPTION) */
    eFileProc *fileProc;
    eEventFinalizerProc *finalizerProc;
    void *clientData;
    struct eFileEvent *next;
} eFileEvent;

/* Time event structure */
typedef struct eTimeEvent {
    long long id; /* time event identifier. */
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    eTimeProc *timeProc;
    eEventFinalizerProc *finalizerProc;
    void *clientData;
    struct eTimeEvent *next;
} eTimeEvent;

/* State of an event based program */
typedef struct eEventLoop {
    long long timeEventNextId;
    eFileEvent *fileEventHead;
    eTimeEvent *timeEventHead;
    int stop;
} eEventLoop;

/* Defines */
#define E_OK 0
#define E_ERR -1

#define E_READABLE 1
#define E_WRITABLE 2
#define E_EXCEPTION 4

#define E_FILE_EVENTS 1
#define E_TIME_EVENTS 2
#define E_ALL_EVENTS (E_FILE_EVENTS|E_TIME_EVENTS)
#define E_DONT_WAIT 4

#define E_NOMORE -1

/* Prototypes */
eEventLoop *eCreateEventLoop(void);
void eDeleteEventLoop(eEventLoop *eventLoop);
int eCreateFileEvent(eEventLoop *eventLoop, int fd, int mask, eFileProc *proc,
		void *clientData, eEventFinalizerProc *finalizerProc);
void eDeleteFileEvent(eEventLoop *eventLoop, int fd, int mask);
long long eCreateTimeEvent(eEventLoop *eventLoop, long long milliseconds,
        eTimeProc *proc, void *clientData, eEventFinalizerProc *finalizerProc);
int eDeleteTimeEvent(eEventLoop *eventLoop, long long id);
int eProcessEvents(eEventLoop *eventLoop, int flags);
void eMain(eEventLoop *eventLoop);

#endif
