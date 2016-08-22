#ifndef NETWORK_OUTPUT_SIM
#define NETWORK_OUTPUT_SIM

#define SIM_CONSTANT   0
#define SIM_FULL_RANGE 1
#define SIM_SINE        2

#include "buffers.h"
#include "errors.h"
#include "KotekanProcess.hpp"

class networkOutputSim : public KotekanProcess {
public:
    networkOutputSim(struct Config &config,
                     struct Buffer &buf,
                     int num_links_in_group,
                     int link_id,
                     int pattern,
                     int stream_id);
    virtual ~networkOutputSim();
    void main_thread();
private:
    struct Buffer * buf;
    int num_links_in_group;
    int link_id;
    int pattern;
    int stream_id;
};

#endif