#ifndef ICE_BOARD_SHUFFLE_HPP
#define ICE_BOARD_SHUFFLE_HPP

#include "iceBoardHandler.hpp"
#include "prometheusMetrics.hpp"
#include "packet_copy.h"
#include "util.h"
#include "chimeMetadata.h"
#include "gpsTime.h"
#include "buffer.h"

class iceBoardShuffle : public iceBoardHandler {
public:

    iceBoardShuffle(Config &config, const std::string &unique_name,
                    bufferContainer &buffer_container, int port);

    virtual int handle_packet(struct rte_mbuf *mbuf);

    virtual void update_stats();

protected:

    /**
     * @brief Advances the @c shuffle_size output frames, and the lost sample frame
     *
     * This function is used to move the system to the next set of output frames.
     * It updates the active frame pointers, and also fills the metadata for the
     * new frame; including GPS/System time, and FPGA seq number/streamID.
     *
     * @param new_seq The seq of the start of this new frame.
     * @param first_time Default false.  Set to true if we are setting up the first frame for start up.
     * @return true if the frame was advanced.  false if the system is exiting, and there are no new frames.
     */
    bool advance_frames(uint64_t new_seq, bool first_time = false);

    void copy_packet_shuffle(struct rte_mbuf *mbuf);

    void handle_lost_samples(int64_t lost_samples);

    bool check_fpga_shuffle_flags(struct rte_mbuf *mbuf);

    static const uint32_t shuffle_size = 4;
    struct Buffer * out_bufs[shuffle_size];
    uint8_t * out_buf_frame[shuffle_size];
    struct Buffer * lost_samples_buf;
    uint8_t * lost_samples_frame;
    int lost_samples_frame_id = 0;
    int out_buf_frame_ids[shuffle_size] = {0};

    // Error counter for each of the 16 lanes of the 2nd stage (within-crate) data shuffle.
    uint64_t fpga_second_stage_shuffle_errors[16] = {0};

    // Error counter for each of the 8 lanes of the 3rd stage (between-crate) data shuffle.
    uint64_t fpga_third_stage_shuffle_errors[8] {0};

    // Tracks the number of times at least one of the flags in the second or
    // thrid stage shuffle were set.
    uint64_t rx_shuffle_flags_set = 0;
};

iceBoardShuffle::iceBoardShuffle(Config &config, const std::string &unique_name,
                    bufferContainer &buffer_container, int port) :
    iceBoardHandler(config, unique_name, buffer_container, port) {

    WARN("iceBoardHandler: %s", unique_name.c_str());

    std::vector<std::string> buffer_names = config.get_string_array(unique_name, "out_bufs");
    if (shuffle_size != buffer_names.size()) {
        throw std::runtime_error("Expecting 4 buffers, got " + std::to_string(port));
    }
    for (uint32_t i = 0; i < shuffle_size; ++i) {
        out_bufs[i] = buffer_container.get_buffer(buffer_names[i]);
        register_producer(out_bufs[i], unique_name.c_str());
    }

    lost_samples_buf = buffer_container.get_buffer(config.get_string(unique_name, "lost_samples_buf"));
    register_producer(lost_samples_buf, unique_name.c_str());
    // We want to make sure the flag buffers are zeroed between uses.
    zero_frames(lost_samples_buf);

    std::string endpoint_name = unique_name + "/port_data";
    restServer::instance().register_get_callback(endpoint_name, [&] (connectionInstance &conn) {
        json info;

        info["fpga_stream_id"] = {{"crate", port_stream_id.crate_id},
                             {"slot", port_stream_id.slot_id},
                             {"link", port_stream_id.link_id}};
        info["lost_packets"] = rx_lost_samples_total / samples_per_packet;
        info["lost_samples"] = rx_lost_samples_total;
        vector<uint64_t> second_stage_errors;
        second_stage_errors.assign(fpga_second_stage_shuffle_errors, fpga_second_stage_shuffle_errors + 16);
        info["fpga_second_stage_shuffle_errors"] = second_stage_errors;
        vector<uint64_t> third_stage_errors;
        third_stage_errors.assign(fpga_third_stage_shuffle_errors, fpga_third_stage_shuffle_errors + 8);
        info["fpga_thrid_stage_shuffle_errors"] = third_stage_errors;

        info["shuffle_flags_set"] = rx_shuffle_flags_set;

        info["chksum_errors"] = rx_crc_errors_total;
        info["out_of_order_errors"] = rx_out_of_order_errors_total;

        // This is the total number of errors from all sources other than missed packets
        // i.e. natural packet loss.
        info["errors_total"] = rx_errors_total;

        info["nic_port"] = port;

        vector<uint32_t> freq_bins;
        vector<float> freq_mhz;
        stream_id_t temp_stream_id = port_stream_id;
        temp_stream_id.crate_id = port_stream_id.crate_id % 2;
        for (uint32_t i = 0; i < shuffle_size; ++i) {
            temp_stream_id.unused = i;
            freq_bins.push_back(bin_number_chime(&temp_stream_id));
            freq_mhz.push_back(freq_from_bin(bin_number_chime(&temp_stream_id)));
        }

        info["freq_bins"] = freq_bins;
        info["freq_mhz"] = freq_mhz;

        conn.send_json_reply(info);
    });

}

