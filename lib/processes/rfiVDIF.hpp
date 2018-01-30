/**
 * @file rfiVDIF.hpp
 * @brief Contains a general VDIF kurtosis estimator kotekan process.
 * - rfiVDIF : public KotekanProcess
 */

#ifndef VDIF_RFI_H
#define VDIF_RFI_H

#include "buffer.h"
#include "KotekanProcess.hpp"
#include "vdif_functions.h"

/**
 * @class rfiVDIF
 * @brief Producer and consumer ``KotekanProcess`` which consumes input VDIF data, 
 * computes spectral kurtosis estimates and places them into a ``Buffer``.
 * 
 * This process is a spectral kurtosis estimator that works on any general kotekan buffer containing
 * VDIF data. This process move block by block through the VDIF data while computing and integrating 
 * power estimates. Once the desired integration length is over, the process does one of two things
 * (as specified by the user):
 *
 * 1) The process combines the sums across the element axis and kurtosis values are calculated on the new sum
 *
 * 2) The process computes kurtosis values for each frequency-element pair
 *
 * There are advantages to both options, however the first is currently heavily favoured by other processes.
 *
 
 * @par Buffers
 * @buffer vdif_in The kotekan buffer which conatins input VDIF data
 *	@buffer_format	Array of bytes (uint8_t) which conatin VDIF header and data
 *	@buffer_metadata chimeMetadata
 * @buffer rfi_out The kotekan buffer to be filled with kurtosis estimates
 *	@buffer_format Array of floats
 *	@buffer_metadata none
 *
 * @conf RFI_combined   Bool, a flag indicating whether or not to combine data inputs
 * @conf sk_step        Int, the number of timesteps per kurtosis estimate
 *
 * @author Jacob Taylor
 */


class rfiVDIF : public KotekanProcess {
public:
    //Constructor, initializes class, sets up config
    rfiVDIF(Config &config,
                   const string& unique_name,
                   bufferContainer &buffer_containter);

    //Deconstructor, cleans up, does nothing
    ~rfiVDIF();

    //Read kotekan config into internal parameters
    void apply_config(uint64_t fpga_seq) override;

    //Main thread: reads vdif_in buffer, computes kurtosis values, fills rfi_out buffer
    void main_thread();

private:
    //Kotekan Buffer for VDIF input
    struct Buffer *buf_in;
    //Kotekan Buffer for kurtosis output
    struct Buffer *buf_out;

    //General config Paramaters
    //Number of elements in the input data
    uint32_t num_elements;
    //Number of total frequencies in the input data
    uint32_t num_frequencies;
    //Number of timesteps in a frame of input data
    uint32_t num_timesteps;

    //RFI config parameters
    //Flag for whether or not to combine inputs in kurtosis estimates
    bool COMBINED;
    //Number of timesteps per kurtosis value
    int SK_STEP;
};

#endif
