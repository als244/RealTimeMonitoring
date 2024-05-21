all: dummyCompute

dummyCompute: dummy_compute.cu
	nvcc -g -G dummy_compute.cu -o dummyCompute

simpleCompute: simple_compute.cu
	nvcc -g -G simple_compute.cu -o simpleCompute

clean:
	rm -f dummyCompute simpleCompute
