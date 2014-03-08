/* Redis - REmote DIctionary Server
 * Copyright (C) 2008 Salvatore Sanfilippo antirez at gmail dot com
 * All Rights Reserved. Under the GPL version 2. See the COPYING file. */

#include "redis.h"

/*================= Prototypes =================== */
static void addReplySds(redisClient *client, sds s);

static void pingCommand(redisClient *client);
static void echoCommand(redisClient *client);
static void setCommand(redisClient *client);
static void setnxCommand(redisClient *client);
static void getCommand(redisClient *client);
static void delCommand(redisClient *client);
static void existsCommand(redisClient *client);
static void incrCommand(redisClient *client);
static void decrCommand(redisClient *client);
static void selectCommand(redisClient *client);
static void randomkeyCommand(redisClient *client);
static void keysCommand(redisClient *client);
static void dbsizeCommand(redisClient *client);
static void lastsaveCommand(redisClient *client);
static void saveCommand(redisClient *client);
static void bgsaveCommand(redisClient *client);
static void shutdownCommand(redisClient *client);
static void moveCommand(redisClient *client);
static void renameCommand(redisClient *client);
static void renamenxCommand(redisClient *client);
static void lpushCommand(redisClient *client);
static void rpushCommand(redisClient *client);
static void lpopCommand(redisClient *client);
static void rpopCommand(redisClient *client);
static void llenCommand(redisClient *client);
static void lindexCommand(redisClient *client);
static void lrangeCommand(redisClient *client);
static void ltrimCommand(redisClient *client);

/*=============================== Globals ============================ */
/* Global vars */
static struct redisServer server; /* server global state */
static struct redisCommand cmdTable[] = {
    {"get",getCommand,2,REDIS_CMD_INLINE},
    {"set",setCommand,3,REDIS_CMD_BULK},
    {"setnx",setnxCommand,3,REDIS_CMD_BULK},
    {"del",delCommand,2,REDIS_CMD_INLINE},
    {"exists",existsCommand,2,REDIS_CMD_INLINE},
    {"incr",incrCommand,2,REDIS_CMD_INLINE},
    {"decr",decrCommand,2,REDIS_CMD_INLINE},
    {"rpush",rpushCommand,3,REDIS_CMD_BULK},
    {"lpush",lpushCommand,3,REDIS_CMD_BULK},
    {"rpop",rpopCommand,2,REDIS_CMD_INLINE},
    {"lpop",lpopCommand,2,REDIS_CMD_INLINE},
    {"llen",llenCommand,2,REDIS_CMD_INLINE},
    {"lindex",lindexCommand,3,REDIS_CMD_INLINE},
    {"lrange",lrangeCommand,4,REDIS_CMD_INLINE},
    {"ltrim",ltrimCommand,4,REDIS_CMD_INLINE},
    {"randomkey",randomkeyCommand,1,REDIS_CMD_INLINE},
    {"select",selectCommand,2,REDIS_CMD_INLINE},
    {"move",moveCommand,3,REDIS_CMD_INLINE},
    {"rename",renameCommand,3,REDIS_CMD_INLINE},
    {"renamenx",renamenxCommand,3,REDIS_CMD_INLINE},
    {"keys",keysCommand,2,REDIS_CMD_INLINE},
    {"dbsize",dbsizeCommand,1,REDIS_CMD_INLINE},
    {"ping", pingCommand, 1, REDIS_CMD_INLINE},
    {"echo",echoCommand,2,REDIS_CMD_BULK},
    {"save",saveCommand,1,REDIS_CMD_INLINE},
    {"bgsave",bgsaveCommand,1,REDIS_CMD_INLINE},
    {"shutdown",shutdownCommand,1,REDIS_CMD_INLINE},
    {"lastsave",lastsaveCommand,1,REDIS_CMD_INLINE},
    /* lpop, rpop, lindex, llen */
    /* dirty, lastsave, info */
    {"",NULL,0,0}
};

/* ========================= Random utility functions ====================== */
/* Redis generally does not try to recover from out of memory conditions
 * when allocating objects or strings, it is not clear if it will be possible
 * to report this condition to the client since the networking layer itself
 * is based on heap allocation for send buffers, so we simply abort.
 * At least the code will be simpler to read... */
static void oom(const char *msg)
{
    fprintf(stderr, "%s: Out of memory\n",msg);
    fflush(stderr);
    sleep(1);
    abort();
}

/* ======================= Redis objects implementation ===================== */
static void freeStringObject(redisObject *obj)
{
    sdsfree(obj->ptr);
}

static void freeListObject(redisObject *obj)
{
    listRelease((list*) obj->ptr);
}

static void freeSetObject(redisObject *obj)
{
    /* TODO */
	obj = obj;
}

static void incrRefCount(redisObject *obj)
{
	obj->refcount++;
}

static void decrRefCount(void *obj)
{
    redisObject *o = obj;
    if (--(o->refcount) == 0) {
        switch(o->type) {
        case REDIS_STRING: freeStringObject(o); break;
        case REDIS_LIST: freeListObject(o); break;
        case REDIS_SET: freeSetObject(o); break;
        default: assert(0 != 0); break;
        }
        if (!listAddNodeHead(server.objfreelist, o)) free(o);
    }
}

static redisObject *createObject(int type, void *ptr)
{
    redisObject *obj;
    if (listLength(server.objfreelist)) {
        listNode *head = listFirst(server.objfreelist);
        obj = listNodeValue(head);
        listDelNode(server.objfreelist, head);
    } else {
        obj = malloc(sizeof(struct redisObject));
    }
    if (!obj) oom("createObject");
    obj->type = type;
    obj->ptr = ptr;
    obj->refcount = 1;
    return obj;
}

static redisObject *createListObject(void)
{
    list *l = listCreate();
    if (!l) oom("createListObject");
    listSetFreeMethod(l, decrRefCount);
    return createObject(REDIS_LIST, l);
}

