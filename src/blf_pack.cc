// Copyright (c) 2013-2014, Matthew W. Moskewicz <moskewcz@alumni.princeton.edu>; part of Boda framework; see LICENSE
#include"boda_tu_base.H"
#include"geom_prim.H"
#include"timers.H"
#include"str_util.H"
#include"has_main.H"
#include"io_util.H"
#include"img_io.H"

namespace boda 
{
  using namespace boost;

  struct blf_bin_t {
    u32_box_t bin;
    vect_u32_box_t holes; 
    set_u32_box_t holes_set;
    // note: strict subset holes may be added in some situations, and thus we may later try to add
    // duplicate holes (which we drop). if we could drop/ignore subset holes easily, that might be
    // nice, but shouldn't be needed.
    void add_hole( u32_box_t const & hole ) { if( holes_set.insert( hole ).second ) { holes.push_back( hole ); } }
    blf_bin_t( u32_box_t const & bin_ ) : bin(bin_) { add_hole( bin ); }
    u32_pt_t place_box( u32_pt_t bsz ) {
      assert_st( bsz.both_dims_non_zero() );
      set_u32_box_t::const_iterator i = holes_set.begin();
      for( ; i != holes_set.end(); ++i ) { if( i->sz().both_dims_ge( bsz ) ) { break; } } // break if fits
      // if bsz didn't fit in any hole fit, return terminal/sentinal value
      if( i == holes_set.end() ) { return u32_pt_t_const_max; } 
      u32_box_t const ret( i->p[0], i->p[0] + bsz ); // place at -X,-Y in hole
      assert_st( ret.is_strictly_normalized() );
      // update holes
      vect_u32_box_t orig_holes;
      orig_holes.swap( holes );
      for( vect_u32_box_t::const_iterator i = orig_holes.begin(); i != orig_holes.end(); ++i ) {
	u32_box_t const ob = i->get_overlap_box_with( ret );
	if( ob.is_strictly_normalized() ) { // if the placement overlaps this hole
	  // remove hole from (ordered) set (note: implicitly removed from holes)
	  uint32_t const num_del = holes_set.erase( *i ); assert_st( num_del == 1 );
	  // add remaining/new holes from all sides if they exist (note that they may overlap each other)
	  for( uint32_t d = 0; d != 2; ++d ) { // +/- edge iter
	    for( uint32_t e = 0; e != 2; ++e ) { // dims (X/Y) edit iter
	      if( i->p[d].d[e] != ob.p[d].d[e] ) { // if there is a non-zero hole on this edge
		u32_box_t new_hole = *i; // start with original hole
		new_hole.p[d^1].d[e] = ob.p[d].d[e]; // trim to edge of overlap area
		assert_st( new_hole.is_strictly_normalized() );
		add_hole( new_hole ); 
	      }
	    }
	  }
	  //printf( "OVER (*i)=%s ret=%s\n", str((*i)).c_str(), str(ret).c_str() );
	} else { holes.push_back( *i ); } // no overlap with this hole, keep hole in holes (implicitly kept in holes_set)
      }
      //printf( "-- OUT holes=%s\n", str(holes).c_str() );
      return ret.p[0];
    }
  };

  typedef vector< blf_bin_t > vect_blf_bin_t; 
  typedef shared_ptr< blf_bin_t > p_blf_bin_t; 
  typedef vector< p_blf_bin_t > vect_p_blf_bin_t;

  uint32_t blf_place( vect_u32_pt_w_t & out, u32_pt_t bin_sz, vect_u32_pt_t const & to_place, bool const no_fit_okay )
  {
    vect_p_blf_bin_t bins;
    for( vect_u32_pt_t::const_iterator i = to_place.begin(); i != to_place.end(); ++i ) {
      u32_pt_t placement = u32_pt_t_const_max;
      uint32_t bin_ix = 0;
      for( ; bin_ix != bins.size(); ++bin_ix ) {
	placement = bins[bin_ix]->place_box( *i );
	if( placement != u32_pt_t_const_max ) { break; }
      }
      if( placement == u32_pt_t_const_max ) { // didn't fit in any bin, add one
	bins.push_back( p_blf_bin_t( new blf_bin_t( u32_box_t( u32_pt_t(), bin_sz ) ) ) );
	placement = bins.back()->place_box( *i );
	if( placement == u32_pt_t_const_max ) {
	  if( no_fit_okay ) { bin_ix = uint32_t_const_max; } 
	  else {
	    rt_err( strprintf( "box (*i)=%s cannot be placed into empty bin with bin_sz=%s "
			       "(i.e. box to place > bin size)", str((*i)).c_str(), str(bin_sz).c_str() ) );
	  }
	}
      }
      //printf( "placement=%s bin_ix=%s\n", str(placement).c_str(), str(bin_ix).c_str() );
      out.push_back( u32_pt_w_t( placement, bin_ix ) );
    }
    return bins.size();
  }


