/*****************************************
@file
@brief Processes for splitting and subsetting visibility data by frequency.
- freqSplit : public KotekanProcess
- freqSubset : public KotekanProcess

*****************************************/
#ifndef FREQ_SLICER_HPP
#define FREQ_SLICER_HPP

#include <unistd.h>
#include "buffer.h"
#include "KotekanProcess.hpp"


/**
 * @class freqSplit
 * @brief Separate a visBuffer stream into two by selecting frequencies in the upper and lower half of the band.
 *
 * This task takes data coming out of a visBuffer stream and separates it into
 * two streams. It selects which frames to copy to which buffer by assigning
 * frequencies in the upper and lower half of the CHIME band to different buffer
 * streams.
 *
 * @par Buffers
 * @buffer in_buf The buffer to be split
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 * @buffer out_bufs The two buffers containing the respective upper or lower band frequencies
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 *
 * @todo Generalise to arbitary frequency splits.
 * @author Mateus Fandino
 */
class freqSplit : public KotekanProcess {

public:

    // Default constructor
    freqSplit(Config &config,
              const string& unique_name,
              bufferContainer &buffer_container);

    void apply_config(uint64_t fpga_seq);

    // Main loop for the process
    void main_thread();

private:
    // Vector of the buffers we are using and their current frame ids.
    std::vector<std::pair<Buffer*, unsigned int>> out_bufs;
    Buffer * in_buf;
};



/**
 * @class freqSubset
 * @brief Outputs a visBuffer stream with a subset of the input frequencies.
 *
 * This task takes data coming out of a visBuffer stream and selects a subset of
 * frequencies to be passed on to the output buffer.
 *
 * @par Buffers
 * @buffer in_buf The original buffer with all frequencies
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 * @buffer out_buf The buffer containing the subset of frequencies
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 *
 * @conf  subset_list       Vector of Int. The list of frequencies that go
 *                          in the subset.
 *
 * @author Mateus Fandino
 */
class freqSubset : public KotekanProcess {

public:

    /// Default constructor
    freqSubset(Config &config,
               const string& unique_name,
               bufferContainer &buffer_container);

    void apply_config(uint64_t fpga_seq);

    /// Main loop for the process
    void main_thread();

private:
    // List of frequencies for the subset
    std::vector<uint32_t> subset_list;

    /// Output buffer with subset of frequencies
    Buffer * out_buf;
    /// Input buffer with all frequencies
    Buffer * in_buf;
};


#endif