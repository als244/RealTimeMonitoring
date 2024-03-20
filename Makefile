CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

SQLITE3_LIBRARY_PATH = /usr/lib/x86_64-linux-gnu
SQLITE3_INCLUDE_PATH = /usr/include

all: monitor readMonitoringData test_db

monitor: monitoring.c
	${CC} ${CFLAGS} -o $@ $^ -ldcgm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^

test_db: test_db.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3
