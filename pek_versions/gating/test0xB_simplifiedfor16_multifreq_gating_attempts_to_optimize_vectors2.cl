//CASPER ordering
//version 0xB: attempting to clean up and make readable
//vectorizing mad24 operations
//comparable to version 4 for speed

//#pragma OPENCL EXTENSION cl_amd_printf : enable

//HARDCODE ALL OF THESE!
//NUM_ELEMENTS, NUM_FREQUENCIES, NUM_BLOCKS defined at compile time
//#define NUM_ELEMENTS                                32u // 2560u eventually //minimum 32
//#define NUM_FREQUENCIES                             128u
//#define NUM_BLOCKS                                  1u  // N(N+1)/2 where N=(NUM_ELEMENTS/32)

// #define NUM_DATASETS    128 
// // -D ACTUAL_NUM_ELEMENTS=16u 
// // -D ACTUAL_NUM_FREQUENCIES=128u 
// #define NUM_ELEMENTS    32u 
// #define NUM_FREQUENCIES 64u 
// #define NUM_BLOCKS      1u 


#define NUM_ELEMENTS_div_4                          (NUM_ELEMENTS/4u)  // N/4
#define _256_x_NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES (256u*NUM_ELEMENTS_div_4*NUM_FREQUENCIES)
#define NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES        (NUM_ELEMENTS_div_4*NUM_FREQUENCIES)
#define NUM_BLOCKS_x_2048                           (2048u) //each block size is 32 x 32 x 2 = 2048

#define LOCAL_SIZE                                  8u
#define BLOCK_DIM_div_4                             8u
#define N_TIME_CHUNKS_LOCAL                         256u

#define FREQUENCY_BAND                              (get_group_id(1))
#define TIME_DIV_256                                (get_global_id(2)/NUM_BLOCKS)
//#define BLOCK_ID                                    (get_global_id(2)%NUM_BLOCKS)
#define LOCAL_X                                     (get_local_id(0))
#define LOCAL_Y                                     (get_local_id(1))

#define TIME_FOR_256_TIMESTEPS                      65536u //time will be calculated using fixed point math. 1/800E6 * 2048 samples *256 steps = 0.65536 ms
                                                    //this unfortunately puts the base time in units of 10 ns, but this means that things will work in integer groups...
//number of data sets should be passed in as a define generated by the c code.

