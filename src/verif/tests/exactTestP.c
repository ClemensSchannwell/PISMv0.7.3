/*
   Copyright (C) 2012 Ed Bueler

   This file is part of PISM.

   PISM is free software; you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software
   Foundation; either version 2 of the License, or (at your option) any later
   version.

   PISM is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
   details.

   You should have received a copy of the GNU General Public License
   along with PISM; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_odeiv2.h>
#include "exactTestP.h"

#define SperA    31556926.0    /* seconds per year; 365.2422 days */
#define g        9.81          /* m s-2 */
#define rhoi     910.0         /* kg m-3 */
#define rhow     1000.0        /* kg m-3 */

/* major model parameters: */
#define Aglen    3.1689e-24    /* Pa-3 s-1 */
#define K        0.01          /* m s-1 */
#define Wr       1.0           /* m */
#define c1       0.500         /* m-1 */
#define c2       0.040         /* [pure] */

/* model regularizations */
#define E0       1.0           /* m */
#define Y0       0.001         /* m */

/* specific to exact solution */
#define Phi0     (0.20 / SperA) /* m s-1 */
#define h0       500.0         /* m */
#define v0       (100.0 / SperA) /* m s-1 */
#define R1       5000.0        /* m */


int getsb(double r, double *sb, double *dsbdr) {
  double CC, CZ, CD, zz;
  if (r < R1) {
    *sb    = 0.0;
    *dsbdr = 0.0;
  } else {
    CC     = pow( (c1 * v0) / (c2 * Aglen * pow((L - R1),5.0)) , (1.0/3.0) );
    *sb    = CC * pow(r - R1, (5.0/3.0));
    *dsbdr = (5.0/3.0) * CC * pow(r - R1, (2.0/3.0));
  }
  return 0;
}


double criticalW(double r) {
  double h = h0 * (1.0 - (r/R0) * (r/R0)),
         Po = rhoi * g * h,
         sb, dsb, sbcube, Pocube;
  getsb(r,&sb,&dsb);
  sbcube = sb * sb * sb;
  Pocube = Po * Po * Po;
  return ((sbcube * Wr - Pocube * Y0) / (sbcube + Pocube));
}


int funcP(double r, const double W[], double f[], void *params) {
  /* Computes RHS f(r,W) for differential equation as given in dampnotes.pdf
  at https://github.com/bueler/hydrolakes:
      dW
      -- = f(r,W)
      dr
  Compare doublediff.m.  Assumes Glen power n=3.
  */

  double sb, dsb, dPo, tmp1, c0, vphi0, numer, denom;

  if (params == NULL) {} /* quash warning "unused parameters" */

  if (r < 0.0) {
    f[0] = 0.0;  /* place-holder */
    return TESTP_R_NEGATIVE;
  } else if (r > L) {
    f[0] = 0.0;  /* place-holder */
    return TESTP_R_EXCEEDS_L;
  } else {
    getsb(r,&sb,&dsb);
    c0    = K / (rhow * g);
    vphi0 = Phi0 / (2 * c0);
    dPo   = - (2.0 * rhoi * g * h0 / (R0*R0)) * r;
    tmp1  = pow(W[0] + Y0,4.0/3.0) * pow(Wr - W[0],2.0/3.0);
    numer = dsb * (W[0] + Y0) * (Wr - W[0]);
    numer = numer - ( vphi0 * r / W[0] + dPo ) * tmp1;
    denom = (1.0/3.0) * (Wr + Y0) * sb + rhow * g * tmp1;
    f[0] = numer / denom;
    return GSL_SUCCESS;
  }
}


/* Computes initial condition W(r=L) = W_c(L^-). */
double initialconditionW() {
  double hL, PoL, sbL;
  hL  = h0 * (1.0 - (L/R0) * (L/R0));
  PoL = rhoi * g * hL;
  sbL = pow( c1 * v0 / (c2 * Aglen), 1.0/3.0);
  return (pow(sbL,3.0) * Wr - pow(PoL,3.0) * Y0) / (pow(sbL,3.0) + pow(PoL,3.0));
}


/* Solves ODE for W(r), the exact solution.  Input r[] and output W[] must be
allocated vectors of length N.  Input r[] must be decreasing.  The combination
EPS_ABS = 1e-12, EPS_REL=0.0, method = RK Dormand-Prince O(8)/O(9)
is believed for now to be predictable and accurate.  Note hstart is negative
so that the ODE solver does negative steps.  Assumes
   0 <= r[N-1] < r[N-2] < ... < r[1] < r[0] < L.                            */
