CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

SQLITE3_LIBRARY_PATH = /home/as1669/local/lib
SQLITE3_INCLUDE_PATH = /home/as1669/local/include

all: monitor monitor_with_db readMonitoringData

monitor: monitoring.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3 -ldcgm -lm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^

