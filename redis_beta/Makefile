# Makefile for wbox
# Copyright (C) 2007 Salvatore Sanfilippo <antirez@invece.org>
# All Rights Reserved
# Under the GPL license version 2

DEBUG?= -g
CFLAGS?= -O2 -Wall -W -DSDS_ABORT_ON_OOM -std=gnu99
CCOPT= $(CFLAGS)

OBJ = dlist.o event.o net.o dict.o redis.o sds.o
PRGNAME = redis-server

all: redis-server

# Deps (use make dep to generate this)
dlist.o: dlist.c dlist.h
event.o: event.c event.h
net.o: net.c net.h
dict.o: dict.c dict.h
redis.o: redis.c redis.h event.h sds.h net.h dict.h dlist.h
sds.o: sds.c sds.h

redis-server: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ)
	@echo ""
	@echo "Hint: To run the test-redis.tcl script is a good idea."
	@echo "Launch the redis server with ./redis-server, then in another"
	@echo "terminal window enter this directory and run 'make test'."
	@echo ""

.c.o:
	$(CC) -c $(CCOPT) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(PRGNAME) *.o

dep:
	$(CC) -MM *.c

test:
	tclsh test-redis.tcl
