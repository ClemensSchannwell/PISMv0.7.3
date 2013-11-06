// Copyright (C) 2011, 2012, 2013 PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cmath>
#include <cstring>
#include <petscdmda.h>
#include <assert.h>

#include "iceModel.hh"
#include "Mask.hh"
#include "PISMStressBalance.hh"
#include "PISMOcean.hh"


//! \file iMpartgrid.cc Methods implementing PIK option -part_grid [\ref Albrechtetal2011].

//! @brief Compute threshold thickness used when deciding if a
//! partially-filled cell should be considered 'full'.
PetscReal IceModel::get_threshold_thickness(planeStar<int> M,
                                            planeStar<PetscScalar> H,
                                            planeStar<PetscScalar> h,
                                            PetscScalar bed_elevation,
                                            bool reduce_frontal_thickness) {
  // get mean ice thickness and surface elevation over adjacent
  // icy cells
  PetscReal
    H_average   = 0.0,
    h_average   = 0.0,
    H_threshold = 0.0;
  PetscInt N = 0;
  Mask m;

  if (m.icy(M.e)) {
    H_average += H.e;
    h_average += h.e;
    N++;
  }

  if (m.icy(M.w)) {
    H_average += H.w;
    h_average += h.w;
    N++;
  }

  if (m.icy(M.n)) {
    H_average += H.n;
    h_average += h.n;
    N++;
  }

  if (m.icy(M.s)) {
    H_average += H.s;
    h_average += h.s;
    N++;
  }

  if (N == 0) {
    // If there are no "icy" neighbors, return the threshold thickness
    // of zero, forcing Href to be converted to H immediately.
    return 0;
  }

  H_average = H_average / N;
  h_average = h_average / N;

  if (bed_elevation + H_average > h_average)
    H_threshold = h_average - bed_elevation;
  else {
    H_threshold = H_average;
    // reduces the guess at the front
    if (reduce_frontal_thickness) {
      // FIXME: Magic numbers without references to the literature are bad.
      // for declining front C / Q0 according to analytical flowline profile in
      //   vandeveen with v0 = 300m / yr and H0 = 600m
      const PetscReal
        H0 = 600.0,                   // 600 m
        V0 = 300.0 / 3.15569259747e7, // 300 m/year (hard-wired for efficiency)
        mslope = 2.4511e-18 * grid.dx / (H0 * V0);
      H_threshold -= 0.8*mslope*pow(H_average, 5);
    }
  }

  return H_threshold;
}


//! Redistribute residual ice mass from subgrid-scale parameterization, when using -part_redist option.
/*!
  See [\ref Albrechtetal2011].  Manages the loop.

  FIXME: Reporting!

  FIXME: repeatRedist should be config flag?

  FIXME: resolve fixed number (=3) of loops issue
*/
PetscErrorCode IceModel::residual_redistribution(IceModelVec2S &H_residual) {
  PetscErrorCode ierr;
  const PetscInt max_loopcount = 3;

  bool done = false;
  int i = 0;
  while (done == false && i < max_loopcount) {
    ierr = residual_redistribution_iteration(H_residual, done); CHKERRQ(ierr);
    ierr = verbPrintf(4, grid.com, "redistribution loopcount = %d\n", i); CHKERRQ(ierr);
    i += 1;
  }

  return 0;
}


//! @brief This routine carries-over the ice mass when using
// -part_redist option, one step in the loop.
/**
 * @param[in,out] H_residual Residual Ice thickness. Updated in place.
 * @param[out] done set to 'true' if this was the last iteration we needed
 *
 * @return 0 on success
 */