int getW(double *r, int N, double *W,
         const double EPS_ABS, const double EPS_REL, const int ode_method) {
   int i, count;
   int status = TESTP_NOT_DONE;
   double rr, hstart;
   const gsl_odeiv2_step_type* Tpossible[4] =
           { gsl_odeiv2_step_rk8pd, gsl_odeiv2_step_rk2,
             gsl_odeiv2_step_rkf45, gsl_odeiv2_step_rkck };
   const gsl_odeiv2_step_type *T;

   /* check first: we have a list, r is decreasing, r is in range [0,R0] */
   if (N < 1) return TESTP_NO_LIST;
   if (r[0] >= L) return TESTP_R_EXCEEDS_L;
   for (i = 1; i<N; i++) {
     if (r[i] >= r[i-1]) return TESTP_LIST_NOT_DECREASING;
     if (r[i] < 0.0)     return TESTP_R_NEGATIVE;
   }

   /* setup for GSL ODE solver; these choices don't need Jacobian */
   if ((ode_method > 0) && (ode_method < 5))
     T = Tpossible[ode_method-1];
   else {
     printf("INVALID ode_method in getW(): must be 1,2,3,4\n");
     return TESTP_INVALID_METHOD;
   }

   gsl_odeiv2_system  sys = {funcP, NULL, 1, NULL};  /* Jac-free method and no params */
   hstart = (L - r[0]) < 1000.0 ? (r[0] - L) : -1000.0;
   gsl_odeiv2_driver *d = gsl_odeiv2_driver_alloc_y_new(&sys, T, hstart, EPS_ABS, EPS_REL);

   /* initial conditions: (r,W) = (R0,W_c(L^-));  r decreases from L toward 0 */
   rr = L;
   for (count = 0; count < N; count++) {
     /* except at start, use value at end of last interval as initial value for subinterval */
     W[count] = (count == 0) ? initialconditionW() : W[count-1];
     while (rr > r[count]) {
       status = gsl_odeiv2_driver_apply(d, &rr, r[count], &(W[count]));
       if (W[count] > Wr) {
         return TESTP_W_EXCEEDS_WR;
       } else if (W[count] < criticalW(r[count])) {
         return TESTP_W_BELOW_WCRIT;
       }
       if (status != GSL_SUCCESS) {
         printf("gsl_odeiv2_driver_apply() returned status = %d\n",status);
         break;
       }
     }
   }

   gsl_odeiv2_driver_free (d);
   return status;
}


int exactP(double r, double *h, double *magvb, double *Wcrit, double *W,
           const double EPS_ABS, const double EPS_REL, const int ode_method) {

  if (r < 0.0) return TESTP_R_NEGATIVE;
  if (r > L)   return TESTP_R_EXCEEDS_L;

  *h = h0 * (1.0 - (r/R0) * (r/R0));
  if (r > R1)
    *magvb = v0 * pow((r - R1)/(R0 - R1),5.0);
  else
    *magvb = 0.0;
  *Wcrit = criticalW(r);

  return getW(&r,1,W,EPS_ABS,EPS_REL,ode_method);
}

#if 0
int exactP_list(double *r, int N, double *h, double *magvb, double *W, 
           const double EPS_ABS, const double EPS_REL, const int ode_method);
  /* N values r[0] > r[1] > ... > r[N-1]  (decreasing)
     assumes r, h, magvb, W are allocated length N arrays  */

  double *W;
  int stat, i;

  W = (double *) malloc((size_t)N * sizeof(double)); /* temporary arrays */

  /* combination EPS_ABS = 1e-12, EPS_REL=0.0, method = 1 = RK Cash-Karp
     believed to be predictable and accurate */
FIXME  stat = getW(r,N,FIXME,1.0e-12,0.0,1);
  if (stat != GSL_SUCCESS) {
    return stat;
  }

  for (i = 0; i < N; i++) {
    h[i] = FIXME
    magvb[i] = FIXME
  }

  free(W);
  return 0;
}
#endif


int error_message_testP(int status) {
  switch (status) {
    case TESTP_R_EXCEEDS_L:
      printf("error in Test P: r exceeds L\n");
      break;
    case TESTP_R_NEGATIVE:
      printf("error in Test P: r < 0\n");
      break;
    case TESTP_W_EXCEEDS_WR:
      printf("error in Test P: W > W_r\n");
      break;
    case TESTP_W_BELOW_WCRIT:
      printf("error in Test P: W < W_crit\n");
      break;
    case TESTP_INVALID_METHOD:
      printf("error in Test P: invalid choice for ODE method\n");
      break;
    case TESTP_NOT_DONE:
      printf("error in Test P: ODE integrator not done\n");
      break;
    case TESTP_NO_LIST:
      printf("error in Test P: no list of r values at input to exactP_list()\n");
      break;
    case TESTP_LIST_NOT_DECREASING:
      printf("error in Test P: input list of r values to exactP_list() is not decreasing\n");
      break;
    default:
      if (status > 0) printf("unknown error status in Test P\n");
  }
  return 0;
}