/*============================ Utility functions ========================= */
/* Glob-style pattern matching. */
int stringmatchlen(const char *pattern, int patternLen, const char *string, int stringLen, int nocase) {
    while (patternLen) {
        switch (pattern[0]) {
        case '*':
            while (pattern[1] == '*') {
                pattern++;
                patternLen--;
            }
            if (patternLen == 1) return 1; /* match */
            while (stringLen) {
                if (stringmatchlen(pattern+1, patternLen-1, string, stringLen, nocase)) return 1; /* match */
                string++;
                stringLen--;
            }
            return 0; /* no match */
            break;
        case '?':
            if (stringLen == 0) return 0; /* no match */
            string++;
            stringLen--;
            break;
        case '[':
        {
            pattern++;
            patternLen--;
            int not = pattern[0] == '^';
            if (not) {
                pattern++;
                patternLen--;
            }
            int match = 0;
            while (1) {
                if (pattern[0] == '\\') {
                    pattern++;
                    patternLen--;
                    if (pattern[0] == string[0]) match = 1;
                } else if (pattern[0] == ']') {
                    break;
                } else if (patternLen == 0) {
                    pattern--;
                    patternLen++;
                    break;
                } else if (pattern[1] == '-' && patternLen >= 3) {
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if (start > end) {
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if (nocase) {
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2;
                    patternLen -= 2;
                    if (c >= start && c <= end) match = 1;
                } else {
                    if (!nocase) {
                        if (pattern[0] == string[0]) match = 1;
                    } else {
                        if (tolower((int)pattern[0]) == tolower((int)string[0])) match = 1;
                    }
                }
                pattern++;
                patternLen--;
            }
            if (not) match = !match;
            if (!match) return 0; /* no match */
            string++;
            stringLen--;
            break;
        }
        case '\\':
            if (patternLen >= 2) {
                pattern++;
                patternLen--;
            }
            /* fall through */
        default:
            if (!nocase) {
                if (pattern[0] != string[0]) return 0; /* no match */
            } else {
                if (tolower((int)pattern[0]) != tolower((int)string[0])) return 0; /* no match */
            }
            string++;
            stringLen--;
            break;
        }
        pattern++;
        patternLen--;
        if (stringLen == 0) {
            while(*pattern == '*') {
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && stringLen == 0) return 1;
    return 0;
}

static void redisLog(int level, const char *fmt, ...)
{
    FILE *fp = (server.logfile == NULL) ? stdout : fopen(server.logfile, "a");
    if (!fp) return;
    va_list ap;
    va_start(ap, fmt);
    if (level >= server.verbosity) {
        char *c = ".-*";
        fprintf(fp, "%c " ,c[level]);
        vfprintf(fp, fmt, ap);
        fprintf(fp, "\n");
        fflush(fp);
    }
    va_end(ap);
    if (server.logfile) fclose(fp);
}

/*====================== Hash table type implementation  ==================== */
/* This is an hash table type that uses the SDS dynamic strings library as
 * keys and redis objects as values (objects can hold SDS strings,
 * lists, sets). */
static unsigned int sdsDictHashFunction(const void *key)
{
    return dictGenHashFunction(key, sdslen((sds)key));
}

static int sdsDictKeyCompare(void *privdata, const void *key1, const void *key2)
{
    DICT_NOTUSED(privdata);
    int l1 = sdslen((sds)key1);
    int l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

static void sdsDictKeyDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    sdsfree(val);
}

static void sdsDictValDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    decrRefCount(val);
}

dictType sdsDictType = {
    sdsDictHashFunction,       /* hash function */
    NULL,                               /* key dup */
    NULL,                               /* val dup */
    sdsDictKeyCompare,         /* key compare */
    sdsDictKeyDestructor,      /* key destructor */
    sdsDictValDestructor,       /* val destructor */
};

/*============================ DB saving/loading ============================ */
/* Save the DB on disk. Return REDIS_ERR on error, REDIS_OK on success */
static int saveDb(char *filename)
{
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "temp-%d.%ld.rdb", (int)time(NULL), (long int)random());
    FILE *fp = fopen(tmpfile, "w");
    if (!fp) {
        redisLog(REDIS_WARNING, "Failed saving the DB: %s", strerror(errno));
        return REDIS_ERR;
    }
    if (fwrite("REDIS0000", 9, 1, fp) == 0) goto error;
    dictIterator *di = NULL;
    for (int j = 0; j < server.dbnum; j++) {
        dict *dict = server.dict[j];
        if (dictGetHashTableUsed(dict) == 0) continue;
        di = dictGetIterator(dict);
        if (!di) {
            fclose(fp);
            return REDIS_ERR;
        }

        /* Write the SELECT DB opcode */
        uint8_t type = REDIS_SELECTDB;
        uint32_t len = htonl(j);
        if (fwrite(&type, 1, 1, fp) == 0) goto error;
        if (fwrite(&len, 4, 1, fp) == 0) goto error;

        /* Iterate this DB writing every entry */
        dictEntry *de;
        while ((de = dictNext(di)) != NULL) {
            sds key = dictGetEntryKey(de);
            redisObject *obj = dictGetEntryVal(de);
            type = obj->type;
            len = htonl(sdslen(key));
            if (fwrite(&type, 1, 1, fp) == 0) goto error;
            if (fwrite(&len, 4, 1, fp) == 0) goto error;
            if (fwrite(key, sdslen(key), 1, fp) == 0) goto error;
            if (type == REDIS_STRING) {
                /* Save a string value */
                sds val = obj->ptr;
                len = htonl(sdslen(val));
                if (fwrite(&len, 4, 1, fp) == 0) goto error;
                if (fwrite(val, sdslen(val), 1, fp) == 0) goto error;
            } else if (type == REDIS_LIST) {
                /* Save a list value */
                list *list = obj->ptr;
                listNode *node = list->head;
                len = htonl(listLength(list));
                if (fwrite(&len, 4, 1, fp) == 0) goto error;
                while (node) {
                    redisObject *elem = listNodeValue(node);
                    len = htonl(sdslen(elem->ptr));
                    if (fwrite(&len, 4, 1 ,fp) == 0) goto error;
                    if (fwrite(elem->ptr, sdslen(elem->ptr), 1, fp) == 0) goto error;
                    node = node->next;
                }
            } else {
                assert(0 != 0);
            }
        }
        dictReleaseIterator(di);
    }
    /* EOF opcode */
    uint8_t type = REDIS_EOF;
    if (fwrite(&type, 1, 1, fp) == 0) goto error;
    fclose(fp);

    /* Use RENAME to make sure the DB file is changed atomically
     * only if the generate DB file is ok. */
    if (rename(tmpfile, filename) == -1) {
        redisLog(REDIS_WARNING, "Error moving temp DB file on the final destionation: %s", strerror(errno));
        unlink(tmpfile);
        return REDIS_ERR;
    }
    redisLog(REDIS_NOTICE, "DB saved on disk");
    server.dirty = 0;
    server.lastsave = time(NULL);
    return REDIS_OK;

error:
    fclose(fp);
    redisLog(REDIS_WARNING, "Error saving DB on disk: %s", strerror(errno));
    if (di) dictReleaseIterator(di);
    return REDIS_ERR;
}

static int saveDbBackground(char *filename)
{
    if (server.bgsaveinprogress) return REDIS_ERR;
    pid_t childpid = fork();
    if (childpid == 0) {
        /* Child */
        close(server.fd);
        if (saveDb(filename) == REDIS_OK) exit(0);
        else exit(1);
    } else {
        /* Parent */
        redisLog(REDIS_NOTICE, "Background saving started by pid %d", childpid);
        server.bgsaveinprogress = 1;
        return REDIS_OK;
    }
    return REDIS_OK;  /* unreached */
}

static int loadDb(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return REDIS_ERR;
    char buf[REDIS_LOADBUF_LEN];    /* Try to use this buffer instead */
    if (fread(buf, 9, 1, fp) == 0) goto error;
    if (memcmp(buf, "REDIS0000", 9) != 0) {
        fclose(fp);
        redisLog(REDIS_WARNING, "Wrong signature trying to load DB from file");
        return REDIS_ERR;
    }
    char vbuf[REDIS_LOADBUF_LEN];   /* malloc() when the element is small */
    char *key = NULL, *val = NULL;
    dict *dict = server.dict[0];
    while (1) {
        /* Read type. */
        uint8_t type;
        if (fread(&type, 1, 1, fp) == 0) goto error;
        if (type == REDIS_EOF) break;
        /* Handle SELECT DB opcode as a special case */
        if (type == REDIS_SELECTDB) {
        	uint32_t dbid;
            if (fread(&dbid, 4, 1, fp) == 0) goto error;
            dbid = ntohl(dbid);
            if (dbid >= (unsigned)server.dbnum) {
                redisLog(REDIS_WARNING, "FATAL: Data file was created with a Redis server "
                		"compiled to handle more than %d databases. Exiting\n", server.dbnum);
                exit(1);
            }
            dict = server.dict[dbid];
            continue;
        }
        /* Read key */
        uint32_t klen;
        if (fread(&klen, 4, 1, fp) == 0) goto error;
        klen = ntohl(klen);
        if (klen <= REDIS_LOADBUF_LEN) {
            key = buf;
        } else {
            key = malloc(klen);
            if (!key) oom("Loading DB from file");
        }
        if (fread(key, klen, 1, fp) == 0) goto error;
        redisObject *obj;
        if (type == REDIS_STRING) {
            /* Read string value */
        	uint32_t vlen;
            if (fread(&vlen, 4, 1, fp) == 0) goto error;
            vlen = ntohl(vlen);
            if (vlen <= REDIS_LOADBUF_LEN) {
                val = vbuf;
            } else {
                val = malloc(vlen);
                if (!val) oom("Loading DB from file");
            }
            if (fread(val, vlen, 1, fp) == 0) goto error;
            obj = createObject(REDIS_STRING, sdsnewlen(val, vlen));
        } else if (type == REDIS_LIST) {
            /* Read list value */
            uint32_t listlen;
            if (fread(&listlen, 4, 1, fp) == 0) goto error;
            listlen = ntohl(listlen);
            obj = createListObject();
            /* Load every single element of the list */
            while (listlen--) {
                uint32_t vlen;
                if (fread(&vlen, 4, 1, fp) == 0) goto error;
                vlen = ntohl(vlen);
                if (vlen <= REDIS_LOADBUF_LEN) {
                    val = vbuf;
                } else {
                    val = malloc(vlen);
                    if (!val) oom("Loading DB from file");
                }
                if (fread(val, vlen, 1, fp) == 0) goto error;
                redisObject *elem = createObject(REDIS_STRING, sdsnewlen(val, vlen));
                if (!listAddNodeTail((list*)obj->ptr, elem)) oom("listAddNodeTail");
                /* free the temp buffer if needed */
                if (val != vbuf) free(val);
                val = NULL;
            }
        } else {
            assert(0 != 0);
        }
        /* Add the new object in the hash table */
        int retval = dictAdd(dict, sdsnewlen(key, klen), obj);
        if (retval == DICT_ERR) {
            redisLog(REDIS_WARNING, "Loading DB, duplicated key found! Unrecoverable error, exiting now.");
            exit(1);
        }
        /* Iteration cleanup */
        if (key != buf) free(key);
        if (val != vbuf) free(val);
        key = val = NULL;
    }
    fclose(fp);
    return REDIS_OK;

error: /* unexpected end of file is handled here with a fatal exit */
    if (key != buf) free(key);
    if (val != vbuf) free(val);
    redisLog(REDIS_WARNING, "Short read loading DB. Unrecoverable error, exiting now.");
    exit(1);
    return REDIS_ERR; /* Just to avoid warning */
}

/* ====================== Redis server networking stuff ===================== */
static void appendServerSaveParams(time_t seconds, int changes)
{
    server.saveparams = realloc(server.saveparams, sizeof(struct saveParam)*(server.saveparamslen+1));
    if (server.saveparams == NULL) oom("appendServerSaveParams");
    server.saveparams[server.saveparamslen].seconds = seconds;
    server.saveparams[server.saveparamslen].changes = changes;
    server.saveparamslen++;
}

static void ResetServerSaveParams()
{
    free(server.saveparams);
    server.saveparams = NULL;
    server.saveparamslen = 0;
}

static void initServerConfig()
{
    server.dbnum = REDIS_DEFAULT_DBNUM;
    server.port = REDIS_SERVERPORT;
    server.verbosity = REDIS_DEBUG;
    server.maxidletime = REDIS_MAXIDLETIME;
    server.saveparams = NULL;
    server.saveparamslen = 0;
    server.logfile = NULL; /* NULL = log on standard output */
    appendServerSaveParams(60*60, 1);  /* save after 1 hour and 1 change */
    appendServerSaveParams(300, 100);  /* save after 5 minutes and 100 changes */
    appendServerSaveParams(60, 10000); /* save after 1 minute and 10000 changes */
}

static void freeClientArgv(redisClient *c)
{
    for (int j = 0; j < c->argc; j++) sdsfree(c->argv[j]);
    c->argc = 0;
}

static void freeClient(redisClient *client)
{
    eDeleteFileEvent(server.el, client->fd, E_READABLE);
    eDeleteFileEvent(server.el, client->fd, E_WRITABLE);
    sdsfree(client->querybuf);
    listRelease(client->reply);
    freeClientArgv(client);
    close(client->fd);
    listNode *node = listSearchKey(server.clients, client);
    assert(node != NULL);
    listDelNode(server.clients, node);
    free(client);
}

static void closeTimedoutClients(void)
{
    listIter *it = listGetIterator(server.clients, DL_START_HEAD);
    if (!it) return;
    listNode *node;
    while ((node = listNextElement(it)) != NULL) {
    	redisClient *c = listNodeValue(node);
    	time_t now = time(NULL);
        if (now - c->lastinteraction > server.maxidletime) {
            redisLog(REDIS_DEBUG, "Closing idle client");
            freeClient(c);
        }
    }
    listReleaseIterator(it);
}

static int serverCron(struct eEventLoop *eventLoop, long long id, void *clientData)
{
    REDIS_NOTUSED(eventLoop);
    REDIS_NOTUSED(id);
    REDIS_NOTUSED(clientData);

    int loops = server.cronloops++;
    /* If the percentage of used slots in the HT reaches REDIS_HT_MINFILL
     * we resize the hash table to save memory */
    for (int j = 0; j < server.dbnum; j++) {
        int size = dictGetHashTableSize(server.dict[j]);
        int used = dictGetHashTableUsed(server.dict[j]);
        if (!(loops % 5) && used > 0) redisLog(REDIS_DEBUG, "DB %d: %d keys in %d slots HT.", j, used, size);
        if (size && used && (size > REDIS_HT_MINSLOTS) && (used*100/size < REDIS_HT_MINFILL)) {
            redisLog(REDIS_NOTICE, "The hash table %d is too sparse, resize it...", j);
            dictResize(server.dict[j]);
            redisLog(REDIS_NOTICE, "Hash table %d resized.", j);
        }
    }

    /* Show information about connected clients */
    if (!(loops % 5)) redisLog(REDIS_DEBUG, "%d clients connected", listLength(server.clients));

    /* Close connections of timeout clients */
    if (!(loops % 10)) closeTimedoutClients();

    /* Check if a background saving in progress terminated */
    if (server.bgsaveinprogress) {
        int status;
        if (waitpid(-1, &status, WNOHANG)) {
            int exitcode = WEXITSTATUS(status);
            if (exitcode == 0) {
                redisLog(REDIS_NOTICE, "Background saving terminated with success");
                server.dirty = 0;
                server.lastsave = time(NULL);
            } else {
                redisLog(REDIS_WARNING, "Background saving error");
            }
            server.bgsaveinprogress = 0;
        }
    } else {
        /* If there is not a background saving in progress check if we have to save now */
         time_t now = time(NULL);
         for (int j = 0; j < server.saveparamslen; j++) {
            struct saveParam *sp = server.saveparams + j;
            if (server.dirty >= sp->changes && now-server.lastsave > sp->seconds) {
                redisLog(REDIS_NOTICE, "%d changes in %d seconds. Saving...", sp->changes, sp->seconds);
                saveDbBackground("dump.rdb");
                break;
            }
         }
    }
    return 1000;
}

static void createSharedObjects(void)
{
    sharedObjs.crlf = createObject(REDIS_STRING, sdsnew("\r\n"));
    sharedObjs.ok = createObject(REDIS_STRING, sdsnew("+OK\r\n"));
    sharedObjs.err = createObject(REDIS_STRING, sdsnew("-ERR\r\n"));
    sharedObjs.zerobulk = createObject(REDIS_STRING, sdsnew("0\r\n\r\n"));
    sharedObjs.nil = createObject(REDIS_STRING, sdsnew("nil\r\n"));
    sharedObjs.zero = createObject(REDIS_STRING, sdsnew("0\r\n"));
    sharedObjs.one = createObject(REDIS_STRING, sdsnew("1\r\n"));
    sharedObjs.pong = createObject(REDIS_STRING, sdsnew("+PONG\r\n"));
}

static void initServer()
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    server.clients = listCreate();
    server.objfreelist = listCreate();
    createSharedObjects();
    server.el = eCreateEventLoop();
    server.dict = malloc(sizeof(dict *) * server.dbnum);
    if (!server.dict || !server.clients || !server.el || !server.objfreelist) oom("server initialization"); /* Fatal OOM */
    for (int j = 0; j < server.dbnum; j++) {
        server.dict[j] = dictCreate(&sdsDictType, NULL);
        if (!server.dict[j]) oom("server initialization"); /* Fatal OOM */
    }
    server.fd = netTcpServer(server.neterr, server.port, NULL);
    if (server.fd == -1) {
        redisLog(REDIS_WARNING, "Opening TCP port: %s", server.neterr);
        exit(1);
    }
    server.cronloops = 0;
    server.bgsaveinprogress = 0;
    server.lastsave = time(NULL);
    server.dirty = 0;
    eCreateTimeEvent(server.el, 1000, serverCron, NULL, NULL);
}

/* I agree, this is a very rudimental way to load a configuration...
   will improve later if the config gets more complex */
static void loadServerConfig(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        redisLog(REDIS_WARNING, "Fatal error, can't open config file");
        exit(1);
    }
    char buf[REDIS_CONFIGLINE_MAX+1], *err = NULL;
    int linenum = 0;
    sds line = NULL;
    while (fgets(buf, REDIS_CONFIGLINE_MAX+1, fp) != NULL) {
        linenum++;
        line = sdsnew(buf);
        line = sdstrim(line, " \t\r\n");
        /* Skip comments and blank lines*/
        if (line[0] == '#' || line[0] == '\0') {
            sdsfree(line);
            continue;
        }
        /* Split into arguments */
        int argc;
        sds *argv = sdssplitlen(line, sdslen(line), " ", 1, &argc);
        /* Execute config directives */
        if (!strcmp(argv[0], "timeout") && argc == 2) {
            server.maxidletime = atoi(argv[1]);
            if (server.maxidletime < 1) {
            	err = "Invalid timeout value";
            	goto loaderr;
            }
        } else if (!strcmp(argv[0], "save") && argc == 3) {
            int seconds = atoi(argv[1]);
            int changes = atoi(argv[2]);
            if (seconds < 1 || changes < 0) {
            	err = "Invalid save parameters";
            	goto loaderr;
            }
            appendServerSaveParams(seconds, changes);
        } else if (!strcmp(argv[0], "dir") && argc == 2) {
            if (chdir(argv[1]) == -1) {
                redisLog(REDIS_WARNING, "Can't chdir to '%s': %s", argv[1], strerror(errno));
                exit(1);
            }
        } else if (!strcmp(argv[0], "loglevel") && argc == 2) {
            if (!strcmp(argv[1], "debug")) server.verbosity = REDIS_DEBUG;
            else if (!strcmp(argv[1], "notice")) server.verbosity = REDIS_NOTICE;
            else if (!strcmp(argv[1], "warning")) server.verbosity = REDIS_WARNING;
            else {
                err = "Invalid log level. Must be one of debug, notice, warning";
                goto loaderr;
            }
        } else if (!strcmp(argv[0], "logfile") && argc == 2) {
            server.logfile = strdup(argv[1]);
            if (!strcmp(server.logfile, "stdout")) server.logfile = NULL;
            if (server.logfile) {
                /* Test if we are able to open the file. The server will not
                 * be able to abort just for this problem later... */
            	FILE *fp = fopen(server.logfile, "a");
                if (fp == NULL) {
                    err = sdscatprintf(sdsempty(), "Can't open the log file: %s", strerror(errno));
                    goto loaderr;
                }
                fclose(fp);
            }
        } else if (!strcmp(argv[0], "databases") && argc == 2) {
            server.dbnum = atoi(argv[1]);
            if (server.dbnum < 1) {
                err = "Invalid number of databases";
                goto loaderr;
            }
        } else {
            err = "Bad directive or wrong number of arguments";
            goto loaderr;
        }
        sdsfree(line);
    }
    fclose(fp);
    return;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
    fprintf(stderr, ">>> '%s'\n", line);
    fprintf(stderr, "%s\n", err);
    exit(1);
}