PetscErrorCode IceModel::residual_redistribution_iteration(IceModelVec2S &H_residual, bool &done) {
  PetscErrorCode ierr;
  Mask mask;

  ierr = update_mask(vbed, vH, vMask); CHKERRQ(ierr);

  // First step: distribute residual ice thickness
  ierr = vMask.begin_access();      CHKERRQ(ierr);
  ierr = vH.begin_access();         CHKERRQ(ierr);
  ierr = vHref.begin_access();      CHKERRQ(ierr);
  ierr = H_residual.begin_access(); CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {

      if (H_residual(i,j) <= 0.0)
        continue;

      planeStar<int> m = vMask.int_star(i,j);
      PetscInt N = 0; // number of empty or partially filled neighbors
      planeStar<bool> neighbors;
      neighbors.set(false);

      if (mask.ice_free_ocean(m.e)) {
        N++;
        neighbors.e = true;
      }
      if (mask.ice_free_ocean(m.w)) {
        N++;
        neighbors.w = true;
      }
      if (mask.ice_free_ocean(m.n)) {
        N++;
        neighbors.n = true;
      }
      if (mask.ice_free_ocean(m.s)) {
        N++;
        neighbors.s = true;
      }

      if (N > 0)  {
        // Remaining ice mass will be redistributed equally among all
        // adjacent partially-filled cells (is there a more physical
        // way?)
        if (neighbors.e)
          vHref(i + 1, j) += H_residual(i, j) / N;
        if (neighbors.w)
          vHref(i - 1, j) += H_residual(i, j) / N;
        if (neighbors.n)
          vHref(i, j + 1) += H_residual(i, j) / N;
        if (neighbors.s)
          vHref(i, j - 1) += H_residual(i, j) / N;

        H_residual(i, j) = 0.0;
      } else {
        // Conserve mass, but (possibly) create a "ridge" at the shelf
        // front
        vH(i, j) += H_residual(i, j);
        H_residual(i, j) = 0.0;
      }

    } // j-loop
  } // i-loop
  ierr = vMask.end_access();      CHKERRQ(ierr);
  ierr = vH.end_access();         CHKERRQ(ierr);

  ierr = vH.update_ghosts(); CHKERRQ(ierr);

  // The loop above updated vH, so we need to re-calculate the mask:
  ierr = update_mask(vbed, vH, vMask); CHKERRQ(ierr);
  // and the surface elevation:
  ierr = update_surface_elevation(vbed, vH, vh); CHKERRQ(ierr);

  PetscScalar remaining_residual_thickness = 0.0,
    remaining_residual_thickness_global    = 0.0;

  // Second step: we need to redistribute residual ice volume if
  // neighbors which gained redistributed ice also become full.
  ierr = vH.begin_access();         CHKERRQ(ierr);
  ierr = vh.begin_access();         CHKERRQ(ierr);
  ierr = vbed.begin_access();       CHKERRQ(ierr);
  ierr = vMask.begin_access();      CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {
      if (vHref(i,j) <= 0.0)
        continue;

      PetscReal H_threshold = get_threshold_thickness(vMask.int_star(i, j),
                                                      vH.star(i, j),
                                                      vh.star(i, j),
                                                      vbed(i,j), true);

      PetscReal coverage_ratio = 1.0;
      if (H_threshold > 0.0)
        coverage_ratio = vHref(i, j) / H_threshold;
      if (coverage_ratio >= 1.0) {
        // The current partially filled grid cell is considered to be full
        H_residual(i, j) = vHref(i, j) - H_threshold;
        remaining_residual_thickness += H_residual(i, j);
        vH(i, j) += H_threshold;
        vHref(i, j) = 0.0;
      }

    }
  }
  ierr = vH.end_access();         CHKERRQ(ierr);
  ierr = vh.end_access();         CHKERRQ(ierr);
  ierr = vbed.end_access();       CHKERRQ(ierr);
  ierr = vMask.end_access();      CHKERRQ(ierr);
  ierr = vHref.end_access();      CHKERRQ(ierr);
  ierr = H_residual.end_access(); CHKERRQ(ierr);

  // check if redistribution should be run once more
  ierr = PISMGlobalSum(&remaining_residual_thickness,
                       &remaining_residual_thickness_global, grid.com); CHKERRQ(ierr);

  if (remaining_residual_thickness_global > 0.0) {
    done = false;
  } else {
    done = true;
  }

  ierr = vH.update_ghosts();         CHKERRQ(ierr);

  return 0;
}
