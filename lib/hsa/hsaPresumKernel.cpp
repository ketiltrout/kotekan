#include "hsaPresumKernel.hpp"

REGISTER_HSA_COMMAND(hsaPresumKernel);

hsaPresumKernel::hsaPresumKernel(
                            Config& config, const string &unique_name,
                            bufferContainer& host_buffers, hsaDeviceInterface& device) :
    hsaCommand("CHIME_presum","presum.hsaco", config, unique_name, host_buffers, device){
    command_type = CommandType::KERNEL;

    _num_elements = config.get_int(unique_name, "num_elements");
    _num_local_freq = config.get_int(unique_name, "num_local_freq");
    _samples_per_data_set = config.get_int(unique_name, "samples_per_data_set");
    input_frame_len = _num_elements * _num_local_freq * _samples_per_data_set;
    presum_len = _num_elements * _num_local_freq * 2 * sizeof (int32_t);

    //pre-allocate GPU memory
    device.get_gpu_memory_array("input", 0, input_frame_len);
    device.get_gpu_memory_array("presum", 0, presum_len);
}

hsaPresumKernel::~hsaPresumKernel() {

}

hsa_signal_t hsaPresumKernel::execute(int gpu_frame_id, const uint64_t& fpga_seq, hsa_signal_t precede_signal) {

    // Set kernel args
    struct __attribute__ ((aligned(16))) args_t {
        void *input_buffer;
        void *mystery;
        int constant;
        void *presum_buffer;
    } args;

    memset(&args, 0, sizeof(args));

    args.input_buffer = device.get_gpu_memory_array("input", gpu_frame_id, input_frame_len);
    args.mystery = NULL;
    args.constant = _num_elements/4;//global_x size
    args.presum_buffer = device.get_gpu_memory_array("presum", gpu_frame_id, presum_len);

    // Copy kernel args into correct location for GPU
    memcpy(kernel_args[gpu_frame_id], &args, sizeof(args));

    // Set kernel dims
    kernelParams params;
    params.workgroup_size_x = 64;
    params.workgroup_size_y = 1;
    params.workgroup_size_z = 1;
    params.grid_size_x = _num_elements/4;
    params.grid_size_y = _samples_per_data_set/N_PRESUM;
    params.grid_size_z = 1;
    params.num_dims = 2;

    // Should this be zero?
    params.private_segment_size = 0;
    params.group_segment_size = 0;

    signals[gpu_frame_id] = enqueue_kernel(params, gpu_frame_id);

    return signals[gpu_frame_id];
}
