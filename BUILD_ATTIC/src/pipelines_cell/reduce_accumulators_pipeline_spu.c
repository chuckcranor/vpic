#define IN_sf_interface
#define HAS_SPU_PIPELINE
#include <sf_interface_private.h>

#include <spu_mfcio.h>

#define NB accumulators_n_block
#define MAX_ARRAY 11

// DMA tag usage:
// 0:MAX_ARRAY-1 -> Getting accumulator blocks
// MAX_ARRAY:31  -> Writing accumulator blocks

// Local store single precision accumulator data is organized
// 0 1 2 3 / 4 5 6 7 / 8 9 10 11 / x x x x / 12 13 14 15 / ...
// ---- A[0] -----------------------------   ---- A[1] -------
//
// Local store double precision accumulator data is organized
// 0 2 / 1 3 / 4 6 / 5 7 / 8 10 / 9 11 / 12 14 / 13 15 / ...
// ---- A[0] -------------------------   ----- A[1] --------

void
_SPUEAR_reduce_accumulators_pipeline_spu( accumulators_pipeline_args_t * args,
                                          int pipeline_rank,
                                          int n_pipeline ) {
  int i, i1, si = sizeof(accumulator_t);
  int r, na = args->n_array, sa = si*args->s_array;
  int k, n;

  DISTRIBUTE( args->n, NB, pipeline_rank, n_pipeline, i, i1 ); i1 += i;

  MEM_PTR( accumulator_t, 128 ) a = args->a;

  if( na>MAX_ARRAY ) return; // FIXME: ABORT W/ DIAGS! 
  if( na<2         ) return; // Nothing to do

  vec_float4  * ALIGNED(128) f; SPU_MALLOC( f, 4*NB*MAX_ARRAY, 128 ); // 176KiB
  vec_float4  * ALIGNED(128) g; SPU_MALLOC( g, 4*NB,           128 ); //  16KiB
  vec_double2 * ALIGNED(128) d; SPU_MALLOC( d, 6*NB,           128 ); //  24KiB

  vec_uchar16 perm = {  4, 5, 6, 7,  0, 1, 2, 3, 12,13,14,15,   8, 9,10,11 };
  vec_float4 f0, f1, f2;

  // Begin loading the accumulators for the initial block

  if( i<i1 )
    for( r=0; r<na; r++ ) mfc_get( f+4*NB*r, a+i*si+r*sa, NB*si, r, 0, 0 );

  // For all blocks of voxels reduced by this pipeline

  for( ; i<i1; i+=NB ) {

    // Wait for array 0's accumulators in this block to arrive (in f_0),
    // store them in double precision (in d) and begin loading array 0's
    // accumulators in the next block.
    
    mfc_write_tag_mask( 1<<0 );
    mfc_read_tag_status_all();

    for( n=0; n<NB; n++ ) {
      f0 = f[4*n+0];
      f1 = f[4*n+1];
      f2 = f[4*n+2];
      d[6*n+0] = spu_extend(             f0          );
      d[6*n+1] = spu_extend( spu_shuffle(f0,f0,perm) );
      d[6*n+2] = spu_extend(             f1          );
      d[6*n+3] = spu_extend( spu_shuffle(f1,f1,perm) );
      d[6*n+4] = spu_extend(             f2          );
      d[6*n+5] = spu_extend( spu_shuffle(f2,f2,perm) );
    }

    if( i+NB<i1 ) mfc_get( f, a+(i+NB)*si, NB*si, 0, 0, 0 );

    // For each remaining array, wait for it's accumulators in this block
    // to arrive (in f_r), sum them in double precision with array 0's
    // accumulators (in d) and begin loading its accumulators in the
    // next block.
 
    for( r=1; r<na; r++ ) {

      mfc_write_tag_mask( 1<<r );
      mfc_read_tag_status_all();

      k = r*NB;
      for( n=0; n<NB; n++ ) {       
        f0       = f[4*(k+n)+0];
        f1       = f[4*(k+n)+1];
        f2       = f[4*(k+n)+2];
        d[6*n+0] = spu_add( d[6*n+0], spu_extend(             f0          ) );
        d[6*n+1] = spu_add( d[6*n+1], spu_extend( spu_shuffle(f0,f0,perm) ) );
        d[6*n+2] = spu_add( d[6*n+2], spu_extend(             f1          ) );
        d[6*n+3] = spu_add( d[6*n+3], spu_extend( spu_shuffle(f1,f1,perm) ) );
        d[6*n+4] = spu_add( d[6*n+4], spu_extend(             f2          ) );
        d[6*n+5] = spu_add( d[6*n+5], spu_extend( spu_shuffle(f2,f2,perm) ) );
      }

      if( i+NB<i1 ) mfc_get( f+4*NB*r, a+(i+NB)*si+r*sa, NB*si, r, 0, 0 );
    }

    // Wait for the reduced accumulators in the previous block (in g) to
    // finish being written, convert the double precision reduced accumulators
    // in this block (in d) to single precision (in g) and begin writing
    // them out.

    mfc_write_tag_mask( 1<<na );
    mfc_read_tag_status_all();
    
    for( n=0; n<NB; n++ ) {
      f0       = spu_roundtf( d[6*n+1] );
      f1       = spu_roundtf( d[6*n+3] );
      f2       = spu_roundtf( d[6*n+5] );
      g[4*n+0] = spu_or( spu_roundtf( d[6*n+0] ), spu_shuffle(f0,f0,perm) );
      g[4*n+1] = spu_or( spu_roundtf( d[6*n+2] ), spu_shuffle(f1,f1,perm) );
      g[4*n+2] = spu_or( spu_roundtf( d[6*n+4] ), spu_shuffle(f2,f2,perm) );
    }

    mfc_put( g, a+i*si, NB*si, na, 0, 0 );
  }
}