inline int iceBoardShuffle::handle_packet(struct rte_mbuf *mbuf) {

    if (!iceBoardHandler::check_packet(mbuf))
        return 0;

    if (unlikely(!got_first_packet)) {
        if (unlikely(!iceBoardHandler::align_first_packet(mbuf))) {
            return 0;
        } else {
            // Get the first set of buffer frames to write into.
            // We use last seq in case there are missing frames,
            // we want to start at the alignment point.
            // See align_first_packet for details.
            if (!advance_frames(last_seq, true))
                return 0; // If for some reason we exit at this stange drop the packet
        }
    } else {
        cur_seq = iceBoardHandler::get_mbuf_seq_num(mbuf);
    }

    // Check footers
    //iceBoardShuffle::check_fpga_shuffle_flags(mbuf);
    if (unlikely(!iceBoardShuffle::check_fpga_shuffle_flags(mbuf)))
        return 0;

    int64_t diff = iceBoardHandler::get_packet_diff();

    if (unlikely(!iceBoardHandler::check_order(diff)))
        return 0;

    if (unlikely(!iceBoardHandler::check_for_reset(diff)))
        return -1;

    // Handle lost packets
    // Note this handles packets for all loss reasons,
    // because we don't update the last_seq number value if the
    // packet isn't accepted for any reason.
    if (unlikely(diff > samples_per_packet))
        iceBoardShuffle::handle_lost_samples(diff - samples_per_packet);

    // copy packet
    iceBoardShuffle::copy_packet_shuffle(mbuf);

    last_seq = cur_seq;

    return 0;
}

inline bool iceBoardShuffle::advance_frames(uint64_t new_seq, bool first_time) {
    struct timeval now;
    gettimeofday(&now, NULL);

    for (uint32_t i = 0; i < shuffle_size; ++i) {
        if (!first_time) {
            mark_frame_full(out_bufs[i], unique_name.c_str(), out_buf_frame_ids[i]);

            // Advance frame ID
            out_buf_frame_ids[i] = (out_buf_frame_ids[i] + 1) % out_bufs[i]->num_frames;
        }

        out_buf_frame[i] = wait_for_empty_frame(out_bufs[i], unique_name.c_str(), out_buf_frame_ids[i]);
        if (out_buf_frame[i] == NULL)
            return false;

        allocate_new_metadata_object(out_bufs[i], out_buf_frame_ids[i]);

        set_first_packet_recv_time(out_bufs[i], out_buf_frame_ids[i], now);

        if (is_gps_global_time_set() == 1) {
            struct timespec gps_time = compute_gps_time(new_seq);
            set_gps_time(out_bufs[i], out_buf_frame_ids[i], gps_time);
        }

        // We take the stream ID only from the first pair of crates,
        // to avoid overwriting it on different ports.
        // This makes the stream ID unique for down stream processes.
        if (port_stream_id.crate_id / 2 == 0) {
            stream_id_t tmp_stream_id = port_stream_id;
            // Set the unused flag to store the post shuffle freq bin number.
            tmp_stream_id.unused = i;
            set_stream_id_t(out_bufs[i], out_buf_frame_ids[i], tmp_stream_id);
        }

        set_fpga_seq_num(out_bufs[i], out_buf_frame_ids[i], new_seq);
    }

    if (!first_time) {
        mark_frame_full(lost_samples_buf, unique_name.c_str(), lost_samples_frame_id);
        lost_samples_frame_id = (lost_samples_frame_id + 1) % lost_samples_buf->num_frames;
    }
    lost_samples_frame = wait_for_empty_frame(lost_samples_buf, unique_name.c_str(), lost_samples_frame_id);
    if (lost_samples_frame == NULL)
        return false;
    return true;
}

inline void iceBoardShuffle::handle_lost_samples(int64_t lost_samples) {

    // By design all the seq numbers for all frames should be the same here.
    int64_t lost_sample_location = last_seq + samples_per_packet
                                    - get_fpga_seq_num(out_bufs[0], out_buf_frame_ids[0]);
    uint64_t temp_seq = last_seq + samples_per_packet;

    // TODO this could be made more efficent by breaking it down into blocks of memsets.
    while (lost_samples > 0) {
        // TODO this assumes the frame size of all the output buffers are the
        // same, which should be true in all cases, but should still be tested
        // elsewhere.
        if (unlikely(lost_sample_location * sample_size == out_bufs[0]->frame_size)) {
            advance_frames(temp_seq);
            lost_sample_location = 0;
        }

        // This sets the flag to zero this sample with the zeroSamples process.
        // NOTE: I thought about using a bit field for this array, but doing so
        // opens up a huge number of problems getting the bit set atomically in
        // a way that's also efficent.  By using a byte array with values of either
        // 0 or 1 then just setting the value to 1 avoids any syncronization issues.
        // The linux function set_bit() might have worked (since DPDK is linux/x86 only),
        // but then there are endianness issues if reading the array as a uint8_t *
        // This might be less memory size efficent, but it's much easier to work with.
        // NOTE: This also introduces cache line contension since we are using one array
        // to for all 4 links, ideally we might use 4 arrays and a reduce operation to bring
        // it down to one on another core.
        // WARN("port %d, adding lost packets at: %d", port, lost_sample_location);
        lost_samples_frame[lost_sample_location] = 1;
        lost_sample_location += 1;
        lost_samples -= 1;
        rx_lost_samples_total += 1;
        temp_seq += 1;
    }
}