__kernel __attribute__((reqd_work_group_size(LOCAL_SIZE, LOCAL_SIZE, 1)))
void corr ( __global const uint *packed,
            __global  int *corr_buf,
            //__global uint *id_x_map,
            //__global uint *id_y_map,
            __local  uint *stillPacked,
            const unsigned int offset_remainder,
            const unsigned int delta_time_bin)
{
    //uint block_x = id_x_map[BLOCK_ID]; //column of output block
    //uint block_y = id_y_map[BLOCK_ID]; //row of output block  //if NUM_BLOCKS = 1, then BLOCK_ID = 0 then block_x = block_y = 0

    uint addr_x = ( (/*BLOCK_DIM_div_4*block_x +*/ LOCAL_X)
                    + TIME_DIV_256 * _256_x_NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES
                    + FREQUENCY_BAND*NUM_ELEMENTS_div_4);
    uint addr_y = ( (/*BLOCK_DIM_div_4*block_y +*/ LOCAL_Y)
                    + LOCAL_X*NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES /*shift local_x timesteps ahead*/
                    + TIME_DIV_256 * _256_x_NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES
                    + FREQUENCY_BAND*NUM_ELEMENTS_div_4);


    uint2 corr_a=(uint2)(0,0);
    uint2 corr_b=(uint2)(0,0);
    uint2 corr_c=(uint2)(0,0);
    uint2 corr_d=(uint2)(0,0);
    uint2 corr_e=(uint2)(0,0);
    uint2 corr_f=(uint2)(0,0);
    uint2 corr_g=(uint2)(0,0);
    uint2 corr_h=(uint2)(0,0);
    uint2 corr_a2=(uint2)(0,0);
    uint2 corr_b2=(uint2)(0,0);
    uint2 corr_c2=(uint2)(0,0);
    uint2 corr_d2=(uint2)(0,0);
    uint2 corr_e2=(uint2)(0,0);
    uint2 corr_f2=(uint2)(0,0);
    uint2 corr_g2=(uint2)(0,0);
    uint2 corr_h2=(uint2)(0,0);
    ///TODO continue conversion to shorter vectors from here on down.  
    /// vectors 8 long: 80 vgpr, 34 ms
    ///         4 long: 84 vgpr, 29.3 ms
    ///         2 long: 84 vgpr, 29.1 ms
    //uint4 corr_temp;

    uint4 temp_stillPacked;//=(uint4)(0,0,0,0);
    uint2 temp_pa;

//#pragma unroll 1
    for (uint i = 0; i < N_TIME_CHUNKS_LOCAL; i += LOCAL_SIZE){ //256 is a number of timesteps to do a local accum before saving to global memory
        uint pa=packed[i * NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES + addr_y ];
        uint la=(LOCAL_X*32u + LOCAL_Y*4u);

        barrier(CLK_LOCAL_MEM_FENCE); //store into local memory as a group
        stillPacked[la]   = ((pa & 0x000000f0) << 12u) | ((pa & 0x0000000f) >>  0u); //packed real and imaginary
        stillPacked[la+1u] = ((pa & 0x0000f000) <<  4u) | ((pa & 0x00000f00) >>  8u);
        stillPacked[la+2u] = ((pa & 0x00f00000) >>  4u) | ((pa & 0x000f0000) >> 16u);
        stillPacked[la+3u] = ((pa & 0xf0000000) >> 12u) | ((pa & 0x0f000000) >> 24u);
        barrier(CLK_LOCAL_MEM_FENCE);

//#pragma unroll 8
        for (uint j=0; j< LOCAL_SIZE; j++){
            temp_stillPacked = vload4(j*8u + LOCAL_Y, stillPacked);

            pa = packed[addr_x + (i+j)*NUM_ELEMENTS_div_4_x_NUM_FREQUENCIES ];

            temp_pa.s0 = (pa >> 4u)  & 0xf; //real
            temp_pa.s1 = (pa >> 0u)  & 0xf; //imag

            corr_a = mad24(temp_pa, temp_stillPacked.s00, corr_a);
            corr_b = mad24(temp_pa, temp_stillPacked.s11, corr_b);
            corr_c = mad24(temp_pa, temp_stillPacked.s22, corr_c);
            corr_d = mad24(temp_pa, temp_stillPacked.s33, corr_d);


            temp_pa.s0 = (pa >> 12u) & 0xf; //real
            temp_pa.s1 = (pa >> 8u)  & 0xf; //imag

            corr_a2 = mad24(temp_pa, temp_stillPacked.s00, corr_a2);
            corr_b2 = mad24(temp_pa, temp_stillPacked.s11, corr_b2);
            corr_c2 = mad24(temp_pa, temp_stillPacked.s22, corr_c2);
            corr_d2 = mad24(temp_pa, temp_stillPacked.s33, corr_d2);


            temp_pa.s0 = (pa >> 20u) & 0xf;
            temp_pa.s1 = (pa >> 16u) & 0xf;

            corr_e = mad24(temp_pa, temp_stillPacked.s00, corr_e);
            corr_f = mad24(temp_pa, temp_stillPacked.s11, corr_f);
            corr_g = mad24(temp_pa, temp_stillPacked.s22, corr_g);
            corr_h = mad24(temp_pa, temp_stillPacked.s33, corr_h);

            temp_pa.s0 = (pa >> 28u) & 0xf;
            temp_pa.s1 = (pa >> 24u) & 0xf;

            corr_e2 = mad24(temp_pa, temp_stillPacked.s00, corr_e2);
            corr_f2 = mad24(temp_pa, temp_stillPacked.s11, corr_f2);
            corr_g2 = mad24(temp_pa, temp_stillPacked.s22, corr_g2);
            corr_h2 = mad24(temp_pa, temp_stillPacked.s33, corr_h2);
        }
    }
    //output: 32 numbers--> 16 pairs of real/imag numbers 
    //16 pairs * 8 (local_size(0)) * 8 (local_size(1)) = 1024
    uint addr_o = (offset_remainder + TIME_DIV_256*TIME_FOR_256_TIMESTEPS);//for multi-dataset was simply this: (get_group_id(0) * NUM_FREQUENCIES * NUM_BLOCKS_x_2048); //dataset/bin offset
    //addr_o = ((addr_o % (NUM_DATASETS*delta_time_bin)) / delta_time_bin) * NUM_BLOCKS_x_2048 + (LOCAL_Y * 256u) + (LOCAL_X * 8u) + (FREQUENCY_BAND * NUM_BLOCKS_x_2048);

     addr_o = addr_o % (NUM_DATASETS*delta_time_bin);
     addr_o = addr_o / delta_time_bin;
     addr_o *= NUM_FREQUENCIES * NUM_BLOCKS_x_2048;
     addr_o += (/*(BLOCK_ID * 2048u) + */(LOCAL_Y * 256u) + (LOCAL_X * 8u)) + (FREQUENCY_BAND * NUM_BLOCKS_x_2048);
    //uint addr_o = ((BLOCK_ID * 2048u) + (LOCAL_Y * 256u) + (LOCAL_X * 8u)) + (FREQUENCY_BAND * NUM_BLOCKS_x_2048);

    atomic_add(&corr_buf[addr_o+0u],   (corr_a.s0  >> 16u) + (corr_a.s1  & 0xffff) ); //real value
    atomic_add(&corr_buf[addr_o+1u],   (corr_a.s1  >> 16u) - (corr_a.s0  & 0xffff) );//imaginary value
    atomic_add(&corr_buf[addr_o+2u],   (corr_a2.s0 >> 16u) + (corr_a2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+3u],   (corr_a2.s1 >> 16u) - (corr_a2.s0 & 0xffff) );
    atomic_add(&corr_buf[addr_o+4u],   (corr_e.s0  >> 16u) + (corr_e.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+5u],   (corr_e.s1  >> 16u) - (corr_e.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+6u],   (corr_e2.s0 >> 16u) + (corr_e2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+7u],   (corr_e2.s1 >> 16u) - (corr_e2.s0 & 0xffff) );

    atomic_add(&corr_buf[addr_o+64u],  (corr_b.s0  >> 16u) + (corr_b.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+65u],  (corr_b.s1  >> 16u) - (corr_b.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+66u],  (corr_b2.s0 >> 16u) + (corr_b2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+67u],  (corr_b2.s1 >> 16u) - (corr_b2.s0 & 0xffff) );
    atomic_add(&corr_buf[addr_o+68u],  (corr_f.s0  >> 16u) + (corr_f.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+69u],  (corr_f.s1  >> 16u) - (corr_f.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+70u],  (corr_f2.s0 >> 16u) + (corr_f2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+71u],  (corr_f2.s1 >> 16u) - (corr_f2.s0 & 0xffff) );

    atomic_add(&corr_buf[addr_o+128u], (corr_c.s0  >> 16u) + (corr_c.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+129u], (corr_c.s1  >> 16u) - (corr_c.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+130u], (corr_c2.s0 >> 16u) + (corr_c2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+131u], (corr_c2.s1 >> 16u) - (corr_c2.s0 & 0xffff) );
    atomic_add(&corr_buf[addr_o+132u], (corr_g.s0  >> 16u) + (corr_g.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+133u], (corr_g.s1  >> 16u) - (corr_g.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+134u], (corr_g2.s0 >> 16u) + (corr_g2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+135u], (corr_g2.s1 >> 16u) - (corr_g2.s0 & 0xffff) );

    atomic_add(&corr_buf[addr_o+192u], (corr_d.s0  >> 16u) + (corr_d.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+193u], (corr_d.s1  >> 16u) - (corr_d.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+194u], (corr_d2.s0 >> 16u) + (corr_d2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+195u], (corr_d2.s1 >> 16u) - (corr_d2.s0 & 0xffff) );
    atomic_add(&corr_buf[addr_o+196u], (corr_h.s0  >> 16u) + (corr_h.s1  & 0xffff) );
    atomic_add(&corr_buf[addr_o+197u], (corr_h.s1  >> 16u) - (corr_h.s0  & 0xffff) );
    atomic_add(&corr_buf[addr_o+198u], (corr_h2.s0 >> 16u) + (corr_h2.s1 & 0xffff) );
    atomic_add(&corr_buf[addr_o+199u], (corr_h2.s1 >> 16u) - (corr_h2.s0 & 0xffff) );
}
