CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

all: monitor monitor_standalone readMonitoringData

monitor: monitoring.c
	${CC} ${CFLAGS} -o $@ $^ -ldcgm

monitor_standalone: monitoring_standalone.c
	${CC} ${CFLAGS} -o $@ $^ -ldcgm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^