static void sendReplyToClient(eEventLoop *el, int fd, void *privdata, int mask)
{
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    int nwritten = 0, totwritten = 0;
    redisClient *c = privdata;
    while (listLength(c->reply)) {
    	redisObject *o = listNodeValue(listFirst(c->reply));
        int objlen = sdslen(o->ptr);
        if (objlen == 0) {
            listDelNode(c->reply, listFirst(c->reply));
            continue;
        }

        nwritten = write(fd, o->ptr+c->sentlen, objlen - c->sentlen);
        if (nwritten <= 0) break;
        c->sentlen += nwritten;
        totwritten += nwritten;
        /* If we fully sent the object on head go to the next one */
        if (c->sentlen == objlen) {
            listDelNode(c->reply, listFirst(c->reply));
            c->sentlen = 0;
        }
    }
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            redisLog(REDIS_DEBUG, "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    }
    if (totwritten > 0) c->lastinteraction = time(NULL);
    if (listLength(c->reply) == 0) {
        c->sentlen = 0;
        eDeleteFileEvent(server.el, c->fd, E_WRITABLE);
    }
}

/* resetClient prepare the client to process the next command */
static void resetClient(redisClient *c)
{
    freeClientArgv(c);
    c->bulklen = -1;
}

static void addReply(redisClient *c, redisObject *obj)
{
    if (listLength(c->reply) == 0 &&
    		eCreateFileEvent(server.el, c->fd, E_WRITABLE, sendReplyToClient, c, NULL) == E_ERR) return;
    if (!listAddNodeTail(c->reply, obj)) oom("listAddNodeTail");
    incrRefCount(obj);
}

static void addReplySds(redisClient *client, sds s)
{
    redisObject *o = createObject(REDIS_STRING, s);
    addReply(client,o);
    decrRefCount(o);
}

static int selectDb(redisClient *client, int id)
{
    if (id < 0 || id >= server.dbnum) return REDIS_ERR;
    client->dict = server.dict[id];
    return REDIS_OK;
}

static struct redisCommand *lookupCommand(char *name)
{
    int j = 0;
    while (cmdTable[j].name != NULL) {
        if (!strcmp(name, cmdTable[j].name)) return &cmdTable[j];
        j++;
    }
    return NULL;
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * If 1 is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if 0 is returned the client was destroyed (i.e. after QUIT). */
static int processCommand(redisClient *client)
{
    sdstolower(client->argv[0]);
    /* The QUIT command is handled as a special case. Normal command
     * procs are unable to close the client connection safely */
    if (!strcmp(client->argv[0], "quit")) {
        freeClient(client);
        return 0;
    }
    struct redisCommand *cmd = lookupCommand(client->argv[0]);
    if (!cmd) {
        addReplySds(client, sdsnew("-ERR unknown command\r\n"));
        resetClient(client);
        return 1;
    } else if (cmd->argc != client->argc) {
        addReplySds(client, sdsnew("-ERR wrong number of arguments\r\n"));
        resetClient(client);
        return 1;
    } else if (cmd->type == REDIS_CMD_BULK && client->bulklen == -1) {
        int bulklen = atoi(client->argv[client->argc-1]);
        sdsfree(client->argv[client->argc-1]);
        if (bulklen < 0 || bulklen > 1024*1024*1024) {
            client->argc--;
            client->argv[client->argc] = NULL;
            addReplySds(client, sdsnew("-ERR invalid bulk write count\r\n"));
            resetClient(client);
            return 1;
        }
        client->argv[client->argc-1] = NULL;
        client->argc--;
        client->bulklen = bulklen + 2; /* add two bytes for CR+LF */
        /* It is possible that the bulk read is already in the
         * buffer. Check this condition and handle it accordingly */
        if ((signed)sdslen(client->querybuf) >= client->bulklen) {
            client->argv[client->argc] = sdsnewlen(client->querybuf, client->bulklen-2);
            client->argc++;
            client->querybuf = sdsrange(client->querybuf, client->bulklen, -1);
        } else {
            return 1;
        }
    }
    /* Exec the command */
    cmd->proc(client);
    resetClient(client);
    return 1;
}
//???????
static void readQueryFromClient(eEventLoop *el, int fd, void *privdata, int mask)
{
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    redisClient *client = (redisClient *) privdata;
    char buf[REDIS_QUERYBUF_LEN];
    int nread = read(fd, buf, REDIS_QUERYBUF_LEN);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_DEBUG, "Reading from client: %s", strerror(errno));
            freeClient(client);
            return;
        }
    } else if (nread == 0) {
        redisLog(REDIS_DEBUG, "Client closed connection");
        freeClient(client);
        return;
    }
    if (nread) {
        client->querybuf = sdscatlen(client->querybuf, buf, nread);
        client->lastinteraction = time(NULL);
    } else {
        return;
    }

