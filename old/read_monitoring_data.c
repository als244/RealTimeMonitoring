#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "dcgm_agent.h"
#include "dcgm_fields.h"
#include "dcgm_structs.h"

#include "monitoring.h"


int collect_data_from_host(char * data_dir_path, char * hostname, char * output_filepath){


	// open output file that data will be dumped to
	// make sure append because ALL hosts will append to this file
	FILE * output_file = fopen(output_filepath, "a");
	if (output_file == NULL){
		fprintf(stderr, "Could not open output_file: %s. Returning...\n", output_filepath);
		return -1;
	}


	// read timestamps file that has list of timestamps with assoicated buffers
	char * filename;

	asprintf(&filename, "%s/%s/timestamps.txt", data_dir_path, hostname);

	FILE * timestamps_file = fopen(filename, "r");

	if (timestamps_file == NULL){
		fprintf(stderr, "Could not open timestamps file: %s. Returning...\n", filename);
		free(filename);
		return -1;
	}

	free(filename);


	// iterate through all buffers and dump data

	char * line = NULL;
    size_t len = 0;
    ssize_t read;

    long timestamp;

    FILE * metadata_file, * data_file;

    int n_cpu, clk_tck, n_devices, n_fields, n_samples;
    unsigned short * fieldTypes, * fieldIds;

    struct timespec time;
    void * fieldValues;

    // hardcoded, from "monitoring.c"
    int field_size_bytes = 8;

    while ((read = getline(&line, &len, timestamps_file)) != -1) {
    	timestamp = atol(line);


    	// Gather metadata
    	
    	asprintf(&filename, "%s/%s/%li.meta", data_dir_path, hostname, timestamp);

    	metadata_file = fopen(filename, "rb");
	// if buffers were flushed and timestamp file not refreshed...
	// expected
    	if (metadata_file == NULL){
    		fprintf(stderr, "Could not open metadata file: %s. Continuing...\n", filename);
    		free(filename);
    		continue;
    	}
    	free(filename);

    	// ordering here same as in the "gpu_monitoring.c" file
    	// would be cleaner to group as single struct, but still ok
    	fread(&n_cpu, sizeof(int), 1, metadata_file);
    	fread(&clk_tck, sizeof(int), 1, metadata_file);
    	fread(&n_devices, sizeof(int), 1, metadata_file);
    	fread(&n_fields, sizeof(int), 1, metadata_file);
    	fread(&n_samples, sizeof(int), 1, metadata_file);

    	fieldTypes = (unsigned short *) malloc(n_fields * sizeof(unsigned short));
    	fieldIds = (unsigned short *) malloc(n_fields * sizeof(unsigned short));
    	
    	fread(fieldTypes, sizeof(unsigned short), n_fields, metadata_file);
    	fread(fieldIds, sizeof(unsigned short), n_fields, metadata_file);

    	fclose(metadata_file);

    	// Gather data

    	asprintf(&filename, "%s/%s/%li.buffer", data_dir_path, hostname, timestamp);

    	data_file = fopen(filename, "rb");
    	if (data_file == NULL){
    		fprintf(stderr, "Could not open samples buffer file: %s. Continuing...\n", filename);
    		free(filename);
    		continue;
    	}
    	free(filename);

    	long time_ms;
    	unsigned short fieldId, fieldType;
    	int ind;


    	Proc_Data * cpu_data = malloc(sizeof(Proc_Data));

    	for (int i = 0; i < n_samples; i++){
    		fread(&time, sizeof(struct timespec), 1, data_file);
    		time_ms = time.tv_sec * 1e3 + time.tv_nsec / 1e6;

    		// CPU memory and util 
    		fread(&(cpu_data -> free_mem), sizeof(unsigned long), 1, data_file);
    		fread(&(cpu_data -> util_pct), sizeof(double), 1, data_file);
		// consistent format with gpu fields
		// assign gpuId to be -1 for CPU and declare fieldId=0 for cpuFreeMem and 1 for cpuUtilPct
    		fprintf(output_file, "%s,%li,-1,0,%lu\n", hostname, time_ms, cpu_data -> free_mem);
		fprintf(output_file, "%s,%li,-1,1,%f\n",hostname,time_ms, cpu_data -> util_pct);


    		// GPU Stuff
    		fieldValues = (void *) malloc(n_fields * n_devices * field_size_bytes);

    		fread(fieldValues, 1, n_fields * n_devices * field_size_bytes, data_file);

    		for (int gpuId = 0; gpuId < n_devices; gpuId++){
    			for (int fieldNum = 0; fieldNum < n_fields; fieldNum++){
    				ind = gpuId * n_fields + fieldNum;
    				fieldId = fieldIds[fieldNum];
					fieldType = fieldTypes[fieldNum];
					switch (fieldType) {
						case DCGM_FT_DOUBLE:
							fprintf(output_file, "%s,%li,%d,%u,%f\n", hostname, time_ms, gpuId, fieldId, ((double *) fieldValues)[ind]);
							break;
						case DCGM_FT_INT64:
							fprintf(output_file, "%s,%li,%d,%u,%li\n", hostname, time_ms, gpuId, fieldId, ((long *) fieldValues)[ind]);
							break;
						case DCGM_FT_TIMESTAMP:
							fprintf(output_file, "%s,%li,%d,%u,%li\n", hostname, time_ms, gpuId, fieldId, ((long *) fieldValues)[ind]);
							break;
						default:
							break;
				
    				}
    			}
    		}
    		free(fieldValues);

    	}
    	free(cpu_data);
    	free(fieldTypes);
    	free(fieldIds);


    	fclose(data_file);
    }

    // after all timestamps/buffers have been processed

    if (line){
    	free(line);
    }

	fclose(timestamps_file);
	fclose(output_file);

	return 0;

}

int main(int argc, char ** argv, char * envp[]){

    char * data_dir = "/scratch/gpfs/as1669/ClusterMonitoring/data";
    
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);

    char * output_filepath;   
    asprintf(&output_filepath, "/scratch/gpfs/as1669/ClusterMonitoring/aggregatedData/%ld.csv", time.tv_sec);

    DIR *dr = opendir(data_dir); 
  
    if (dr == NULL)  // opendir returns NULL if couldn't open directory 
    { 
        fprintf(stderr, "Could not open current directory\n"); 
        exit(1); 
    } 
  
    // Refer http://pubs.opengroup.org/onlinepubs/7990989775/xsh/readdir.html 
    // for readdir()
    struct dirent * host_dirs;

    char * host_dir_path;
    while ((host_dirs = readdir(dr)) != NULL) {
    	if (!strcmp (host_dirs->d_name, "."))
            continue;
        if (!strcmp (host_dirs->d_name, ".."))    
            continue;

        // assume that there will be subdirectories with hostnames
        if (host_dirs -> d_type == DT_DIR){
        	host_dir_path = host_dirs -> d_name;
		printf("Collecting data from host: %s\n", host_dir_path);
        	collect_data_from_host(data_dir, host_dir_path, output_filepath);
        }
    }

    printf("Finished writing to: %s\n", output_filepath);
  
    closedir(dr);     
    return 0; 
}
