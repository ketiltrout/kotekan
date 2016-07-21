#ifndef CORRELATOR_KERNEL_H
#define CORRELATOR_KERNEL_H

#include "gpu_command.h"
#include "device_interface.h"

class correlator_kernel: public gpu_command
{
public:
    correlator_kernel();
    correlator_kernel(char* param_gpuKernel, char* param_name);
    ~correlator_kernel();
    virtual void build(Config* param_Config, class device_interface& param_Device);
    virtual cl_event execute(int param_bufferID, class device_interface &param_Device, cl_event param_PrecedeEvent);
protected:
    void defineOutputDataMap(Config* param_Config, int param_num_blocks,class device_interface& param_Device);
    cl_mem device_block_lock;
    cl_int *zeros;

    //Host Buffers
    cl_mem id_x_map;
    cl_mem id_y_map;
};

#endif