  struct blf_pack_t : virtual public nesi, public has_main_t // NESI(help="blf rectangle packing",bases=["has_main_t"], type_id="blf_pack")
  {
    virtual cinfo_t const * get_cinfo( void ) const; // required declaration for NESI support
    filename_t out_fn; //NESI(default="%(boda_output_dir)/out.txt",help="text output filename")
    filename_t to_pack_fn; //NESI(help="input: filename for list of boxes to pack",req=1)
    uint32_t bin_sz; //NESI(help="bin size for packing (as many bins needed will be used)",req=1)
    virtual void main( nesi_init_arg_t * nia ) { 
      p_ostream out = ofs_open( out_fn.exp );
      vect_u32_pt_t to_pack;
      read_text_file( to_pack, to_pack_fn.exp );
      sort( to_pack.begin(), to_pack.end(), u32_pt_t_by_prod_gt_t() );
      (*out) << strprintf( "bin_sz=%s\n", str(bin_sz).c_str() );
      (*out) << strprintf( "to_pack=%s\n", str(to_pack).c_str() );
      vect_u32_pt_w_t placements;
      blf_place( placements, u32_pt_t(bin_sz,bin_sz), to_pack, 0 );
      (*out) << strprintf( "placements=%s\n", str(placements).c_str() );
    }
  };


  // return a minimal amount of padding (4 per-edge padding values packed into a u32_box_t) for a
  // box of size sz, such that there is a minimum of min_pad[dim] on the -/+[dim] edges, and such
  // that both the - edge padding *and* the total of the padding + sz in each dim is a multiple of
  // align.
  u32_box_t pad_and_align_sz( u32_pt_t const & sz, u32_pt_t const & align, u32_pt_t const & min_pad ) {
    assert_st( sz.both_dims_non_zero() ); // might be okay, but doesn't seem sensible
    assert_st( align.both_dims_non_zero() );
    return u32_box_t( min_pad.ceil_align( align ), (min_pad + sz).ceil_align( align ) - sz );
  }

