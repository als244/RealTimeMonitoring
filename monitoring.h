#inlcude "job_stats.h"

typedef struct Proc_Data {
	unsigned long free_mem;
	double util_pct;
	// will be used to compute the next util_pct
	long total_time;
	long idle_time;
} Proc_Data;


typedef struct sample {
	struct timespec time;
	void * field_values;
	Proc_Data * cpu_util;
} Sample;

typedef struct samples_buffer {
	int n_cpu;
	int clk_tck;
	int n_devices;
	int n_fields;
	unsigned short * field_ids;
	unsigned short * field_types;
	int max_samples;
	int n_samples;
	Sample * samples;
} Samples_Buffer;


// used to collect values from fscanf from /proc/stat
typedef struct Cpu_stat {
	int cpu_id;
	unsigned long t_user;
	unsigned long t_nice;
    unsigned long t_system;
    unsigned long t_idle;
    unsigned long t_iowait;
    unsigned long t_irq;
    unsigned long t_softirq;
} Cpu_stat;


