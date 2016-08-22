#include "networkOutputSim.hpp"
#include "buffers.h"
#include "errors.h"
#include "test_data_generation.h"
#include "error_correction.h"
#include "nt_memcpy.h"
#include "config.h"
#include "util.h"
#include "time_tracking.h"

#include <dirent.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <memory.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <inttypes.h>
#include <functional>

networkOutputSim::networkOutputSim(struct Config &config_,
                     struct Buffer &buf_,
                     int num_links_in_group_,
                     int link_id_,
                     int pattern_,
                     int stream_id_) :
                  KotekanProcess(config_, std::bind(&networkOutputSim::main_thread, this)),
                  buf(buf_),
                  num_links_in_group(num_links_in_group_),
                  link_id(link_id_),
                  pattern(pattern_),
                  stream_id(stream_id_) {
}

networkOutputSim::~networkOutputSim() {}

void networkOutputSim::main_thread() {

    int buffer_id = link_id;
    int data_id = 0;
    uint64_t fpga_seq_num = 0;
    int constant = 9;

    for (EVER) {
        wait_for_empty_buffer(&buf, buffer_id);

        if ((fpga_seq_num / config.processing.samples_per_data_set) % 2 == 0) {
            constant = 10;
        } else {
            constant = 9;
        }

        set_data_ID(&buf, buffer_id, data_id++);
        set_stream_ID(&buf, buffer_id, stream_id);
        set_fpga_seq_num(&buf, buffer_id, fpga_seq_num);
        struct timeval now;
        gettimeofday(&now, NULL);
        set_first_packet_recv_time(&buf, buffer_id, now);

        // TODO perfect place for lambdas here.
        if (pattern == SIM_CONSTANT) {
            //INFO("Generating a constant data set all (1,1).");
            generate_const_data_set(constant, constant,
                                config.processing.samples_per_data_set,
                                config.processing.num_local_freq,
                                config.processing.num_elements,
                                buf.data[buffer_id]);
        } else if (pattern == SIM_FULL_RANGE) {
            //INFO("Generating a full range of all possible values.");
            generate_full_range_data_set(0,
                                config.processing.samples_per_data_set,
                                config.processing.num_local_freq,
                                config.processing.num_elements,
                                buf->data[buffer_id]);
        } else if (pattern == SIM_SINE) {
            stream_id_t stream_id;
            stream_id.link_id = stream_id;
            //INFO("Generating data with a complex sine in frequency.");
            generate_complex_sine_data_set(stream_id,
                                config.processing.samples_per_data_set,
                                config.processing.num_local_freq,
                                config.processing.num_elements,
                                buf.data[buffer_id]);
        } else {
            ERROR("Invalid Pattern");
            exit(-1);
        }

        mark_buffer_full(&buf, buffer_id);

        buffer_id = (buffer_id + num_links_in_group) % (buf.num_buffers);

        fpga_seq_num += config.processing.samples_per_data_set;

    }

    mark_producer_done(&buf, link_id);
    int ret = 0;
    pthread_exit((void *) &ret);

}