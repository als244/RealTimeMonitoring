#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include "dcgm_agent.h"
#include "dcgm_fields.h"
#include "dcgm_structs.h"

#include "monitoring.h"


#define PRINT 1

// CPU MONITORING
Proc_Data * process_proc_stat(Sample * cur_sample, Proc_Data * prev_data){

	FILE * fp = fopen("/proc/stat", "r");

	if (fp == NULL){
		fprintf(stderr, "Error Opening /proc/stat\n");
		return NULL;
	}

	Proc_Data * proc_data = cur_sample -> cpu_util;

	// query available host memory
	// in MB
	unsigned long free_mem = (unsigned long) (get_avphys_pages() * sysconf(_SC_PAGESIZE)) / 1e6;
	proc_data -> free_mem = free_mem;

	// only collecting aggregate 
	Cpu_stat cpu_stats;

	// parse all the cpu data
	char dummy_name[255];
	// only look the top line which aggregates all CPUS
	fscanf(fp, "%s %lu %lu %lu %lu %lu %lu %lu", dummy_name, &(cpu_stats.t_user), &(cpu_stats.t_nice), 
        &(cpu_stats.t_system), &(cpu_stats.t_idle), &(cpu_stats.t_iowait), &(cpu_stats.t_irq),
        &(cpu_stats.t_softirq));

	//done with file, closing
	fclose(fp);
  

	long total_time = cpu_stats.t_user + cpu_stats.t_nice + cpu_stats.t_system + cpu_stats.t_idle + cpu_stats.t_iowait + cpu_stats.t_irq + cpu_stats.t_softirq;
	long idle_time = cpu_stats.t_idle;

	// if there wasn't a previous sample can't compute util %
	if (prev_data == NULL){
		proc_data -> util_pct = 0;
		proc_data -> total_time = total_time;
		proc_data -> idle_time = idle_time;
		return proc_data;
	}

	long prev_total_time = prev_data -> total_time;
	long prev_idle_time = prev_data -> idle_time;


	long total_delta = total_time - prev_total_time;
	long idle_delta = idle_time - prev_idle_time;

	long cpu_used = total_delta - idle_delta;

	double util_pct = (100 * (double) cpu_used) / ((double) total_delta);

	proc_data -> util_pct = util_pct;
	proc_data -> total_time = total_time;
	proc_data -> idle_time = idle_time;

	return proc_data;
}

// GPU MONITORING

// < 100 values, do not need hash table
int get_my_field_ind(unsigned short fieldId, unsigned short * field_ids, int n_fields){

	for (int i = 0; i < n_fields; i++){
		if (field_ids[i] == fieldId){
			return i;
		}
	}
	return -1;
}

// pass in memory location of pointer to allocated array
int copy_field_values_function(unsigned int gpuId, dcgmFieldValue_v1 * values, int numValues, void * userdata){
	Samples_Buffer * samples_buffer = (Samples_Buffer *) userdata;
	int n_samples = samples_buffer -> n_samples;
	Sample * cur_sample = &((samples_buffer -> samples)[n_samples]);
	unsigned short * field_ids = samples_buffer -> field_ids;
	int n_fields = samples_buffer -> n_fields;
	int n_devices = samples_buffer -> n_devices;
	// hardcoded but could use the fieldTypes
	int field_size_bytes = 8;
	unsigned short fieldId, fieldType;
	int indOfField;
	for (int i = 0; i < numValues; i++){
		fieldId = values[i].fieldId;
		indOfField = get_my_field_ind(fieldId, field_ids, n_fields);
		if (indOfField == -1){
			continue;
		}
		fieldType = values[i].fieldType;
		if (fieldType == DCGM_FT_DOUBLE){
			memcpy(cur_sample -> field_values + (gpuId * n_fields + indOfField) * field_size_bytes, &(values[i].value.dbl), field_size_bytes);
		}
		else if ((fieldType == DCGM_FT_INT64) || (fieldType == DCGM_FT_TIMESTAMP)){
			memcpy(cur_sample -> field_values + (gpuId * n_fields + indOfField) * field_size_bytes, &(values[i].value.i64), field_size_bytes);
		}
		else{
			// fieldType not supported
			continue;
		}		
	}
	return 0;
}