inline void iceBoardShuffle::copy_packet_shuffle(struct rte_mbuf *mbuf) {

    const int sample_size = 2048;
    const int sub_sample_size = sample_size / shuffle_size;

    // Where in the buf frame we should write sample.
    // TODO by construction this value should be the same for all
    // frames, but that should be proven carefully...
    int64_t sample_location;

    sample_location = cur_seq - get_fpga_seq_num(out_bufs[0], out_buf_frame_ids[0]);
    assert(sample_location * sample_size <= out_bufs[0]->frame_size);
    if (unlikely(sample_location * sample_size == out_bufs[0]->frame_size)) {
        // If there are no new frames to fill, we are just dropping the packet
        if (!advance_frames(cur_seq))
            return;
        sample_location = 0;
    }

    // Where to place each of the 512 element blocks based on which crate they
    // are coming from.
    int sub_sample_pos = port_stream_id.crate_id / 2;

    // Initial packet offset, advances with each call to copy_block.
    int pkt_offset = header_offset;

    // Copy the packet in packet memory order.
    for (uint32_t sample_id = 0; sample_id < samples_per_packet; ++sample_id) {

        for (uint32_t sub_sample_freq = 0; sub_sample_freq < shuffle_size; ++sub_sample_freq) {
            uint64_t copy_location = (sample_location + sample_id) * sample_size
                                     + sub_sample_pos * sub_sample_size;

            copy_block(&mbuf,
                       (uint8_t *) &out_buf_frame[sub_sample_freq][copy_location],
                       sub_sample_size,
                       &pkt_offset);
        }
    }
}

inline bool iceBoardShuffle::check_fpga_shuffle_flags(struct rte_mbuf *mbuf) {

    const int flag_len = 4; // 32-bits = 4 bytes
    const int rounding_factor = 2;

    // Go to the last part of the packet
    // Note this assumes that the footer doesn't cross two mbuf
    // segment, but based on the packet design this should never happen.
    while(mbuf->next != NULL) {
        mbuf = mbuf->next;
    }

    int cur_mbuf_len = mbuf->data_len;
    assert(cur_mbuf_len >= flag_len);
    int flag_location = cur_mbuf_len - flag_len - rounding_factor;
    assert (2048 * 2 + flag_location == 4922); // Make sure the flag address is correct.
    const uint8_t * mbuf_data = rte_pktmbuf_mtod_offset(mbuf, uint8_t *, cur_mbuf_len - flag_len - rounding_factor);

    uint32_t flag_value = *(uint32_t *)mbuf_data;

    // If no flags are set then we hvae no errors to check,
    // so we accept the packet
    if (flag_value == 0) {
        return true;
    }

    // The 32 bits of the flag contain:
    // - The most significant 16 bits indicate an error for each of the 16 lanes
    //   of the 2nd stage (within-crate) data shuffle.
	// - The middle 8 bits are always 0.
	// - The least significant 8 bits indicate an error for each of the 8 lanes
    //   of the 3rd stage (between-crate) data shuffle.
    // The FPGA sends data in Little-endian byte order, so the operation below works
    // only on systems which are little-endian.  Therefore this code is not portiable.

    int i;
    for (i = 0; i < 8; ++i) {
        fpga_third_stage_shuffle_errors[i] += (flag_value >> i) & 1;
    }

    for (i = 0; i < 16; ++i) {
        fpga_second_stage_shuffle_errors[i] += (flag_value >> (16 + i)) & 1;
    }

    // One of the flags was set, so let's not process this packet.
    rx_shuffle_flags_set += 1;
    rx_errors_total += 1;

    return false;
}

void iceBoardShuffle::update_stats() {
    iceBoardHandler::update_stats();

    prometheusMetrics &metrics = prometheusMetrics::instance();

    std::string tags = "port=\"" + std::to_string(port) + "\"";

    for (int i = 0; i < 8; ++i) {
        metrics.add_process_metric("kotekan_dpdk_shuffle_fpga_third_stage_shuffle_errors_total",
                                    unique_name,
                                    fpga_third_stage_shuffle_errors[i],
                                    tags + ",fpga_lane=\"" + std::to_string(i) + "\"");
    }
    for (int i = 0; i < 16; ++i) {
        metrics.add_process_metric("kotekan_dpdk_shuffle_fpga_second_stage_shuffle_errors_total",
                                    unique_name,
                                    fpga_second_stage_shuffle_errors[i],
                                    tags + ",fpga_lane=\"" + std::to_string(i) + "\"");
    }
}

#endif