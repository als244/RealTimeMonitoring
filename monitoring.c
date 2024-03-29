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
#include <math.h>

#include <sqlite3.h>

#include "dcgm_agent.h"
#include "dcgm_fields.h"
#include "dcgm_structs.h"


#include "monitoring.h"


#define PRINT 0

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

void insert_row_to_db(sqlite3 * db, long timestamp_ms, long device_id, long field_id, long value){

	char * insert_statement;

	asprintf(&insert_statement, "INSERT INTO Data (timestamp,device_id,field_id,value) VALUES (%ld, %ld, %ld, %ld);", timestamp_ms, device_id, field_id, value);

	char *sqlErr;

	int sql_ret = sqlite3_exec(db, insert_statement, NULL, NULL, &sqlErr);
	
	free(insert_statement);

	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "SQL error: %s\n", sqlErr);
		sqlite3_free(sqlErr);
	}
	return;
}


int dump_samples_buffer(Samples_Buffer * samples_buffer, sqlite3 * db){

	int n_fields = samples_buffer -> n_fields;
	int n_devices = samples_buffer -> n_devices;

	// hardcoded, but could also look at field_types field in sample struct
	int field_size_bytes = 8;

	int n_samples = samples_buffer -> n_samples;
	unsigned short * fieldIds = samples_buffer -> field_ids;
	unsigned short * fieldTypes = samples_buffer -> field_types;

	Sample * samples = samples_buffer -> samples;

	// Saving Data
	Proc_Data * cpu_data;
	void * fieldValues;
	Sample data;

	unsigned short fieldId, fieldType;
	long ind, time_ns;

	long val;
	// insert timestamp and field values for every sample
	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);

	// EXPLICITY START DB TRANSACTION SO IT DOESN't AUTO COMMIT
	sqlite3_exec(db, "BEGIN", 0, 0, 0);	
	

	for (int i = 0; i < n_samples; i++){

		data = samples[i];
		time_ns = data.time.tv_sec * 1e9 + data.time.tv_nsec;

		// CPU dump
		cpu_data = data.cpu_util;
		insert_row_to_db(db, time_ns, -1, 0, cpu_data -> free_mem);
		insert_row_to_db(db, time_ns, -1, 1, round(cpu_data -> util_pct));
		
		// GPU Field dump
		fieldValues = data.field_values;

		for (int gpuId = 0; gpuId < n_devices; gpuId++){
    		for (int fieldNum = 0; fieldNum < n_fields; fieldNum++){
    			ind = gpuId * n_fields + fieldNum;
    			fieldId = fieldIds[fieldNum];
				fieldType = fieldTypes[fieldNum];
				switch (fieldType) {
					case DCGM_FT_DOUBLE:
						// all the doubles are fractions 0-1, we instead represent as int 0-100
						val = (long) round(((double *) fieldValues)[ind] * 100);
						break;
					case DCGM_FT_INT64:
						val =  (((long *) fieldValues)[ind]);
						break;
					case DCGM_FT_TIMESTAMP:
						val = (((long *) fieldValues)[ind]);
						break;
					default:
						val = 0;
						break;
    			}
    			insert_row_to_db(db, time_ns, gpuId, fieldId, val);
    		}
    	}
	}
	
	// EXPLICITY COMMIT TRANSACTION
	sqlite3_exec(db, "COMMIT", 0, 0, 0);

	clock_gettime(CLOCK_REALTIME, &end);

	long elapsed_time_ns = ((end.tv_sec - start.tv_sec) * 1e9) + (end.tv_nsec - start.tv_nsec);
	long elapsed_time_ms = elapsed_time_ns / 1e6;
	printf("Elasped time of dump: %lu ms\n", elapsed_time_ms);
	fflush(stdout);


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
	if ((error_code != -1) && (error_code != DCGM_ST_OK)){
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
	// location where the per-host databases are 
	char * output_dir = "/scratch/gpfs/as1669/ClusterMonitoring/data/node_utilizations";

	

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
	 * 254: % Used Frame Buffer
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

	sqlite3 *db;

	char * db_filename;
	asprintf(&db_filename, "%s/%s_metrics.db", output_dir, hostbuffer);
	
	int sql_ret;
	sql_ret = sqlite3_open(db_filename, &db);
	free(db_filename);

	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "COULD NOT OPEN SQL DB, Exiting...\n");
		cleanup_and_exit(-1, &dcgmHandle, &groupId, &fieldGroupId);
	}

	char * create_table_cmd = "CREATE TABLE IF NOT EXISTS Data (timestamp INT, device_id INT, field_id INT, value INT);";
	char * sqlErr;

	sql_ret = sqlite3_exec(db, create_table_cmd, NULL, NULL, &sqlErr);
	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "SQL Error: %s\n", sqlErr);
		cleanup_and_exit(-1, &dcgmHandle, &groupId, &fieldGroupId);
	}
	
	long time_sec;
        long prev_job_collection_time = 0;

	// For now, run indefinitely 
	while (true){
		n_samples = samples_buffer -> n_samples;
		clock_gettime(CLOCK_REALTIME, &time);

		// CHECK TO SEE IF IT HAS BEEN AN HOUR SINCE LAST JOB STATUS QUERY
                // IF SO, CALL PYTHON SCRIPT TO COLLECT INFO FROM SACCT AND DUMP TO DIFFERENT DB
                time_sec = time.tv_sec;
                if ((time_sec - prev_job_collection_time) > (60 * 60)){
                        system("python /home/as1669/RealTimeMonitoring/scripts/dump_job_details.py &");
                        prev_job_collection_time = time_sec;
                }

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
				printf("CPU Stats. Util: %d, Free Mem: %d\n\nGPU Stats:\n", (int) round(cur_sample -> cpu_util -> util_pct), (int) (cur_sample -> cpu_util -> free_mem));
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
							printf("GPU ID: %d, Field ID: %u, Value: %d\n", gpuId, fieldId, (int) round((((double *) field_values)[ind] * 100)));
							break;
						case DCGM_FT_INT64:
							printf("GPU ID: %d, Field ID: %u, Value: %d\n", gpuId, fieldId, (int) (((long *) field_values)[ind]));
							break;
						case DCGM_FT_TIMESTAMP:
							printf("GPU ID: %d, Field ID: %u, Value: %d\n", gpuId, fieldId, (int) (((long *) field_values)[ind]));
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
			err = dump_samples_buffer(samples_buffer, db);
			if (err == -1){
				fprintf(stderr, "Error dumping buffer to file. Skipping this dump and collecting new data...\n");
			}
			samples_buffer -> n_samples = 0;
		}
		usleep(update_freq_micros);
	}

	// shouldn't reach this point because inifinte loop collecting data
	// free's field value memory in this funciton
	dump_samples_buffer(samples_buffer, db);

	// destroy the buffer
	free(fieldIds);
	free(fieldTypes);
	free(samples_buffer -> samples);
	free(samples_buffer);
	
	// AT END
	cleanup_and_exit(DCGM_ST_OK, &dcgmHandle, &groupId, &fieldGroupId);

}
