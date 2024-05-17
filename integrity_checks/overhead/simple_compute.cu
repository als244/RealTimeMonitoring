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
	// USE "VOLATILE" so compiler does not optimize away (confirmed with compiler explorer tool)
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
	int max_thread_per_sm = devProp.maxThreadsPerMultiProcessor;
	

	// info for dumping and matching with monitoring data
	struct timespec time;

	int is_with_monitoring = atoi(getenv("WITH_MONITORING"));
	int num_cpus = atoi(getenv("NUM_CPUS"));

	char * hostbuffer = (char *) malloc(256 * sizeof(char));
	int hostname_ret = gethostname(hostbuffer, 256);
	if (hostname_ret == -1){
		fprintf(stderr, "Could not get hostname, exiting...\n");
		exit(1);
	}
	
	FILE * compute_test_out_file;
	FILE * complete_timing_out_file;
	if (is_with_monitoring == 1){
		compute_test_out_file = fopen("with_monitoring_computeTest.csv", "a");
		complete_timing_out_file = fopen("with_monitoring_simple_compute.csv", "a");
	}
	else {
		compute_test_out_file = fopen("raw_computeTest.csv", "a");
		complete_timing_out_file = fopen("raw_simple_compute.csv", "a");
	}
        
	
	// sleep for 5 seconds between kernel launches
	size_t micros_sleep = 5 * 1e6;


	// set the device to have 70GB to identify which device on node we are working on
        size_t gb = 1024 * 1024 * 1024;
        float * d_p;
        CUDA_CALL(cudaMalloc(&d_p, 70 * gb));

	
	int n_block_sizes = 27;
	int blockSizes[] = {1, 2, 4, 8, 16, 32, 64, 70, 75, 80, 81, 82, 83, 84, 85, 86, 100, 116, 128, 168, 252, 256, 336, 512, 672, 840, 1024};

	int n_thread_sizes = 11;
	int threadSizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
	
	int blocks, threads;
	
	// every thread iterating to 1 billion 
	// (arbritrary number, need large enough to see measurements from kernel in monitoring, but not too large that it is wasting time...)
	// Will look at FieldIds 203 (normal GPU utilization), 1002 (SM active = ratio of cycles 1 warp scheduled), 1003 (SM occupancy = ratio of warps resident on SM to theoretical maximum)
	// hoping to see that with less blocks launched that 203 & 1002 show high numbmers and 1003 is low. As GPU gets saturated expect 1003 to increase...
	size_t dummy_loop_bound = 1e8;
	
	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop, elapsed;

	clock_gettime(CLOCK_REALTIME, &start);
	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;

	for (int blockInd = 0; blockInd < n_block_sizes; blockInd++){
		for (int threadInd = 0; threadInd < n_thread_sizes; threadInd++){
			blocks = blockSizes[blockInd];
			threads = threadSizes[threadInd];
			dim3 gridDimDummy(blocks);
			dim3 blockDimDummy(threads);
			clock_gettime(CLOCK_REALTIME, &time);
			// Current time (ns), about to launch #blocks, about to launch #threads
			fprintf(compute_test_out_file, "%ld,%d,%d\n", time.tv_sec * 1e9 + time.tv_nsec, blocks, threads);
			fflush(compute_test_out_file);
			dummyComputeKernel <<< gridDimDummy, blockDimDummy >>> (dummy_loop_bound);
			cudaDeviceSynchronize();
			usleep(micros_sleep);
		}
	}

	clock_gettime(CLOCK_REALTIME, &stop);
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed = timestamp_stop - timestamp_start;
	
	fprintf(complete_timing_out_file, "%ld,%ld,%ld,%s,%d\n", elapsed, timestamp_start, timestamp_stop, hostbuffer, num_cpus);

	CUDA_CALL(cudaFree(d_p));	
}
