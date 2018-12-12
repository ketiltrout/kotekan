/*****************************************
@file
@brief Remove the eigenvalues/vectors from a buffer
- removeEv : public KotekanProcess
*****************************************/
#ifndef REMOVE_EV_HPP
#define REMOVE_EV_HPP

#include "buffer.h"
#include "KotekanProcess.hpp"
#include "visUtil.hpp"
#include "datasetManager.hpp"

/**
 * @class removeEv
 * @brief Remove any eigenvalues/vectors from a buffer.
 *
 * @par Buffers
 * @buffer in_buf Input buffer with eigensector.
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 * @buffer out_buf Output buffer without eigensector.
 *         @buffer_format visBuffer structured
 *         @buffer_metadata visMetadata
 *
 * @author Richard Shaw
 */
class removeEv : public KotekanProcess {

public:
    removeEv(Config& config,
             const string& unique_name,
             bufferContainer &buffer_container);

    void main_thread() override;

private:

    dset_id_t change_dataset_state(dset_id_t input_dset_id);

    Buffer* in_buf;
    Buffer* out_buf;

    state_id_t ev_state_id;
    dset_id_t input_dset_id = 0;

};

#endif