#define GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <cuda.h>
#include <cuda_runtime.h>

#define CUDA_CALL(x) { checkCuda((x), __FILE__, __LINE__); }

inline void checkCuda(cudaError_t code, const char * file, int line){
	if (code != cudaSuccess){
		fprintf(stderr, "CUDA Error in File %s, Line %d: %s\n", file, line, cudaGetErrorString(code));
		exit(1);
	}
}

__global__ void dummyComputeKernel(size_t loop_bound){
	int ind = blockDim.x * blockIdx.x + threadIdx.x;
	// USE "VOLATILE" so compiler does not optimize away (still doesn't work!)
	// using -g -G flags in Makefile to ensure loop is executed...
	for (volatile int i = 0; i < loop_bound; i++) {
		// dummy loop
	}
}

int main(int argc, char *argv[]){
	
	// get device info so we can know how many SMs	
	int device;
	CUDA_CALL(cudaGetDevice(&device));

	struct cudaDeviceProp devProp;
	CUDA_CALL(cudaGetDeviceProperties(&devProp, device));

	int sm_count = devProp.multiProcessorCount;
	int max_thread_per_block = devProp.maxThreadsPerBlock;
	
	char * env_with_monitor = getenv("WITH_MONITOR");
	char * env_num_cpus = getenv("NUM_CPUS");

	if ((env_with_monitor == NULL) || (env_num_cpus == NULL)) {
		fprintf(stderr, "ERROR: Usage. Must declare 'WITH_MONITOR' & 'NUM_CPUS' env. variables\n");
		exit(1);
	}

	int is_with_monitoring = atoi(env_with_monitor);
	int num_cpus = atoi(env_num_cpus);

	char * hostbuffer = (char *) malloc(256 * sizeof(char));
	int hostname_ret = gethostname(hostbuffer, 256);
	if (hostname_ret == -1){
		fprintf(stderr, "Could not get hostname, exiting...\n");
		exit(1);
	}
	
	FILE * complete_timing_out_file;
	if (is_with_monitoring == 1){
		complete_timing_out_file = fopen("with_monitoring_dummy_compute.csv", "a");
	}
	else {
		complete_timing_out_file = fopen("raw_dummy_compute.csv", "a");
	}
        
	
	// set the device to have 70GB to identify which device on node we are working on
	// + don't get docked for Slurm priority for under-use
        size_t gb = 1024 * 1024 * 1024;
        float * d_p;
        CUDA_CALL(cudaMalloc(&d_p, 70 * gb));

	
	int blocks = sm_count;
	int threads = max_thread_per_block;

	dim3 gridDimDummy(blocks);
	dim3 blockDimDummy(threads);
	
	size_t dummy_loop_bound = 1e8;
	
	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop, elapsed;
	
	printf("Launching kernel...\n");

	clock_gettime(CLOCK_REALTIME, &start);
	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;

	// ACTUALLY LAUNCH KERNEL
	dummyComputeKernel <<< gridDimDummy, blockDimDummy >>> (dummy_loop_bound);
	
	// non-blocking cpu-side so must wait to kernel to finish before recording timing
	cudaDeviceSynchronize();

	clock_gettime(CLOCK_REALTIME, &stop);
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed = timestamp_stop - timestamp_start;

	fprintf(complete_timing_out_file, "%ld,%ld,%ld,%s,%d,%ld\n", elapsed, timestamp_start, timestamp_stop, hostbuffer, num_cpus, dummy_loop_bound);

	CUDA_CALL(cudaFree(d_p));	
}