again:
    if (client->bulklen == -1) {
        /* Read the first line of the query */
        char *p = strchr(client->querybuf, '\n');
        if (p) {
            sds query = client->querybuf;
            client->querybuf = sdsempty();
            size_t querylen = 1 + (p - query);
            if (sdslen(query) > querylen) {
                /* leave data after the first line of the query in the buffer */
                client->querybuf = sdscatlen(client->querybuf, query+querylen, sdslen(query)-querylen);
            }
            *p = '\0'; /* remove "\n" */
            if (*(p - 1) == '\r') *(p - 1) = '\0'; /* and "\r" if any */
            sdsupdatelen(query);

            /* Now we can split the query in arguments */
            if (sdslen(query) == 0) {
                /* Ignore empty query */
                sdsfree(query);
                return;
            }
            int argc;
            sds *argv = sdssplitlen(query, sdslen(query), " ", 1, &argc);
            sdsfree(query);
            if (argv == NULL) oom("Splitting query in token");
            for (int j = 0; j < argc && j < REDIS_MAX_ARGS; j++) {
                if (sdslen(argv[j])) {
                    client->argv[client->argc] = argv[j];
                    client->argc++;
                } else {
                    sdsfree(argv[j]);
                }
            }
            free(argv);
            /* Execute the command. If the client is still valid
             * after processCommand() return and there is something
             * on the query buffer try to process the next command. */
            if (processCommand(client) && sdslen(client->querybuf)) goto again;
            return;
        } else if (sdslen(client->querybuf) >= 1024) {
            redisLog(REDIS_DEBUG, "Client protocol error");
            freeClient(client);
            return;
        }
    } else {
        /* Bulk read handling. Note that if we are at this point
           the client already sent a command terminated with a newline,
           we are reading the bulk data that is actually the last
           argument of the command. */
        if (client->bulklen <= (int)sdslen(client->querybuf)) {
            /* Copy everything but the final CRLF as final argument */
            client->argv[client->argc] = sdsnewlen(client->querybuf, client->bulklen-2);
            client->argc++;
            client->querybuf = sdsrange(client->querybuf, client->bulklen, -1);
            processCommand(client);
            return;
        }
    }
}