int dump_samples_buffer(Samples_Buffer * samples_buffer, char * dir){

	long first_time = (samples_buffer -> samples)[0].time.tv_sec;

	int n_cpu = samples_buffer -> n_cpu;
	int clk_tck = samples_buffer -> clk_tck;
	
	int n_fields = samples_buffer -> n_fields;
	int n_devices = samples_buffer -> n_devices;

	// hardcoded, but could also look at field_types field in sample struct
	int field_size_bytes = 8;

	int n_samples = samples_buffer -> n_samples;

	Sample * samples = samples_buffer -> samples;

	int n_wrote, print_ret;
	char * filepath = NULL;
	FILE * fp;

	// Saving timestamp to file which lists timestamps that have associated buffers

	print_ret = asprintf(&filepath, "%s/timestamps.txt", dir);
	fp = fopen(filepath, "a");

	if (fp == NULL){
		fprintf(stderr, "Could not open timestamps_file. Returning...\n");
		return -1;
	}

	fprintf(fp, "%li\n", first_time);
	fflush(fp);
	fclose(fp);
	free(filepath);


	// Saving Metadata

	asprintf(&filepath, "%s/%li.meta", dir, first_time);

	fp = fopen(filepath, "wb");
	if (fp == NULL){
		fprintf(stderr, "Could not open file for dumping sample buffer metadata. Returning...\n");
		return -1;
	}

	fwrite(&n_cpu, sizeof(int), 1, fp);
	fwrite(&clk_tck, sizeof(int), 1, fp);
	fwrite(&n_devices, sizeof(int), 1, fp);
	fwrite(&n_fields, sizeof(int), 1, fp);
	fwrite(&n_samples, sizeof(int), 1, fp);
	fwrite(samples_buffer -> field_types, sizeof(unsigned short), n_fields, fp);
	fwrite(samples_buffer -> field_ids, sizeof(unsigned short), n_fields, fp);

	fflush(fp);
	fclose(fp);
	free(filepath);


	// Saving Data
	
	print_ret = asprintf(&filepath, "%s/%li.buffer", dir, first_time);

	fp = fopen(filepath, "wb");
	if (fp == NULL){
		fprintf(stderr, "Could not open file for dumping sample buffer. Returning...\n");
		return -1;
	}

	Proc_Data * cpu_data;
	Sample data;

	// write timestamp and field values for every sample
	for (int i = 0; i < n_samples; i++){

		data = samples[i];
		fwrite(&(data.time), sizeof(struct timespec), 1, fp);

		// CPU dump
		cpu_data = data.cpu_util;
		fwrite(&(cpu_data -> free_mem), sizeof(unsigned long), 1, fp);
		fwrite(&(cpu_data -> util_pct), sizeof(double), 1, fp);
		
		// GPU field value dump
		fwrite(data.field_values, 1, n_fields * n_devices * field_size_bytes, fp);
	}

	fflush(fp);
	fclose(fp);
	free(filepath);

	// reset samples
	struct timespec time;
	for (int i = 0; i < n_samples; i++){
		samples[i].time = time;
		memset(samples[i].field_values, 0, n_fields * n_devices * field_size_bytes);
	}

	return 0;
	
}

void cleanup_and_exit(int error_code, dcgmHandle_t * dcgmHandle, dcgmGpuGrp_t * groupId, dcgmFieldGrp_t * fieldGroupId){

	// if cleanup was caused by error
	if (error_code != DCGM_ST_OK){
		printf("ERROR: %s\nFreeing Structs And Exiting...\n", errorString(error_code));
	}

	if (fieldGroupId){
		dcgmFieldGroupDestroy(*dcgmHandle, *fieldGroupId);
	}

	if (groupId){
		dcgmGroupDestroy(*dcgmHandle, *groupId);
	}

	if (dcgmHandle){
		dcgmStopEmbedded(*dcgmHandle);
	}

	dcgmShutdown();

	exit(error_code);

}


