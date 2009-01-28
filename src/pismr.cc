// Copyright (C) 2004-2009 Jed Brown, Ed Bueler and Constantine Khroulev
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

static char help[] =
  "Ice sheet driver for PISM ice sheet simulations initialized from data.\n";

#include <petsc.h>
#include "base/grid.hh"
#include "base/materials.hh"
#include "base/iceModel.hh"

int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;

  MPI_Comm    com;
  PetscMPIInt rank, size;

  ierr = PetscInitialize(&argc, &argv, PETSC_NULL, help); CHKERRQ(ierr);

  com = PETSC_COMM_WORLD;
  ierr = MPI_Comm_rank(com, &rank); CHKERRQ(ierr);
  ierr = MPI_Comm_size(com, &size); CHKERRQ(ierr);

  /* This explicit scoping forces destructors to be called before PetscFinalize() */
  {
    IceGrid g(com, rank, size);
    IceType*   ice = PETSC_NULL;

    ierr = verbosityLevelFromOptions(); CHKERRQ(ierr);
    ierr = verbPrintf(1,com, "PISMR (basic evolution run mode)\n"); CHKERRQ(ierr);
    
    ierr = userChoosesIceType(com, ice); CHKERRQ(ierr); // allocates ice
    IceModel m(g, ice);
    ierr = m.setExecName("pismr"); CHKERRQ(ierr);
    ierr = m.setFromOptions(); CHKERRQ(ierr);
    ierr = m.initFromOptions(); CHKERRQ(ierr);

    ierr = verbPrintf(2,com, "running ...\n"); CHKERRQ(ierr);
    ierr = m.run(); CHKERRQ(ierr);

    ierr = verbPrintf(2,com, "... done with run\n"); CHKERRQ(ierr);

    // We provide a default base name if no -o option.
    ierr = m.writeFiles("unnamed.nc"); CHKERRQ(ierr);

    delete ice;
  }

  ierr = PetscFinalize(); CHKERRQ(ierr);
  return 0;
}