static int createClient(int fd)
{
    netNonBlock(NULL, fd);
    netTcpNoDelay(NULL, fd);
    redisClient *client = malloc(sizeof(struct redisClient));
    if (!client) return REDIS_ERR;
    client->fd = fd;
    selectDb(client, 0);
    client->querybuf = sdsempty();
    client->argc = 0;
    client->bulklen = -1;
    if ((client->reply = listCreate()) == NULL) oom("listCreate");
    listSetFreeMethod(client->reply, decrRefCount);
    client->sentlen = 0;
    client->lastinteraction = time(NULL);
    if (eCreateFileEvent(server.el, client->fd, E_READABLE, readQueryFromClient, client, NULL) == E_ERR) {
        freeClient(client);
        return REDIS_ERR;
    }
    if (!listAddNodeTail(server.clients, client)) oom("listAddNodeTail");
    return REDIS_OK;
}

static void acceptHandler(eEventLoop *el, int fd, void *privdata, int mask)
{
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(privdata);
    REDIS_NOTUSED(mask);
    int cport;
    char cip[128];
    int cfd = netAccept(server.neterr, fd, cip, &cport);
    if (cfd == NET_ERR) {
        redisLog(REDIS_DEBUG, "Accepting client connection: %s", server.neterr);
        return;
    }
    redisLog(REDIS_DEBUG, "Accepted %s:%d", cip, cport);
    if (createClient(cfd) == REDIS_ERR) {
        redisLog(REDIS_WARNING, "Error allocating resources for the client");
        close(cfd); /* May be already closed, just ignore errors */
        return;
    }
}

