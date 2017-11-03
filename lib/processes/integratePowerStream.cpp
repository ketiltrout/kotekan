#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <functional>
#include <string>

#include "integratePowerStream.hpp"
#include "util.h"
#include "errors.h"

integratePowerStream::integratePowerStream(Config& config,
                                       const string& unique_name,
                                       bufferContainer &buffer_container) :
    KotekanProcess(config, unique_name, buffer_container,
                   std::bind(&integratePowerStream::main_thread, this))
    {

    buf_in  = get_buffer("power_in_buf");
    buf_out = get_buffer("power_out_buf");
    register_consumer(buf_in, unique_name.c_str());
    register_producer(buf_out,  unique_name.c_str());

    integration_length = config.get_int(unique_name, "integration");
}

integratePowerStream::~integratePowerStream() {
}

void integratePowerStream::apply_config(uint64_t fpga_seq) {
}

void integratePowerStream::main_thread() {
    int buf_in_id = 0;
    uint8_t * in_frame = NULL;
    int buf_out_id = 0;
    uint8_t * out_frame = NULL;
    uint packet_length = freqs * sizeof(float) + sizeof(IntensityPacketHeader);
    uint packets_per_buffer = buf_in->frame_size / packet_length;

    void *packet_in = malloc(packet_length);
        IntensityPacketHeader *packet_header = (IntensityPacketHeader *)packet_in;
        float *data_in = (float*)((char *)packet_in + sizeof(IntensityPacketHeader));

    int *integrated_samples = (int*)calloc(elems,sizeof(int));
    void *accum_buffer = malloc(packet_length * elems);
        IntensityPacketHeader *accum_header;
        float *data_out;

    //IntensityPacketHeader *header_in;
    while(!stop_thread) {
        in_frame = wait_for_full_frame(buf_in, unique_name.c_str(), buf_in_id);
        if (in_frame == NULL) break;

        for (uint i = 0; i < packets_per_buffer; i++){
            memcpy(packet_in,
                    in_frame+packet_length*i,
                    packet_length);
            int e = packet_header->elem_idx;

            integrated_samples[e]++;
            data_out = (float*)((char *)accum_buffer + e*packet_length + sizeof(IntensityPacketHeader));
            for (int f=0; f<freqs; f++) data_out[f] += data_in[f];
            accum_header = (IntensityPacketHeader *)((char*)accum_buffer+e*packet_length);
            accum_header->samples_summed += packet_header->samples_summed;

            if (integrated_samples[e] >= integration_length) {
//                INFO("Integrated sample! %i", integrated_samples[e]);
                integrated_samples[e]=0;
                out_frame = wait_for_empty_frame(buf_out, unique_name.c_str(), buf_out_id);
                if (out_frame == NULL) goto end_loop;

                memcpy(out_frame,(char*)accum_buffer+e*packet_length,packet_length);

                mark_frame_full(buf_out, unique_name.c_str(), buf_out_id);
                buf_out_id = (buf_out_id + 1) % buf_out->num_frames;
                memset((char*)accum_buffer+e*packet_length,0,packet_length);
            }
        }
        mark_frame_empty(buf_in, unique_name.c_str(), buf_in_id);
        buf_in_id = (buf_in_id + 1) % buf_in->num_frames;
    }
    end_loop:;
}