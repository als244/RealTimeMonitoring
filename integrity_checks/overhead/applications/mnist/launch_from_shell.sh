#!/bin/sh

## pass number of iterations as argument!

NUM_REPEAT=$1


for (( i=0; i < $NUM_REPEAT; i++))
do
	echo "Launching repetition: $i"
	python mnist_train.py
done	