/*============================= Commands ============================== */
static void pingCommand(redisClient *client)
{
    addReply(client, sharedObjs.pong);
}

static void echoCommand(redisClient *client) {
    addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(client->argv[1])));
    addReplySds(client,client->argv[1]);
    addReply(client,sharedObjs.crlf);
    client->argv[1] = NULL;
}

static void setGenericCommand(redisClient *c, int nx) {
    redisObject *o = createObject(REDIS_STRING,c->argv[2]);
    c->argv[2] = NULL;
    int retval = dictAdd(c->dict,c->argv[1],o);
    if (retval == DICT_ERR) {
        if (!nx) dictReplace(c->dict,c->argv[1],o);
        else decrRefCount(o);
    } else {
        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    }
    server.dirty++;
    addReply(c,sharedObjs.ok);
}

static void setCommand(redisClient *client) {
    return setGenericCommand(client,0);
}

static void setnxCommand(redisClient *client) {
    return setGenericCommand(client,1);
}

static void getCommand(redisClient *client) {
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) {
        addReply(client,sharedObjs.nil);
    } else {
        redisObject *o = dictGetEntryVal(de);
        if (o->type != REDIS_STRING) {
            char *err = "GET against key not holding a string value";
            addReplySds(client,
                sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(o->ptr)));
            addReply(client,o);
            addReply(client,sharedObjs.crlf);
        }
    }
}

static void delCommand(redisClient *client) {
    if (dictDelete(client->dict,client->argv[1]) == DICT_OK) server.dirty++;
    addReply(client,sharedObjs.ok);
}

static void existsCommand(redisClient *client) {
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) addReply(client,sharedObjs.zero);
    else addReply(client,sharedObjs.one);
}

