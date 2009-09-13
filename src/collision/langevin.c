#define IN_collision
#include "collision_private.h"

/* Private interface *********************************************************/

typedef struct langevin {
  species_t * sp;
  mt_rng_t ** rng;
  float kT;
  float nu;
  int interval;
} langevin_t;


    
void
langevin_pipeline( langevin_t * RESTRICT l,
                   int pipeline_rank,
                   int n_pipeline ) {
  particle_t * RESTRICT p   = l->sp->p;
  mt_rng_t   * RESTRICT rng = l->rng[ pipeline_rank ];

  /* Decay and drive have a fun derivation.  We want to integrate the
     stochastic equation:
       du = -nu u dt + sqrt( 2 kT / mc ) dW
     For small dt, this is:
       u_1 = u_0 ( 1- nu dt ) + RANDN( 2 kT nu dt / mc )
     where RANDN( var ) is a normal random number with _variance_ var.
     Let:
       a = nu dt
       b = 2 kT nu dt / mc
     Then:
       u_1 = (1-a) u_0 + RANDN( b )
     We can get more accurate by making N substeps of length dt/N.
     Then:
       u_{n+1} = (1-a/N) u_n + RANDN( b/N )
     such that:
       u_N = (1-a/N)^N u_0 + sum_{n=0:N-1} (1-a/N)^n RANDN( b/N )
     Noting that sum of N normal random numbers is a normal random
     number whose variance is the sum of the N variances, we have:
       u_N = (1-a/N)^N u_0 + RANDN( sum_{n=0:N-1} (1-a/N)^{2n} b/N )
     Analytically summing the variances yields:
       u_N = (1-a/N)^N u_0 + RANDN( [1-(1-a/N)^{2N} / ( 1-(1-a/N)^2 )] b/N )
     In the continuum limit (N goes to infinity):
       u_N = decay u_0 + RANDN( drive^2 )
     or:
       u_N = decay u_0 + drive RANDN( 1 )
     where:
       decay   = lim (1-a/N)^N = exp(-a)
       drive^2 = lim variance sum
               = [(1-exp(-2a) b] / [(1 - 1 + 2a/N) N]
               = ( 1-exp(-2a) ) b / (2a)
     subtituting a and b into decay and drive yields:
       decay   = exp(-nu dt)
       drive   = sqrt( (1-exp(-2 nu dt)) kT / mc )
     In the limit nu dt small:
       decay -> 1 - nu dt
       drive -> sqrt( 2 nu dt kT / mc )
     reproducing the infinitesimal stochastic differential equation.
     In the limit nu dt large:
       decay -> 0
       drive -> sqrt( kT / mc )
     which is equivalent to resampling the momentum with the
     desired temperature. */

  const float nudt  = l->nu * (float)l->interval * l->sp->g->dt;
  const float decay = exp( -nudt );
  const float drive = sqrt( ( -expm1(-2*nudt)*l->kT ) /
                            ( l->sp->m*l->sp->g->cvac  ) );

  double n_target = (double)l->sp->np / (double)n_pipeline;
  /**/  int i  = (int)( 0.5 + n_target*(double) pipeline_rank    );
  const int i1 = (int)( 0.5 + n_target*(double)(pipeline_rank+1) );

  if( pipeline_rank==n_pipeline ) return; /* No host straggler cleanup */

  for( ; i<i1; i++ ) {
    p[i].ux = decay*p[i].ux + drive*mt_frandn(rng);
    p[i].uy = decay*p[i].uy + drive*mt_frandn(rng);
    p[i].uz = decay*p[i].uz + drive*mt_frandn(rng);
  }
}

void
apply_langevin( langevin_t * l ) {
  if( l->interval<1 || (l->sp->g->step % l->interval) ) return;
  EXEC_PIPELINES( langevin, l, 0 );
  WAIT_PIPELINES();
}

void
checkpt_langevin( const collision_op_t * cop ) {
  const langevin_t * l = (const langevin_t *)cop->params;
  CHECKPT( l, 1 );
  CHECKPT_PTR( l->sp );
  CHECKPT_PTR( l->rng );
  checkpt_collision_op_internal( cop );
}

collision_op_t *
restore_langevin( void ) {
  langevin_t * l;
  RESTORE( l );
  RESTORE_PTR( l->sp );
  RESTORE_PTR( l->rng );
  return restore_collision_op_internal( l );
}

void
delete_langevin( collision_op_t * cop ) {
  FREE( cop->params );
  delete_collision_op_internal( cop );
}

/* Public interface **********************************************************/

collision_op_t *
langevin( species_t * RESTRICT sp,
          mt_rng_t  **         rng,
          float                kT,
          float                nu,
          int                  interval ) {
  langevin_t * l;

  if( !sp || !rng || kT<0 || nu<0 ) ERROR(( "Bad args" ));

  MALLOC( l, 1 );
  l->sp       = sp;
  l->rng      = rng;
  l->kT       = kT;
  l->nu       = nu;
  l->interval = interval;
  return new_collision_op_internal( l,
                                    (collision_op_func_t)apply_langevin,
                                    delete_langevin,
                                    (checkpt_func_t)checkpt_langevin,
                                    (restore_func_t)restore_langevin,
                                    NULL );
}

