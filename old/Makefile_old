CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

SQLITE3_LIBRARY_PATH = /home/as1669/local/lib
SQLITE3_INCLUDE_PATH = /home/as1669/local/include

all: monitor readMonitoringData jobsTest insert_rows_from_csv

monitor: monitoring.c job_stats.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3 -ldcgm -lm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^

insert_rows_from_csv: insert_rows_from_csv.c
	${CC} ${CFLAGS} -o $@ $^ -I${SQLITE3_INCLUDE_PATH} -L${SQLITE3_LIBRARY_PATH} -lsqlite3 -lm

jobsTest: job_stats_test.c
	${CC} ${CFLAGS} -o $@ $^