  // create an approximately logarithmically spaced list of sizes given an input size, with
  // 'interval' steps per octave (factor of 2). the scale of the largest size returned will be
  // 2^num_upsampled_octaves. note that a 'primary' set of sizes (for the first downsampled octave)
  // are determined by scaling the input size by 2^(-i/interval) (with i in [0,interval) and
  // rounding to the nearest integer. the remaining sizes are calculated per-octave by iteratively
  // doubling the primary sizes for each upsampled octave, and by iteratively halving and rounding
  // the primary sizes for the downsampled octaves. this is done to allow for efficient and
  // close-to-right 2x up/downsampling to create the non-primary sizes, with the approximation being
  // the need to fudge 2x downsamples of odd sizes by (for example) clone-padding by 1 on the +
  // edge. sizes with either dimension == 1 won't be halved again, and thus the retuned set of sizes
  // is finite. due to rounding effects, one might want to re-calculate the scale of each primary
  // size by taking a ratio with the original size. it would also be possible to scale at exactly
  // the ideal scale and pad or clip the input or output slightly. for the down sampled sizes, the
  // best scale to use for them might depend on if exact-to-size versus exactly-2x scaling is
  // used. generally, as the intent is to use exactly-2x downsampling, the scales of the downsamples
  // sizes should be treated as exactly 2x less than thier parent size, even if the parent size was
  // odd. any scales smaller than and including the first duplicate size will be removed. roughly
  // log2(min_dim(in_sz))*interval sizes will be returned.
  void create_pyra_sizes( vect_u32_pt_t & pyra, u32_pt_t const & in_sz, 
			  uint32_t const num_upsamp_octaves, uint32_t const interval ) 
  {
    pyra.clear();
    assert_st( num_upsamp_octaves < 7 ); // sanity limit, maybe a little too strong?
    assert_st( in_sz.both_dims_non_zero() );
    assert_st( (in_sz.d[0] != uint32_t_const_max) && (in_sz.d[1] != uint32_t_const_max) ); // avoid overflow/sanity check
    for( uint32_t i = 0; i < interval; ++i ) { 
      double const scale = pow(2.0d, 0.0d - (double(i) / double(interval) ) );
      u32_pt_t scale_sz = in_sz.scale_and_round( scale );
      for( uint32_t oct_ix = 0; oct_ix != num_upsamp_octaves ; ++oct_ix ) { 
	u32_pt_t const us_scale_sz( scale_sz.d[0] << 1, scale_sz.d[1] << 1 ); // scale up one octave (factor of 2)
	assert_st( us_scale_sz.both_dims_gt( scale_sz ) ); // check for no overflow
	scale_sz = us_scale_sz;
      }      
      uint32_t cur_scale_ix = i;
      for( int32_t oct_ix = num_upsamp_octaves; oct_ix > -20 ; --oct_ix ) { // limit if -20 is another sanity-ish limit.
	assert_st( scale_sz.both_dims_non_zero() );
	while( pyra.size() <= cur_scale_ix ) { pyra.push_back( u32_pt_t() ); }
	pyra[cur_scale_ix] = scale_sz;
	if( (scale_sz.d[0] == 1) || (scale_sz.d[1] == 1) ) { break; } // more-or-less can't scale down more
	scale_sz = u32_pt_t( (scale_sz.d[0]+1) >> 1, (scale_sz.d[1]+1) >> 1 ); // scale down one octave (factor of 2)
	cur_scale_ix += interval;
      }
    }
    // remove all scales after and including first duplicate
    vect_u32_pt_t::iterator af = std::adjacent_find( pyra.begin(), pyra.end() );
    if( af != pyra.end() ) { pyra.erase( ++af, pyra.end() ); } 
  }

  struct pyra_pack_t : virtual public nesi, public has_main_t // NESI(help="pyramid packing",bases=["has_main_t"], type_id="pyra_pack")
  {
    virtual cinfo_t const * get_cinfo( void ) const; // required declaration for NESI support
    u32_pt_t in_sz; //NESI(help="input size to create pyramid for",req=1)
    uint32_t num_upsamp_octaves; //NESI(default=1,help="number of upsampled octaves")
    uint32_t interval; //NESI(default=10,help="steps per octave (factor of 2)")
    u32_pt_t align; //NESI(default="16 16", help="pyra per-dim alignment")
    u32_pt_t min_pad; //NESI(default="165 165", help="pyra per-dim minimum padding")
    uint32_t bin_sz; //NESI(default="1200", help="bin size for packing (as many bins needed will be used)")

    // for interactive/testing use
    filename_t out_fn; //NESI(default="%(boda_output_dir)/out.txt",help="text output filename")

    // outputs
    vect_u32_pt_t sizes;
    vect_u32_box_t pads;          
    vect_u32_pt_w_t placements;
    uint32_t num_bins;

    pyra_pack_t( void ) : num_bins(0) { }

    void do_place( void ) {
      create_pyra_sizes( sizes, in_sz, num_upsamp_octaves, interval );
      vect_u32_pt_t to_pack;
      for( vect_u32_pt_t::const_iterator i = sizes.begin(); i != sizes.end(); ++i ) {
	pads.push_back( pad_and_align_sz( *i, align, min_pad ) );
	to_pack.push_back( pads.back().bnds_sum() + (*i) );
      }
      num_bins = blf_place( placements, u32_pt_t(bin_sz,bin_sz), to_pack, 1 );
    }