static void incrDecrCommand(redisClient *c, int incr) {
    long long value;
    dictEntry *de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        value = 0;
    } else {
        redisObject *o = dictGetEntryVal(de);
        if (o->type != REDIS_STRING) {
            value = 0;
        } else {
            char *eptr;
            value = strtoll(o->ptr, &eptr, 10);
        }
    }

    value += incr;
    sds newval = sdscatprintf(sdsempty(),"%lld",value);
    redisObject *o = createObject(REDIS_STRING,newval);
    int retval = dictAdd(c->dict,c->argv[1],o);
    if (retval == DICT_ERR) {
        dictReplace(c->dict,c->argv[1],o);
    } else {
        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    }
    server.dirty++;
    addReply(c,o);
    addReply(c,sharedObjs.crlf);
}

static void incrCommand(redisClient *client) {
    return incrDecrCommand(client,1);
}

static void decrCommand(redisClient *client) {
    return incrDecrCommand(client,-1);
}

static void selectCommand(redisClient *client) {
    int id = atoi(client->argv[1]);
    
    if (selectDb(client,id) == REDIS_ERR) {
        addReplySds(client,"-ERR invalid DB index\r\n");
    } else {
        addReply(client,sharedObjs.ok);
    }
}

static void randomkeyCommand(redisClient *client) {
    dictEntry *de = dictGetRandomKey(client->dict);
    if (de == NULL) {
        addReply(client,sharedObjs.crlf);
    } else {
        addReply(client,dictGetEntryVal(de));
        addReply(client,sharedObjs.crlf);
    }
}

static void keysCommand(redisClient *client) {
    sds pattern = client->argv[1];
    int plen = sdslen(pattern);

    dictIterator *di = dictGetIterator(client->dict);
    sds keys = sdsempty();
    dictEntry *de;
    while((de = dictNext(di)) != NULL) {
        sds key = dictGetEntryKey(de);
        if ((pattern[0] == '*' && pattern[1] == '\0') ||
            stringmatchlen(pattern,plen,key,sdslen(key),0)) {
            keys = sdscat(keys, key);
            keys = sdscatlen(keys, " ", 1);
        }
    }
    dictReleaseIterator(di);
    keys = sdstrim(keys," ");
    sds reply = sdscatprintf(sdsempty(),"%lu\r\n",sdslen(keys));
    reply = sdscat(reply, keys);
    reply = sdscatlen(reply, "\r\n", 2);
    sdsfree(keys);
    addReplySds(client,reply);
}

static void dbsizeCommand(redisClient *client) {
    addReplySds(client, sdscatprintf(sdsempty(),"%lu\r\n",dictGetHashTableUsed(client->dict)));
}

static void lastsaveCommand(redisClient *client) {
    addReplySds(client, sdscatprintf(sdsempty(),"%lu\r\n",server.lastsave));
}

static void saveCommand(redisClient *client) {
    if (saveDb("dump.rdb") == REDIS_OK) addReply(client,sharedObjs.ok);
    else addReply(client,sharedObjs.err);
}

static void bgsaveCommand(redisClient *client) {
    if (server.bgsaveinprogress) {
        addReplySds(client,sdsnew("-ERR background save already in progress\r\n"));
        return;
    }
    if (saveDbBackground("dump.rdb") == REDIS_OK) addReply(client,sharedObjs.ok);
    else addReply(client,sharedObjs.err);
}

static void shutdownCommand(redisClient *client) {
    redisLog(REDIS_WARNING,"User requested shutdown, saving DB...");
    if (saveDb("dump.rdb") == REDIS_OK) {
        redisLog(REDIS_WARNING,"Server exit now, bye bye...");
        exit(1);
    } else {
        redisLog(REDIS_WARNING,"Error trying to save the DB, can't exit"); 
        addReplySds(client,sdsnew("-ERR can't quit, problems saving the DB\r\n"));
    }
}

static void renameGenericCommand(redisClient *c, int nx) {
    /* To use the same key as src and dst is probably an error */
    if (sdscmp(c->argv[1],c->argv[2]) == 0) {
        addReplySds(c,sdsnew("-ERR src and dest key are the same\r\n"));
        return;
    }

    dictEntry *de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReplySds(c,sdsnew("-ERR no such key\r\n"));
        return;
    }
    redisObject *o = dictGetEntryVal(de);
    incrRefCount(o);
    if (dictAdd(c->dict,c->argv[2],o) == DICT_ERR) {
        if (nx) {
            decrRefCount(o);
            addReplySds(c,sdsnew("-ERR destination key exists\r\n"));
            return;
        }
        dictReplace(c->dict,c->argv[2],o);
    } else {
        c->argv[2] = NULL;
    }
    dictDelete(c->dict,c->argv[1]);
    server.dirty++;
    addReply(c,sharedObjs.ok);
}

static void renameCommand(redisClient *client) {
    renameGenericCommand(client,0);
}

static void renamenxCommand(redisClient *client) {
    renameGenericCommand(client,1);
}

static void moveCommand(redisClient *client) {
    /* Obtain source and target DB pointers */
    dict *src = client->dict;
    if (selectDb(client,atoi(client->argv[2])) == REDIS_ERR) {
        addReplySds(client,sdsnew("-ERR target DB out of range\r\n"));
        return;
    }
    dict *dst = client->dict;
    client->dict = src;

    /* If the user is moving using as target the same
     * DB as the source DB it is probably an error. */
    if (src == dst) {
        addReplySds(client,sdsnew("-ERR source DB is the same as target DB\r\n"));
        return;
    }

    /* Check if the element exists and get a reference */
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (!de) {
        addReplySds(client,sdsnew("-ERR no such key\r\n"));
        return;
    }

    /* Try to add the element to the target DB */
    sds *key = dictGetEntryKey(de);
    redisObject *o = dictGetEntryVal(de);
    if (dictAdd(dst,key,o) == DICT_ERR) {
        addReplySds(client,sdsnew("-ERR target DB already contains the moved key\r\n"));
        return;
    }

    /* OK! key moved, free the entry in the source DB */
    dictDeleteNoFree(src,client->argv[1]);
    server.dirty++;
    addReply(client,sharedObjs.ok);
}

