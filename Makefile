CC = gcc
CFLAGS = -O2 -std=c99 -Wall -pedantic

SQLITE3_LIBRARY_PATH = /home/as1669/local/lib
SQLITE3_INCLUDE_PATH = /home/as1669/local/include

all: monitor

monitor: monitoring.c job_stats.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3 -ldcgm -lm

clean:
	rm -f monitor *.o