Samples_Buffer * init_samples_buffer(int n_cpu, int clk_tck, int n_devices, int n_fields, unsigned short * field_ids, unsigned short * field_types, int max_samples){

	Samples_Buffer * samples_buffer = (Samples_Buffer *) malloc(sizeof(Samples_Buffer));
	if (samples_buffer == NULL){
		fprintf(stderr, "Could not allocate memory for samples buffer, exiting...\n");
		return NULL;
	}

	samples_buffer -> n_cpu = n_cpu;
	samples_buffer -> clk_tck = clk_tck;
	samples_buffer -> n_devices = n_devices;
	samples_buffer -> n_fields = n_fields;
	samples_buffer -> field_ids = field_ids;
	samples_buffer -> field_types = field_types;
	samples_buffer -> max_samples = max_samples;
	samples_buffer -> n_samples = 0;
	Sample * samples = (Sample *) malloc(max_samples * sizeof(Sample));
	if (samples == NULL){
		fprintf(stderr, "Could not allocate memory for samples buffer, exiting...\n");
		return NULL;
	}

	// hardcoded because only doubles and i64 field value types
	int field_size_bytes = 8;
	for (int i = 0; i < max_samples; i++){
		Sample my_sample;
		my_sample.field_values = (void *) malloc(n_fields * n_devices * field_size_bytes);
		my_sample.cpu_util = (Proc_Data *) malloc(sizeof(Proc_Data));
		if ((my_sample.field_values == NULL) || (my_sample.cpu_util == NULL)){
			fprintf(stderr, "Could not allocate memory for values in samples buffer, exiting...\n");
			return NULL;
		}

		samples[i] = my_sample;
	}

	samples_buffer -> samples = samples;

	return samples_buffer;

}

void print_usage(){
	const char * usage_str = "Usage: [-f, --fields=<string: comma separated of field ids>] || \
					[-s, --sample_freq_millis=<int>] || \
					[-n, --n_samples_per_buffer=<int: number of samples to hold in-mem before dumping to file>] || \
					[-o, --output_dir=<string: directory to store outputted results]";
	
	printf("%s\n", usage_str);
}

unsigned short * parse_string_to_arr(char * str, int * n_vals){

	char * str_cpy = strdup(str);

	int n_commas = 0;
	int len = strlen(str_cpy);
	for (int i = 0; i < len; i++){
		if (str_cpy[i] == ','){
			n_commas++;
		}
	}

	int size = n_commas + 1;

	*n_vals = size;

	unsigned short * arr = (unsigned short *) malloc(size * sizeof(unsigned short));

	const char * delim = ", ";
	char * token;

	// first token
	token  = strtok(str_cpy, delim);

	int ind = 0;
	while (token != NULL){
		arr[ind] = (unsigned short) atoi(token);
		ind++;
		token = strtok(NULL, delim);
	}

	free(str_cpy);

	return arr; 


}


