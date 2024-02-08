CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall

all: monitor readMonitoringData

monitor: monitoring.c
	${CC} ${CFLAGS} -o $@ $^ -ldcgm

readMonitoringData: read_monitoring_data.c
	${CC} ${CFLAGS} -o $@ $^
