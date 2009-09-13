#ifndef _collision_h_
#define _collision_h_

/* Note that it is possible to generalize these interfaces to
   accomdate collisional processes involving an arbitrary number of
   bodies (e.g. 3-body recombination processes). */

#include "../species_advance/species_advance.h"

struct collision_op;
typedef struct collision_op collision_op_t;

BEGIN_C_DECLS

/* In collision.c */

int
num_collision_op( const collision_op_t * RESTRICT cop_list );

void
apply_collision_op_list( collision_op_t * RESTRICT cop_list );

void
delete_collision_op_list( collision_op_t * RESTRICT cop_list );

collision_op_t *
append_collision_op( collision_op_t * cop,
                     collision_op_t ** cop_list );

/* In langevin.c */

/* The most basic collision model (but implemented with numerical
   sophistication).  nu is the collision frequency of the particles
   with some unresolved stationary large thermal bath.  kT is the
   thermal bath temperature.  This method is stable (e.g. if you set 
   nu very large, it is equivalent to resampling your particle
   normal momenta from a normal distribution with
   uth = sqrt(kT/mc)) every time this operator is applied (in MD,
   this is called an Anderson thermostat).  This method is only
   intended to be used when the temperature is non-relativistic.

   For the pedants, this operator applies exactly (in exact
   arithmetic), the stochastic operator:
     du = -nu dt + sqrt((2kT)/(mc)) dW
   for the finite duration:
     sp->g->dt * interval
   every interval time step to all the particle momenta in the
   species.  Above, dW is a basic Weiner process. */

collision_op_t *
langevin( species_t * RESTRICT sp,
          mt_rng_t  **         rng,
          float                kT,
          float                nu,
          int                  interval );

/* In unary.c */

/* A unary_rate_constant_func_t returns the lab-frame rate constant for
   collisions between a monochromatic beam of particles (with rest mass
   sp->m) and momentum p->u{xyz} (normalized to sp->m sp->g->cvac where
   cvac is the speed of light in vacuum) against some background whose
   properties are determined by the specific collision model.

   The returned value has units of FREQUENCY.

   In the case of collisions with a static background of density
   n_background, the rate constant is:

     vi sigma(vi) n_background

   where vi is cvac |ui| / gamma_i is the physical velocity of particle
   vi in the lab frame and sigma is the model specific collision
   cross section for collisions of particles of that velocity with the
   background.

   The basic control flow for a unary_collision_func_t should be:

     void
     my_unary_rate_constant( my_collision_model_params_t * RESTRICT params,
                             species_t * RESTRICT sp,
                             particle_t * RESTRICT ALIGNED(32) p ) {
       return vi sigma(vi) n_background;
     } */

typedef float
(*unary_rate_constant_func_t)( /**/  void       * RESTRICT params,
                               const species_t  * RESTRICT sp,
                               const particle_t * RESTRICT ALIGNED(32) p );

/* A unary_collision_func_t implements the microscopic physics of a
   collision between a particle and some background whose properties
   are determined by the specific collision model.

   The basic control flow for a unary_collision_func_t should be:

     void
     my_unary_collide( my_collision_model_params_t * RESTRICT params,
                       const species_t * RESTRICT sp,
                       particle_t * RESTRICT ALIGNED(32) p,
                       mt_rng_t * RESTRICT rng ) {
       pi->u{xyz} = final momentum, ui{xyz} of particle i
                    given fluid background and initial momentum
                    pi->u{xyz}
     } */

typedef void
(*unary_collision_func_t)( /**/  void       * RESTRICT params,
                           const species_t  * RESTRICT sp,
                           /**/  particle_t * RESTRICT ALIGNED(32) p,
                           /**/  mt_rng_t   * RESTRICT rng );

/* Declare a unary collision model with the given microscopic physics. 
   params must be a registered object or NULL.  Every particle is
   tested for collision on every "interval" timesteps.  */

collision_op_t *
unary_collision_model( const char      * RESTRICT name,
                       unary_rate_constant_func_t rate_constant,
                       unary_collision_func_t     collision,
                       /**/  void      * RESTRICT params,
                       /**/  species_t * RESTRICT sp,
                       /**/  mt_rng_t  **         rng,
                       int                        interval );

/* In binary.c */

