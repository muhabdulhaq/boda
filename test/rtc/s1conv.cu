// each thread: computes 8x8 block of out
// loop over k dim
extern "C"  __global__ void %(cu_func_name)( float const * const filts, float const * const biases, float const * const in, float * const out ) {
  __shared__ float in_smem[%(in_pad) + %(in_ix_x_dim) + %(in_pad)]; 
  // zero init padding part of in_smem
  if( threadIdx.x < ( 2 * %(in_pad) ) ) { 
    in_smem[ (( threadIdx.x < %(in_pad) ) ? 0 : %(in_ix_x_dim))+threadIdx.x] = 0.0f; 
  }  
  int32_t const blk_filt_ix_sz = %(threadIdx.x_out_chan_tile_dim)*%(t_tile_sz);
  __shared__ float filts_smem[blk_filt_ix_sz];
  float out_tile[%(t_tile_sz)*%(t_tile_sz)] = {0}; // tile of output for this thread to compute, stored in registers
  // reg. buffers for one strip each from in and filts of %(t_tile_sz) elements, for the same filts_ix_out_chan_elem
  float filts_strip[%(t_tile_sz)]; // across output chans (stride is blk_filt_ix_sz )
  float in_strip[%(t_tile_sz)]; // across patches (approx square block in x/y space, favoring x if sqrt() not integer)
  int32_t const blk_filt_ix_base = %(blockIdx.x_out_chan_blk)*blk_filt_ix_sz;

  // iteratate over filter elements
  int32_t filts_off = blk_filt_ix_base;
  for( int32_t filts_ix_out_chan_elem = 0; filts_ix_out_chan_elem != (%(filts_xp_ix_sz) / %(filts_xp_ix_x_sz));
       ++filts_ix_out_chan_elem ) {
    __syncthreads();
    for( int32_t i = 0; i != %(out_chan_smem_load_iter); ++i ) {
      int32_t const t_smem_filt_ix = threadIdx.x+blockDim.x*i;
      if( t_smem_filt_ix < blk_filt_ix_sz ) { 
	filts_smem[t_smem_filt_ix] = filts[filts_off+t_smem_filt_ix];
      }
    }
    if( %(filts_ix_out_chan_elem_x) == 0 ) {
      int32_t const t_smem_line_x = threadIdx.x;
      if( t_smem_line_x < %(in_ix_x_dim) ) { 
	%(get_in);
	in_smem[%(in_pad)+t_smem_line_x] = v;
      }
    }
    filts_off += %(filts_xp_ix_x_sz);
    __syncthreads();
    %(t_tile_filt_loads);
    %(t_tile_in_loads);

    %(t_tile_fmas);
  }

  // load per-block biases into smem
  __syncthreads();
    for( int32_t i = 0; i != %(out_chan_smem_load_iter); ++i ) {
      int32_t const t_smem_bias_ix = threadIdx.x+blockDim.x*i;
      if( t_smem_bias_ix < blk_filt_ix_sz ) { 
	int32_t const ocix_base = %(blockIdx.x_out_chan_blk)*blk_filt_ix_sz;
	int32_t const load_reg = t_smem_bias_ix / %(threadIdx.x_out_chan_tile_dim);
	int32_t const load_tile = t_smem_bias_ix %% %(threadIdx.x_out_chan_tile_dim);
	int32_t const ocix = ocix_base + load_tile*%(t_tile_sz) + load_reg;
	if( ocix < %(out_ix_chan_dim) ) { filts_smem[t_smem_bias_ix] = biases[ ocix ]; }
      }
  }
  __syncthreads();
  // load biases into filts_strip
  %(t_tile_filt_loads);

  // add bias to each elem of out_tile[] and store the results to out[]
#ifdef NO_IO2
  %(t_tile_dummy_stores);
#else
  %(t_tile_stores);
#endif
}

