#ifndef CH_ACQ_UPLINK
#define CH_ACQ_UPLINK

struct ch_acqUplinkThreadArg {
    struct Buffer * buf;
    int num_links;
    int buffer_depth;
    char * ch_acq_ip_addr;
    int ch_acq_port_num;
    int num_data_sets;
    int num_timesamples;
    int timesamples_per_packet;

    int actual_num_freq;
    int actual_num_elements;

    int total_num_freq;
};


void ch_acq_uplink_thread(void * arg);

#endif