/* A binary_rate_constant_func_t returns the lab-frame rate constant
   for the collisions between a monochromatic beam of species i
   _physical_ particles (with mass spi->m) and momentum pi->u{xyz}
   (normalized to spi->m spi->g->cvac where cvac is the speed of light
   in vacuum) and a monochromatic beam of species j _physical_
   particles (with mass spj->m) with momentum pj->u{xyz}
   (normalized to spj->m spj->g->cvac).

   The returned value has units of VOLUME / TIME.

   For simple non-relativistic collisions, the rate constant, K, is
   related to the total collision cross section by:

     K = vr sigma(vr)

   where vr = cvac | ui - uj | and sigma( vr ) is the collision cross
   section for particles of that relative velocity.

   For relativistic collisions, this needs to be modified to:

     K = vr sigma( vr ) [ 1 - vi.vj / c^2 ]

   where vr = sqrt( |vi-vj|^2 - |vi x vj|^2/c^2 ) is the relative
   particle velocity in a frame in which one particle is at rest, vi
   = c ui / gamma_i is particle i's lab frame velocity, gamma_i =
   sqrt(1+ui^2) is particle i's lab frame relativistic factor and
   similarly for vj and gamma_j.  [CITE: Peano et al, ARXIV
   pre-print, 2009].

   This is both inefficient and numerically unsafe method to compute
   K in this regime (it misbehaves badly in finite precision for
   relativistic particles ... which is the whole point of doing this
   correct relativistically).  Relativistically, K is better
   computed via:

     s  = gamma_i gamma_j - ui.uj - 1
     vr = cvac sqrt{ s / [ s + 1/(2+s) ] }
     K  = vr sigma( vr ) (1+s) / (gamma_i gamma_j)

   which, provided s is computed with care, has _no_ catastropic
   cancellations, no near singular divisions, behaves well for
   non-relativistic particles and behaves wells for
   ultra-relativistic particles.

   For relativity afficinados, note that s is related to the Lorentz
   boost factor and has the manifestly covariant expression s =
   Ui.Uj - 1 where Ui = (gamma_i,ui) and Uj = (gamma_j,uj) are the
   normalized 4-momenta of particle i and particle j respectively
   and the Minkowski 4-dot product has a +--- signature. 

   The basic control flow for a unary_collision_func_t should be:

     void
     my_binary_rate_constant( my_collision_model_params_t * RESTRICT params,
                              const species_t * RESTRICT spi,
                              const species_t * RESTRICT spj,
                              const particle_t * RESTRICT ALIGNED(32) pi,
                              const particle_t * RESTRICT ALIGNED(32) pj ) {
       return vr sigma(vr);
     } */

typedef float
(*binary_rate_constant_func_t)( /**/  void       * RESTRICT params,
                                const species_t  * RESTRICT spi,
                                const species_t  * RESTRICT spj,
                                const particle_t * RESTRICT ALIGNED(32) pi,
                                const particle_t * RESTRICT ALIGNED(32) pj );

/* A binary_collision_func_t implements the microscopic physics of a
   collision between two particles, pi and pj.  The basic control
   flow for a binary_collision_func_t should be:

     void
     my_collide_func( my_collision_model_params_t * RESTRICT params,
                      const species_t * RESTRICT spi,
                      const species_t * RESTRICT spj,
                      particle_t * RESTRICT ALIGNED(32) pi,
                      particle_t * RESTRICT ALIGNED(32) pj,
                      mt_rng_t * RESTRICT rng,
                      int type ) {

       ... compute the final normalized momenta, ui{xyz} and uj{xyz}
       ... between two colliding _physical_ particles, one from
       ... species spi with initial normalized momenta pi->u{xyz} and
       ... the other from spj with initial momentum pj->u{xyz}.

       if( type & 1 ) pi->u{xyz} = ui{xyz};
       if( type & 2 ) pj->u{xyz} = uj{xyz};
     } */

typedef void
(*binary_collision_func_t)( /**/  void       * RESTRICT params,
                            const species_t  * RESTRICT spi,
                            const species_t  * RESTRICT spj,
                            /**/  particle_t * RESTRICT ALIGNED(32) pi,
                            /**/  particle_t * RESTRICT ALIGNED(32) pj,
                            /**/  mt_rng_t   * RESTRICT rng,
                            int type );

/* Declare a binary collision model with the given microscopic physics. 
   params must be a registered object or NULL.  A particle in a species
   will be tested for collision on average at least "sample" times every
   "interval" timesteps.  */

collision_op_t *
binary_collision_model( const char      * RESTRICT  name,
                        binary_rate_constant_func_t rate_constant,
                        binary_collision_func_t     collision,
                        /**/  void      * RESTRICT  params,
                        /**/  species_t * RESTRICT  spi,
                        /**/  species_t * RESTRICT  spj,
                        /**/  mt_rng_t  **          rng,
                        double                      sample,
                        int                         interval );

END_C_DECLS

#endif /* _collision_h_ */
