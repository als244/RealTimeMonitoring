typedef struct Proc_Data {
	unsigned long free_mem;
	double util_pct;
	// will be used to compute the next util_pct
	long total_time;
	long idle_time;
} Proc_Data;


typedef struct Network_stat
{
	unsigned long long int rx_bytes;
	unsigned long long int tx_bytes;
	unsigned long long int rx_packets;
	unsigned long long int tx_packets;
	unsigned long long int rx_errors;
	unsigned long long int tx_errors;
	unsigned long long int rx_dropped;
	unsigned long long int tx_dropped;
	unsigned long long int rx_compressed;
	unsigned long long int tx_compressed;
} Network_stat;

struct NetworkUtilStatFiles 
{
	char **statfile_paths;
	int n_statfiles;
};

typedef struct sample {
	struct timespec time;
	void * field_values;
	Proc_Data * cpu_util;
	Network_stat network_stat;
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


