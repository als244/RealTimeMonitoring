#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sqlite3.h>

void insert_row_to_db(sqlite3 * db, char * hostname, long timestamp_ms, long device_id, long field_id, long value){

	char * insert_statement;
	
	asprintf(&insert_statement, "INSERT INTO Data (hostname,timestamp_ms,device_id,field_id,value) VALUES ('%s', %ld, %ld, %ld, %ld);", hostname, timestamp_ms, device_id, field_id, value);

	char *sqlErr;
	int sql_ret = sqlite3_exec(db, insert_statement, NULL, NULL, &sqlErr);
	
	free(insert_statement);

	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "SQL error: %s\n", sqlErr);
		sqlite3_free(sqlErr);
	}
	return;
}

sqlite3 * init_db(char * db_filepath){

	sqlite3 *db;

	int sql_ret;
	sql_ret = sqlite3_open(db_filepath, &db);

	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "COULD NOT OPEN SQL DB, Exiting...\n");
		return NULL;
	}

	char * create_table_cmd = "CREATE TABLE IF NOT EXISTS Data (hostname TEXT, timestamp_ms INTEGER, device_id INTEGER, field_id INTEGER, value INTEGER);";
	char * sqlErr;

	sql_ret = sqlite3_exec(db, create_table_cmd, NULL, NULL, &sqlErr);
	if (sql_ret != SQLITE_OK){
		fprintf(stderr, "SQL Error: %s\n", sqlErr);
		return NULL;
	}

	return db;
}

int main(int argc, char *argv[]){


	char * db_name = "/scratch/gpfs/as1669/ClusterMonitoring/data/all_data.db";

	sqlite3 * db = init_db(db_name);

	if (db == NULL){
		fprintf(stderr, "COULD NOT CREATE DB...\n");
		exit(1);
	}



	char * csv_filename = "/scratch/gpfs/as1669/ClusterMonitoring/aggregatedData/clean_data.csv";

	FILE * fp = fopen(csv_filename, "r");

	if (fp == NULL){
		fprintf(stderr, "COULD NOT READ CSV...\n");
		exit(1);
	}


	char * line_buffer = NULL;
	size_t len = 0;
	ssize_t n_read;	

	char hostname[256];
	double value_raw;
	long timestamp_ms, field_id, device_id, value;
	int n_success;

	long total_lines = 0;
	long db_rows = 0;

	struct timespec start, end;
	long elapsed_time_ms;
	clock_gettime(CLOCK_REALTIME, &start);

	// EXPLICITY START DB TRANSACTION SO IT DOESN't AUTO COMMIT
	sqlite3_exec(db, "BEGIN", 0, 0, 0);	

	while ((n_read = getline(&line_buffer, &len, fp)) != -1) {
        	
		/*
		printf("Line buffer:\n");
		printf("%s\n\n", line_buffer);
		*/

        	n_success = sscanf(line_buffer, "%[^,],%ld,%ld,%ld,%lg", hostname, &timestamp_ms, &device_id, &field_id, &value_raw);
    		
		/*	
		printf("N success: %d\n", n_success);
		printf("Hostname: %s\n", hostname);
		printf("Timestamp ms: %ld\n", timestamp_ms);
		printf("Device id: %ld\n", device_id);
		printf("Field id: %ld\n", field_id);
		printf("Value raw: %lg\n", value_raw);
		*/

		// bad line, so continue
    		if (n_success != 5){
    			total_lines += 1;
    			continue;
    		}

    		// specify which field id's should be mulitplied by 100 and converted
    		switch (field_id){
    			// gpu memory %
    			case 254:
    				value = (long) round(value_raw * 100);
    				break;
    			// sm activity initally a fraction 0-1
    			case 1002:
    				value = (long) round(value_raw * 100);
    				break;
    			// occupancy initially a fraction 0-1
    			case 1003:
    				value = (long) round(value_raw * 100);
    				break;
    			// tensor activity a fraction 0-1
    			case 1004:
    				value = (long) round(value_raw * 100);
    				break;
    			// DRAM active a fraction 0-1
    			case 1005:
    				value = (long) round(value_raw * 100);
    				break;
    			default:
    				value = (long) round(value_raw);
    				break;
    		}

		// ROUGH ERROR HANDLING IN CASE NOT CAUGHT BY SCANF
        	if ((timestamp_ms < 1700000000000) || (timestamp_ms > 1800000000000)){
                	total_lines += 1;
			continue;
        	}
        	if ((device_id < -1) || (device_id > 32)){
			total_lines += 1;
                	continue;
        	}
        	if ((field_id < 0) || (field_id > 1020)){
                	total_lines += 1;
			continue;
        	}


		// PERFORM DB INSERTION!
    		insert_row_to_db(db, hostname, timestamp_ms, device_id, field_id, value);
    		
		db_rows += 1;
    		total_lines += 1;

    		if ((total_lines % 10000000) == 0){
			clock_gettime(CLOCK_REALTIME, &end);
    			printf("Total lines: %ld\n", total_lines);
    			printf("DB Rows: %ld\n", db_rows);
			elapsed_time_ms = (((end.tv_sec - start.tv_sec) * 1e9) + (end.tv_nsec - start.tv_nsec)) / 1e6;
			printf("10 M elasped time: %ld ms\n\n", elapsed_time_ms);
			// reset timer
			clock_gettime(CLOCK_REALTIME, &start);
    		}

    	
    		if ((db_rows % 10000000) == 0){
  			// EXPLICITY COMMIT AFTER 100 million rows
			sqlite3_exec(db, "COMMIT", 0, 0, 0);
			// BEGIN NEW TRANSACTION
			sqlite3_exec(db, "BEGIN", 0, 0, 0);		
    		}
    	}

	// if there are remaining rows uncommited, commit
    	sqlite3_exec(db, "COMMIT", 0, 0, 0);	

    	free(line_buffer);

}