int main(int argc, char ** argv, char * envp[]){

	// handle command line args

	// deafult args
	int n_fields = 10;
	char * field_ids_string = "203,254,1002,1003,1004,1005,1009,1010,1011,1012";
	int sample_freq_millis = 100;
	int n_samples_per_buffer = 6000;
	// deafult for Della
	char * output_dir = "/scratch/gpfs/as1669/ClusterMonitoring/data";

	

	static struct option long_options[] = {
		{"fields", required_argument, 0, 'f'},
		{"sample_freq_millis", required_argument, 0, 's'},
		{"n_samples_per_buffer", required_argument, 0, 'n'},
		{"output_dir", required_argument, 0, 'o'},
		{0, 0, 0, 0}
	};

	int opt_index = 0;
	int opt;
	while ((opt = getopt_long(argc, argv, "f:s:n:o:", long_options, &opt_index)) != -1){
		switch (opt){
			case 'f': field_ids_string = optarg;
				break;
			case 's': sample_freq_millis = atoi(optarg);
				break;
			case 'n': n_samples_per_buffer = atoi(optarg);
				break;
			case 'o': output_dir = optarg;
				break;
			default: print_usage();
				exit(1);
		}
	}

	// appending hostname to the output directory to store values for this host
	char hostbuffer[256];
	int hostname_ret = gethostname(hostbuffer, sizeof(hostbuffer));
	if (hostname_ret == -1){
		fprintf(stderr, "Could not get hostname, exiting...\n");
		exit(1);
	}

	struct stat st = {0};

	char * true_output_dir;
	asprintf(&true_output_dir, "%s/%s", output_dir, hostbuffer);

	if (stat(true_output_dir, &st) == -1){
		if (mkdir(true_output_dir, 0755) == -1){
			fprintf(stderr, "Could not make directory with hostname, exiting...\n");
			exit(1);
		}
	}

	//printf("True directory: %s\n", true_output_dir);

	// convert the fieldId comma separted string to array
	unsigned short * fieldIds = parse_string_to_arr(field_ids_string, &n_fields);


	/* DCGM SETUP */
	dcgmReturn_t dcgm_ret; 
	dcgm_ret = dcgmInit();

	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "INIT ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, NULL, NULL, NULL);
	}

	dcgmHandle_t dcgmHandle;
	// Start embedded process
	dcgm_ret = dcgmStartEmbedded(DCGM_OPERATION_MODE_MANUAL, &dcgmHandle);

	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "CONNECT ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, NULL, NULL);
	}


	/* READ SYSTEM INFO */

	unsigned int gpuIdList[DCGM_MAX_NUM_DEVICES];
	int n_devices;

	dcgm_ret = dcgmGetAllSupportedDevices(dcgmHandle, gpuIdList, &n_devices);

	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "GET DEVICES ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, NULL, NULL);
	}	

	// no GPUs in system
	if (n_devices == 0){
		fprintf(stderr, "No GPUs in System, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, NULL, NULL);
	}
	//printf("Found %d GPUs\n", n_devices);

	// create group with all devices

	// GROUP_DEFAULT creates group with all entities present on system
	char groupName[] = "MyGroup";
	dcgmGpuGrp_t groupId;
	dcgm_ret = dcgmGroupCreate(dcgmHandle, DCGM_GROUP_DEFAULT, groupName, &groupId);

	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "GROUP CREATE ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, NULL);
	}

	// create field group with all the metrics we want to scan

	dcgmFieldGrp_t fieldGroupId;
	char fieldGroupName[] = "MyFieldGroup";

	/* DEFAULT FIELDS BEING COLLECTED */

	/* 203: COASE GPU UTIL
	 * 254: % Used Frame Buffer in MB
	 * 1002: SM_ACTIVE: Ratio of cycles at least 1 warp assigned to any SM
	 * 1003: SM_OCCUPANCY: Ratio of warps resident to theoretical maximum warps per cycle
	 * 1004: PIPE_TENSOR_ACTIVE: ratio of cycles any tensor pipe is active
	 * 1005: DRAM_ACTIVE: Ratio of cycles device memory interface is sending or receiving data
	 * 1009: PCIe Sent Bytes
	 * 1010: PCIe Recv Bytes
	 * 1011: NVLink Sent Bytes
	 * 1012: NVLink Recv Bytes 
	*/

	// from command line args

	dcgm_ret = dcgmFieldGroupCreate(dcgmHandle, n_fields, fieldIds, fieldGroupName, &fieldGroupId);
	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "FIELD GROUP CREATE ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
	}
	// watch fields by combining device group and field group

	// update every second
	// sample freq millis from command line
	long long update_freq_micros = sample_freq_millis * 1000;

	// don't cache old metrics for more than 1 sec
	double max_keep_seconds = 1;

	int max_keep_samples = n_samples_per_buffer;

	dcgm_ret = dcgmWatchFields(dcgmHandle, groupId, fieldGroupId, update_freq_micros, max_keep_seconds, max_keep_samples);

	if (dcgm_ret != DCGM_ST_OK){
		fprintf(stderr, "WATCH FIELDS ERROR, Exiting...\n");
		cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
	}

	/* FINISHED INIT SETUP FOR DCGM, NOW INIT THE SAMPLE BUFFER TO STORE VALUES*/
	DcgmFieldsInit();

	unsigned short * fieldTypes = (unsigned short *) malloc(n_fields * sizeof(unsigned short));
	dcgm_field_meta_p meta_ptr;
	for (int i = 0 ; i < n_fields; i++){
		meta_ptr = DcgmFieldGetById(fieldIds[i]);
		if (meta_ptr == NULL){
			fprintf(stderr, "Unknown field %d\n", fieldIds[i]);
			print_usage();
			cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
		}
		fieldTypes[i] = (unsigned short) meta_ptr -> fieldType;
	}
	


	int n_cpu = sysconf(_SC_NPROCESSORS_ONLN);
	int clk_tck = sysconf(_SC_CLK_TCK);

	Samples_Buffer * samples_buffer = init_samples_buffer(n_cpu, clk_tck, n_devices, n_fields, fieldIds, fieldTypes, n_samples_per_buffer);
	if (samples_buffer == NULL){
		cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
	}
	
	struct timespec time;
	int n_samples, err;
	Sample * cur_sample;

	Proc_Data * cpu_util;
	Proc_Data * prev_proc_data = NULL;

	// For now, run indefinitely 
	while (true){
		n_samples = samples_buffer -> n_samples;
		clock_gettime(CLOCK_REALTIME, &time);
		cur_sample = &((samples_buffer -> samples)[n_samples]);
		cur_sample -> time = time;
		
		// COLLECT CPU FREE MEM AND COMPUTE %
		cpu_util = process_proc_stat(cur_sample, prev_proc_data);
		cur_sample -> cpu_util = cpu_util;

		// set the previous to be current so as to accurately compute util % next time
		prev_proc_data = cpu_util;


		// COLLECT GPU VALUES
		
		if (PRINT) {
			printf("Time %ld: Collecting Values...\n", time.tv_sec);
		}

		// update fields (and wait for return)
		dcgm_ret = dcgmUpdateAllFields(dcgmHandle, 1);
		if (dcgm_ret != DCGM_ST_OK){
			fprintf(stderr, "UPDATE ALL FIELDS ERROR, Exiting...\n");
                        cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
		}

		// retrieve values
		dcgm_ret = dcgmGetLatestValues(dcgmHandle, groupId, fieldGroupId, &copy_field_values_function, (void *) samples_buffer);
		
		if (dcgm_ret != DCGM_ST_OK){
			fprintf(stderr, "GET LATEST VALUES ERROR, Exiting...\n");
			cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
		}
		
		if (PRINT) {
			if (cpu_util != NULL){
				printf("CPU Stats. Util: %.3f%, Free Mem: %lu\n\nGPU Stats:\n", cur_sample -> cpu_util -> util_pct, cur_sample -> cpu_util -> free_mem);
			}
			else{
				printf("Could not retrieve CPU stats\n");
			}
		}
		
		if (PRINT) {
			void * field_values = cur_sample -> field_values;
			int ind;
			unsigned short fieldId, fieldType;
			for (int gpuId = 0; gpuId < n_devices; gpuId++){
				for (int fieldNum = 0; fieldNum < n_fields; fieldNum++){
					ind = gpuId * n_fields + fieldNum;
					fieldId = fieldIds[fieldNum];
					fieldType = fieldTypes[fieldNum];
					switch (fieldType) {
						case DCGM_FT_DOUBLE:
							printf("GPU ID: %d, Field ID: %u, Value: %f\n", gpuId, fieldId, ((double *) field_values)[ind]);
							break;
						case DCGM_FT_INT64:
							printf("GPU ID: %d, Field ID: %u, Value: %li\n", gpuId, fieldId, ((long *) field_values)[ind]);
							break;
						case DCGM_FT_TIMESTAMP:
							printf("GPU ID: %d, Field ID: %u, Value: %li\n", gpuId, fieldId, ((long *) field_values)[ind]);
							break;
						default:
							printf("Error in Field Value Types...");
							printf("GPU ID: %d, Field ID: %u\n", gpuId, fieldId);
							cleanup_and_exit(dcgm_ret, &dcgmHandle, &groupId, &fieldGroupId);
							break;
					}
				}
				printf("\n");
			}
		}
		
		
		n_samples++;
		samples_buffer -> n_samples = n_samples;
		// SAVING VALUES
		if (n_samples == n_samples_per_buffer){
			err = dump_samples_buffer(samples_buffer, true_output_dir);
			if (err == -1){
				fprintf(stderr, "Error dumping buffer to file. Skipping this dump and collecting new data...\n");
			}
			samples_buffer -> n_samples = 0;
		}
		usleep(update_freq_micros);
	}

	// shouldn't reach this point because inifinte loop collecting data
	// free's field value memory in this funciton
	dump_samples_buffer(samples_buffer, true_output_dir);
	
	// string allocated by asprintf
	free(true_output_dir);

	// destroy the buffer
	free(fieldIds);
	free(fieldTypes);
	free(samples_buffer -> samples);
	free(samples_buffer);
	
	// AT END
	cleanup_and_exit(DCGM_ST_OK, &dcgmHandle, &groupId, &fieldGroupId);

}
