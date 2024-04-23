
typedef struct Proc_Data {
	unsigned long free_mem;
	double util_pct;
	// will be used to compute the next util_pct
	long total_time;
	long idle_time;
} Proc_Data;

typedef struct interface_totals {
	int n_ib_ifs;
	char ** ib_ifs;
	int n_eth_ifs;
	char ** eth_ifs;
	// THESE ARE CUMULATIVE TOTALS
        //      - each sample will record the difference and save most recent value
        //      - raw values from /sys/class/net/<ifname>/statistics
        unsigned long total_ib_rx_bytes;
        unsigned long total_ib_tx_bytes;
        unsigned long total_eth_rx_bytes;
        unsigned long total_eth_tx_bytes;
} Interface_Totals;

typedef struct net_data {
	// These are populated for the values to record in database
	//	- calculated from difference of this total - prev total
	unsigned long ib_rx_bytes;
	unsigned long ib_tx_bytes;
	unsigned long eth_rx_bytes;
	unsigned long eth_tx_bytes;
} Net_Data;


typedef struct sample {
	struct timespec time;
	void * field_values;
	Proc_Data * cpu_util;
	Net_Data * net_util;
} Sample;

typedef struct samples_buffer {
	int n_cpu;
	int clk_tck;
	int n_devices;
	int n_fields;
	unsigned short * field_ids;
	unsigned short * field_types;
	Interface_Totals * interface_totals;
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


