CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

SQLITE3_LIBRARY_PATH = /usr/lib/x86_64-linux-gnu
SQLITE3_INCLUDE_PATH = /usr/include

all: monitor monitor_with_db readMonitoringData test_db

monitor: monitoring.c
	${CC} ${CFLAGS} -o $@ $^ -ldcgm

monitor_with_db: monitoring_with_db.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3 -lm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^

test_db: test_db.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3