    virtual void main( nesi_init_arg_t * nia ) { 
      p_ostream out = ofs_open( out_fn.exp );
      do_place();
      (*out) << strprintf( "sizes=%s\n", str(sizes).c_str() );
      (*out) << strprintf( "bin_sz=%s\n", str(bin_sz).c_str() );
      (*out) << strprintf( "num_bins=%s placements=%s\n", str(num_bins).c_str(), str(placements).c_str() );
    }
  };


  void create_pyra_imgs( vect_p_img_t & pyra_imgs, p_img_t const & src_img, pyra_pack_t const & pyra ) {
    pyra_imgs.resize( pyra.sizes.size() );
    assert_st( get_wh(*src_img).both_dims_non_zero() );
    for( uint32_t i = 0; i < pyra.interval; ++i ) {
      if( i >= pyra.sizes.size() ) { break; } // < 1 octave in pyra, we're done
      u32_pt_t const i_max_sz = pyra.sizes[i];
      u32_pt_t const base_octave_sz = i_max_sz >> pyra.num_upsamp_octaves;
      if( ( base_octave_sz << pyra.num_upsamp_octaves ) != i_max_sz ) {
	rt_err( strprintf( "for interval step %s, i_max_sz=%s isn't evenly divisible by num_upsamp_octaves=%s.\n", 
			   str(i).c_str(), str(i_max_sz).c_str(), str(pyra.num_upsamp_octaves).c_str() ) );
      }
      p_img_t base_octave_img = src_img;
      if( !i ) { 
	if( base_octave_sz != get_wh(*src_img) ) {
	  rt_err( strprintf( "pyramid scale=1 size of base_octave_sz=%s does not equal src_img_sz=%s (pyra/src_img mismatch)\n", 
			     str(base_octave_sz).c_str(), str(get_wh(*src_img)).c_str() ) );
	} 
      } else {
	base_octave_img = downsample_to_size( src_img, base_octave_sz.d[0], base_octave_sz.d[1] );
      }
      p_img_t cur_octave_img = base_octave_img;
      // create base and (if any) upsampled octaves
      for( uint32_t j = i + (pyra.interval * pyra.num_upsamp_octaves); ; j -= pyra.interval ) {
	if( j < pyra_imgs.size() ) {
	  assert_st( pyra.sizes[j] == get_wh(*cur_octave_img) );
	  pyra_imgs[j] = cur_octave_img; 
	}
	if( j < pyra.interval ) { break; }
	cur_octave_img = upsample_2x( cur_octave_img );
      }
      cur_octave_img = base_octave_img;
      // create downsampled octaves
      for( uint32_t j = i + (pyra.interval * (1 + pyra.num_upsamp_octaves)); ; j += pyra.interval ) {
	if( ! (j < pyra_imgs.size()) ) { break; } // all done with ds'd octaves
	cur_octave_img = downsample_2x( cur_octave_img );
	assert_st( pyra.sizes[j] == get_wh(*cur_octave_img) );
	pyra_imgs[j] = cur_octave_img; 
      }
    }
  }


