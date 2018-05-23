#include "clOutputDataZero.hpp"

REGISTER_CL_COMMAND(clOutputDataZero);

clOutputDataZero::clOutputDataZero(Config& config, const string &unique_name,
                            bufferContainer& host_buffers, clDeviceInterface& device) :
    clCommand("", "", config, unique_name, host_buffers, device)
{
    _num_elements = config.get_int(unique_name, "num_elements");
    _num_local_freq = config.get_int(unique_name, "num_local_freq");
    _block_size = config.get_int(unique_name, "block_size");
    _num_data_sets = config.get_int(unique_name, "num_data_sets");
    _num_blocks = config.get_int(unique_name,"num_blocks");

    output_len = _num_local_freq * _num_blocks * (_block_size*_block_size) * 2 * _num_data_sets  * sizeof(int32_t);
    output_zeros = malloc(output_len);
    memset(output_zeros, 0, output_len);
}

clOutputDataZero::~clOutputDataZero()
{
    free(output_zeros);
}

cl_event clOutputDataZero::execute(int gpu_frame_id, const uint64_t& fpga_seq, cl_event pre_event)
{
    DEBUG2("CLOUTPUTDATAZERO::EXECUTE");

    clCommand::execute(gpu_frame_id, 0, pre_event);

    cl_mem gpu_memory_frame = device.get_gpu_memory_array("output",
                                                gpu_frame_id, output_len);

    // Data transfer to GPU
    CHECK_CL_ERROR( clEnqueueWriteBuffer(device.getQueue(0),
                                            gpu_memory_frame,
                                            CL_FALSE,
                                            0, //offset
                                            output_len,
                                            output_zeros,
                                            1,
                                            &pre_event,
                                            &post_event[gpu_frame_id]) );
    return post_event[gpu_frame_id];
}
