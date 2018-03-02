#include "hsaBeamformPulsar.hpp"

REGISTER_HSA_COMMAND(hsaBeamformPulsar);

hsaBeamformPulsar::hsaBeamformPulsar(Config& config, const string &unique_name,
                            bufferContainer& host_buffers, hsaDeviceInterface& device) :
    hsaCommand("pulsarbf", "pulsar_beamformer.hsaco", config, unique_name, host_buffers, device) {
    command_type = CommandType::KERNEL;

    _num_elements = config.get_int(unique_name, "num_elements");
    _num_pulsar = config.get_int(unique_name, "num_pulsar");
    _samples_per_data_set = config.get_int(unique_name, "samples_per_data_set");
    _num_pol = config.get_int(unique_name, "num_pol");

    input_frame_len = _num_elements * _samples_per_data_set;
    output_frame_len =  _samples_per_data_set * _num_pulsar * _num_pol *  sizeof(uint8_t);

    phase_len = _num_elements*_num_pulsar*2*sizeof(float);
    host_phase = (float *)hsa_host_malloc(phase_len);

    int index = 0;
    for (int b=0; b < _num_pulsar; b++){
      for (int n=0; n<_num_elements; n++){
        host_phase[index++] = b/10.;
        host_phase[index++] = b/10.;
      }
    }

    void * device_phase = device.get_gpu_memory("beamform_phase", phase_len);
    device.sync_copy_host_to_gpu(device_phase, (void *)host_phase, phase_len);
}

hsaBeamformPulsar::~hsaBeamformPulsar() {
    hsa_host_free(host_phase);
}

hsa_signal_t hsaBeamformPulsar::execute(int gpu_frame_id, const uint64_t& fpga_seq, hsa_signal_t precede_signal) {

    struct __attribute__ ((aligned(16))) args_t {
        void *input_buffer;
        void *phase_buffer;
        void *output_buffer;
    } args;
    memset(&args, 0, sizeof(args));
    args.input_buffer = device.get_gpu_memory_array("input", gpu_frame_id, input_frame_len);
    args.phase_buffer = device.get_gpu_memory("beamform_phase", phase_len);
    args.output_buffer = device.get_gpu_memory_array("bf_psr_output", gpu_frame_id, output_frame_len);

    // Allocate the kernel argument buffer from the correct region.
    memcpy(kernel_args[gpu_frame_id], &args, sizeof(args));


    kernelParams params;
    params.workgroup_size_x = 256;
    params.workgroup_size_y = 1;
    params.workgroup_size_z = 1;
    params.grid_size_x = 512;
    params.grid_size_y = _num_pulsar;
    params.grid_size_z = _samples_per_data_set/32;
    params.num_dims = 3;

    params.private_segment_size = 0;
    params.group_segment_size = 2048;

    signals[gpu_frame_id] = enqueue_kernel(params, gpu_frame_id);

    return signals[gpu_frame_id];
}