  void img_draw_box_pad( img_t * const dest, u32_box_t const & b, u32_box_t const & pad, uint32_t const & ec ) {
    // pad edges
    for( uint32_t e = 0; e != 2; ++e ) {
      for( uint32_t d = 0; d != 2; ++d ) {
	u32_pt_t sp;
	sp.d[d^1] = b.p[e].d[d^1]-e; // d coord of pixel on e edge
	for( sp.d[d] = b.p[0].d[d]; sp.d[d] != b.p[1].d[d]; ++sp.d[d] ) {
	  int32_t const stride = e?1:-1;
	  int32_t const stride_x = d?stride:0;
	  int32_t const stride_y = d?0:stride;
	  uint32_t const ic = dest->get_pel( sp.d[0], sp.d[1] );
	  img_draw_pels( dest, sp.d[0]+stride_x, sp.d[1]+stride_y, pad.p[e].d[d^1], stride_x, stride_y, ic, ec ); 
	}
      }
    }
    // pad corners
	
    // dim 0 --> - side of image
    for( uint32_t dx = 0; dx != 2; ++ dx ) { 
      for( uint32_t dy = 0; dy != 2; ++ dy ) {
	u32_pt_t cp;
	cp.d[0] = b.p[dx].d[0]-dx; 
	cp.d[1] = b.p[dy].d[1]-dy;
	// cp is coord of dx,dy source (non-padding) corner image pixel 
	uint32_t const lt_sz = std::min( pad.p[dx].d[0], pad.p[dy].d[1] ); 
	for( uint32_t dd = 2; dd <= lt_sz; ++dd ) {
	  uint32_t const cx = cp.d[0] + ( dx ? dd : -dd ); // cx,y is point on existing dx padding, dd outside image
	  uint32_t const cy = cp.d[1] + ( dy ? dd : -dd ); // x,cy is point on existing dy padding, dd outside image
	  uint32_t const cx_y_v = dest->get_pel( cx, cp.d[1] );
	  uint32_t const x_cy_v = dest->get_pel( cp.d[0], cy );
	  int32_t const stride_x = dx ? -1 :  1 ;
	  int32_t const stride_y = dy ?  1 : -1 ;
	  //printf( "cx=%s cp.d[1]=%s (dd-1)=%s stride_x=%s stride_y=%s\n", str(cx).c_str(), str(cp.d[1]).c_str(), str((dd-1)).c_str(), str(stride_x).c_str(), str(stride_y).c_str() );
	  img_draw_pels( dest, cx, cp.d[1], dd, stride_x, stride_y, cx_y_v, x_cy_v );  // note: overwrites cx,cp.d[1]
	}
      }
    }
  }

  struct img_pyra_pack_t : virtual public nesi, public pyra_pack_t // NESI(help="image pyramid packing",bases=["pyra_pack_t"], type_id="img_pyra_pack")
  {
    virtual cinfo_t const * get_cinfo( void ) const; // required declaration for NESI support
    // for interactive/testing use
    filename_t img_in_fn; //NESI(help="input image filename",req=1)
    filename_t img_out_fn; //NESI(default="%(boda_output_dir)/out_%%s.png",help="format for filenames of output image bin files. %%s will be replaced with the bin index.")

    virtual void main( nesi_init_arg_t * nia ) { 
      do_place();

      p_img_t img_in( new img_t );
      img_in->load_fn( img_in_fn.exp );

      vect_p_img_t pyra_imgs;
      create_pyra_imgs( pyra_imgs, img_in, *this );

      uint32_t const inmc = 123U+(117U<<8)+(104U<<16)+(255U<<24); // RGBA

      vect_p_img_t bin_imgs;
      for( uint32_t bix = 0; bix != num_bins; ++bix ) {
	bin_imgs.push_back( p_img_t( new img_t ) );
	bin_imgs.back()->set_sz_and_alloc_pels( bin_sz, bin_sz ); // w, h
	bin_imgs.back()->fill_with_pel( inmc );
      }

      for( uint32_t pix = 0; pix != pyra_imgs.size(); ++pix ) {
	//filename_t ofn = filename_t_printf( img_out_fn, str(pix).c_str() );
	//pyra_imgs[pix]->save_fn_png( ofn.exp );
	assert_st( get_wh(*pyra_imgs.at(pix)) == sizes.at(pix) );
	uint32_t const bix = placements.at(pix).w;
	if( bix == uint32_t_const_max ) { continue; } // skip failed placements FIXME: diagnostic?
	u32_pt_t const dest = placements.at(pix) + pads.at(pix).p[0];
	img_copy_to( pyra_imgs.at(pix).get(), bin_imgs.at(bix).get(), dest.d[0], dest.d[1] );
	//printf( "dest=%s sizes.at(pix)=%s pads.at(pix)=%s\n", str(dest).c_str(), str(sizes.at(pix)).c_str(), str(pads.at(pix)).c_str() );
	img_draw_box_pad( bin_imgs.at(bix).get(), u32_box_t( dest, dest + sizes.at(pix) ), pads.at(pix), inmc );
      }

      for( uint32_t bix = 0; bix != num_bins; ++bix ) {
	filename_t ofn = filename_t_printf( img_out_fn, str(bix).c_str() );
	bin_imgs.at(bix)->save_fn_png( ofn.exp );
	printf( "ofn.exp=%s\n", str(ofn.exp).c_str() );	
      }
    }
  };

#include"gen/blf_pack.cc.nesi_gen.cc"

};