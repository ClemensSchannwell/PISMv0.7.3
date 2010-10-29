// Copyright (C) 2010 Constantine Khroulev and Ed Bueler
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
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

#include "SSB_Modifier.hh"

//! \brief Perform the SIA computation.
PetscErrorCode SIA::update(bool fast) {
  PetscErrorCode ierr;

  ierr = compute_surface_gradient(); CHKERRQ(ierr);

  ierr = compute_sia_flux(fast); CHKERRQ(ierr);

  if (!fast) {
    ierr = compute_horizontal_3d_velocity(); CHKERRQ(ierr);
  }

  return 0;
}

