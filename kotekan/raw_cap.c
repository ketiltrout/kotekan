#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "raw_cap.h"
#include "util.h"
#include "errors.h"
#include "network_dpdk.h"
#include "raw_file_write.h"

// This function is very much a hack to make life easier, but it should be replaced with something better
void copy_gains(char * base_dir, char * data_set) {
    char src[100];  // The source gains file
    char dest[100]; // The dist for the gains file copy

    snprintf(src, 100, "/home/squirrel/ch_acq/gains_slotNone.pkl");
    snprintf(dest, 100, "%s/%s/gains_slotNone.pkl", base_dir, data_set);

    if (cp(dest, src) != 0) {
        fprintf(stderr, "Could not copy %s to %s\n", src, dest);
    } else {
        printf("Copied gains.pkl from %s to %s\n", src, dest);
    }
}

int raw_cap(struct Config * config) {

    // Some constants
    const int num_elements = 2;
    const int buffer_depth = 5;
    const int vdif_header_len = 32;
    const int bit_depth = 4;

    // Compute the data set name.
    char data_time[64];
    char data_set[150];
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = gmtime(&rawtime);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int j = 4; j < 8; j++)
        CPU_SET(j, &cpuset);

    strftime(data_time, sizeof(data_time), "%Y%m%dT%H%M%SZ", timeinfo);
    snprintf(data_set, sizeof(data_set), "%s_%s_raw", data_time, config->raw_cap.instrument_name);

    if (config->raw_cap.write_packets) {
        make_raw_dirs(config->raw_cap.disk_base,
                config->raw_cap.disk_set,
                data_set,
                config->raw_cap.num_disks);

        for (int i = 0; i < config->raw_cap.num_disks; ++i) {
            char disk_base_dir[256];
            snprintf(disk_base_dir, sizeof(disk_base_dir), "%s/%s/%d/",
                    config->raw_cap.disk_base, config->raw_cap.disk_set, i);
            copy_gains(disk_base_dir, data_set);
        }

        //  ** Create settings file **
        FILE * info_file = fopen("settings.txt", "w");

        if(!info_file) {
            ERROR("Error creating info file: settings.txt\n");
            exit(-1);
        }

        int data_format_version = 3;

        fprintf(info_file, "format_version_number=%02d\n", data_format_version);
        fprintf(info_file, "num_freq=%d\n", config->processing.num_total_freq);
        fprintf(info_file, "num_inputs=%d\n", num_elements);
        fprintf(info_file, "num_frames=%d\n", 1); // Always one for this VDIF
        fprintf(info_file, "num_timesamples=%d\n", config->raw_cap.samples_per_file);
        fprintf(info_file, "header_len=%d\n", vdif_header_len); // VDIF
        fprintf(info_file, "packet_len=%d\n", vdif_header_len + config->processing.num_total_freq); // 1056 for VDIF with 1024 freq
        fprintf(info_file, "offset=%d\n", 0);
        fprintf(info_file, "data_bits=%d\n", bit_depth);
        fprintf(info_file, "stride=%d\n", 1);
        fprintf(info_file, "stream_id=n/a\n");
        fprintf(info_file, "note=\"%s\"\n", config->raw_cap.note);
        fprintf(info_file, "start_time=%s\n", data_time);
        fprintf(info_file, "num_disks=%d\n", config->raw_cap.num_disks);
        fprintf(info_file, "disk_set=%s\n", config->raw_cap.disk_set);
        fprintf(info_file, "# Warning: The start time is when the program starts it, the time recorded in the packets is more accurate\n");

        fclose(info_file);

        INFO("Created meta data file: settings.txt\n");

        for (int i = 0; i < config->raw_cap.num_disks; ++i) {
            char to_file[256];
            snprintf(to_file, sizeof(to_file), "%s/%s/%d/%s/settings.txt",
                    config->raw_cap.disk_base, config->raw_cap.disk_set, i, data_set);
            int err = cp(to_file, "settings.txt");
            if (err != 0) {
                ERROR("could not copy settings");
                exit(err);
            }
        }
    }

    // Setup the network output buffer (only one for this case)
    struct Buffer vdif_buf;
    struct InfoObjectPool pool;

    const int num_vdif_buf = buffer_depth * config->raw_cap.num_disks;
    const int buffer_len = ( config->processing.num_total_freq + vdif_header_len ) *
                            num_elements * config->raw_cap.samples_per_file;

    // We don't really need a pool, but buffer needs one.
    create_info_pool(&pool, num_vdif_buf * 2, 1, 1);

    create_buffer(&vdif_buf, num_vdif_buf,
            buffer_len, config->fpga_network.num_links,
            1, &pool, "vdif_buffer");

    for (int i = 0; i < num_vdif_buf; ++i) {
        zero_buffer(&vdif_buf, i);
    }

    // We always need the network threads
    INFO("Starting up network threads...");

    // Create network threads
    pthread_t network_dpdk_t;
    struct networkDPDKArg network_dpdk_args;

    for (int i = 0; i < config->fpga_network.num_links; ++i) {
        network_dpdk_args.num_links_in_group[i] = num_links_in_group(config, i);
        network_dpdk_args.link_id[i] = config->fpga_network.link_map[i].link_id;
    }
    network_dpdk_args.buf = NULL;
    network_dpdk_args.vdif_buf = &vdif_buf;
    network_dpdk_args.num_links = config->fpga_network.num_links;
    network_dpdk_args.config = config;
    network_dpdk_args.num_lcores = 4;
    network_dpdk_args.num_links_per_lcore = 2;
    network_dpdk_args.port_offset[0] = 0;
    network_dpdk_args.port_offset[1] = 2;
    network_dpdk_args.port_offset[2] = 4;
    network_dpdk_args.port_offset[3] = 6;

    CHECK_ERROR( pthread_create(&network_dpdk_t, NULL, &network_dpdk_thread,
                                (void *)&network_dpdk_args ) );

    // Create the file writing threads.
    pthread_t file_write_t[config->raw_cap.num_disks];
    struct raw_file_write_thread_arg file_write_args[config->raw_cap.num_disks];

    if (config->raw_cap.write_packets == 1) {
        for (int i = 0; i < config->raw_cap.num_disks; ++i) {
            file_write_args[i].buf = &vdif_buf;
            file_write_args[i].diskID = i;
            file_write_args[i].numDisks = config->raw_cap.num_disks;
            file_write_args[i].bufferDepth = buffer_depth;
            file_write_args[i].dataset_name = data_set;
            file_write_args[i].disk_base = config->raw_cap.disk_base;
            file_write_args[i].disk_set = config->raw_cap.disk_set;
            CHECK_ERROR( pthread_create(&file_write_t[i], NULL, (void *) &raw_file_write_thread, (void *)&file_write_args[i] ) );
            CHECK_ERROR( pthread_setaffinity_np(file_write_t[i], sizeof(cpu_set_t), &cpuset) );
        }
    }

    // Just block on the network thread for now.
    int ret;
    CHECK_ERROR( pthread_join(network_dpdk_t, (void **) &ret) );

    return 0;
}