static void pushGenericCommand(redisClient *c, int where) {
    redisObject *lobj;
    list *list;
    
    redisObject *ele = createObject(REDIS_STRING,c->argv[2]);
    c->argv[2] = NULL;

    dictEntry *de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        lobj = createListObject();
        list = lobj->ptr;
        if (where == REDIS_HEAD) {
            if (!listAddNodeHead(list,ele)) oom("listAddNodeHead");
        } else {
            if (!listAddNodeTail(list,ele)) oom("listAddNodeTail");
        }
        dictAdd(c->dict,c->argv[1],lobj);

        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    } else {
        lobj = dictGetEntryVal(de);
        if (lobj->type != REDIS_LIST) {
            decrRefCount(ele);
            addReplySds(c,sdsnew("-ERR push against existing key not holding a list\r\n"));
            return;
        }
        list = lobj->ptr;
        if (where == REDIS_HEAD) {
            if (!listAddNodeHead(list,ele)) oom("listAddNodeHead");
        } else {
            if (!listAddNodeTail(list,ele)) oom("listAddNodeTail");
        }
    }
    server.dirty++;
    addReply(c,sharedObjs.ok);
}

static void lpushCommand(redisClient *client) {
    pushGenericCommand(client,REDIS_HEAD);
}

static void rpushCommand(redisClient *client) {
    pushGenericCommand(client,REDIS_TAIL);
}

static void llenCommand(redisClient *client) {
    list *l;
    
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) {
        addReply(client,sharedObjs.zero);
        return;
    } else {
        redisObject *o = dictGetEntryVal(de);
        if (o->type != REDIS_LIST) {
            addReplySds(client,sdsnew("-1\r\n"));
        } else {
            l = o->ptr;
            addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",listLength(l)));
        }
    }
}

static void lindexCommand(redisClient *client) {
    int index = atoi(client->argv[2]);
    
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) {
        addReply(client,sharedObjs.nil);
    } else {
        redisObject *o = dictGetEntryVal(de);
        if (o->type != REDIS_LIST) {
            char *err = "LINDEX against key not holding a list value";
            addReplySds(client, sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            listNode *ln = listIndex(list, index);
            if (ln == NULL) {
                addReply(client,sharedObjs.nil);
            } else {
                redisObject *ele = listNodeValue(ln);
                addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(client,ele);
                addReply(client,sharedObjs.crlf);
            }
        }
    }
}

static void popGenericCommand(redisClient *c, int where) {
    dictEntry *de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,sharedObjs.nil);
    } else {
        redisObject *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            char *err = "POP against key not holding a list value";
            addReplySds(c, sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            listNode *ln;

            if (where == REDIS_HEAD) ln = listFirst(list);
            else ln = listLast(list);

            if (ln == NULL) {
                addReply(c,sharedObjs.nil);
            } else {
                redisObject *ele = listNodeValue(ln);
                addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(c,ele);
                addReply(c,sharedObjs.crlf);
                listDelNode(list,ln);
                server.dirty++;
            }
        }
    }
}

static void lpopCommand(redisClient *client) {
    popGenericCommand(client,REDIS_HEAD);
}

static void rpopCommand(redisClient *client) {
    popGenericCommand(client,REDIS_TAIL);
}

static void lrangeCommand(redisClient *client) {
    int start = atoi(client->argv[2]);
    int end = atoi(client->argv[3]);
    
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) {
        addReply(client,sharedObjs.nil);
    } else {
        redisObject *o = dictGetEntryVal(de);
        if (o->type != REDIS_LIST) {
            char *err = "LRANGE against key not holding a list value";
            addReplySds(client, sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            int llen = listLength(list);
            redisObject *ele;

            /* convert negative indexes */
            if (start < 0) start = llen+start;
            if (end < 0) end = llen+end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;

            /* indexes sanity checks */
            if (start > end || start >= llen) {
                /* Out of range start or start > end result in empty list */
                addReply(client,sharedObjs.zero);
                return;
            }
            if (end >= llen) end = llen-1;
            int rangelen = (end-start)+1;

            /* Return the result in form of a multi-bulk reply */
            listNode *ln = listIndex(list, start);
            addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",rangelen));
            for (int j = 0; j < rangelen; j++) {
                ele = listNodeValue(ln);
                addReplySds(client,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(client,ele);
                addReply(client,sharedObjs.crlf);
                ln = ln->next;
            }
        }
    }
}

static void ltrimCommand(redisClient *client) {
    int start = atoi(client->argv[2]);
    int end = atoi(client->argv[3]);
    
    dictEntry *de = dictFind(client->dict,client->argv[1]);
    if (de == NULL) {
        addReplySds(client,sdsnew("-ERR no such key\r\n"));
    } else {
        redisObject *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            addReplySds(client, sdsnew("-ERR LTRIM against key not holding a list value"));
        } else {
            list *list = o->ptr;
            listNode *ln;
            int llen = listLength(list);
            int ltrim, rtrim;

            /* convert negative indexes */
            if (start < 0) start = llen+start;
            if (end < 0) end = llen+end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;

            /* indexes sanity checks */
            if (start > end || start >= llen) {
                /* Out of range start or start > end result in empty list */
                ltrim = llen;
                rtrim = 0;
            } else {
                if (end >= llen) end = llen-1;
                ltrim = start;
                rtrim = llen-end-1;
            }

            /* Remove list elements to perform the trim */
            for (int j = 0; j < ltrim; j++) {
                ln = listFirst(list);
                listDelNode(list,ln);
            }
            for (int j = 0; j < rtrim; j++) {
                ln = listLast(list);
                listDelNode(list,ln);
            }
            addReply(client,sharedObjs.ok);
        }
    }
}

/* ============================= Main! ============================== */
int main(int argc, char **argv) {
	initServerConfig();
    initServer();
    if (argc == 2) {
        ResetServerSaveParams();
        loadServerConfig(argv[1]);
        redisLog(REDIS_NOTICE, "Configuration loaded");
    } else if (argc > 2) {
        fprintf(stderr, "Usage: ./redis-server [/path/to/redis.conf]\n");
        exit(1);
    }
    redisLog(REDIS_NOTICE, "Server started");
    if (loadDb("dump.rdb") == REDIS_OK)
        redisLog(REDIS_NOTICE, "DB loaded from disk");
    if (eCreateFileEvent(server.el, server.fd, E_READABLE, acceptHandler, NULL, NULL) == E_ERR)
    	oom("creating file event");
    redisLog(REDIS_NOTICE, "The server is now ready to accept connections");
    eMain(server.el);
    eDeleteEventLoop(server.el);
    return 0;
}
