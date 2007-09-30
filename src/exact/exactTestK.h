/*
   Copyright (C) 2007 Ed Bueler
  
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

#ifndef __exactTestK_h
#define __exactTestK_h 1

#ifdef __cplusplus
extern "C"
{
#endif

/*
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
! exactTestK is a C implementation of an exact solution to a time-dependent
! pure conduction problem in a column of ice and bedrock in an incomplete
! preprint
!
!    Ed Bueler (September 2007) "An exact solution to the temperature 
!    equation in a column of ice and bedrock"
!
! see also src/exact/simpleK.c
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
*/

/* compute the exact solution TT = T(t,z) */
int exactK(const double t, const double z, double *TT);

/* find the alpha_k values for the eigenfunction expansion in the exact
   solution; these values are found by rigorous (bracketed) numerical 
   solution of a one-variable root-finding problem */
int print_alpha_k(const int N);

#ifdef __cplusplus
}
#endif


#endif  /* __exactTestK_h */

