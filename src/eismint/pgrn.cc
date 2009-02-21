// Copyright (C) 2007-2009 Ed Bueler and Nathan Shemonski
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
  "Driver for PISM simulations based on EISMINT-Greenland intercomparison.\n";

#include <petsc.h>
#include "../base/grid.hh"
#include "../base/materials.hh"
#include "../base/iceModel.hh"
#include "../coupler/pccoupler.hh"
#include "iceGRNModel.hh"

int main(int argc, char *argv[]){
  PetscErrorCode ierr;

  MPI_Comm com;
  PetscMPIInt rank, size;
  
  ierr = PetscInitialize(&argc, &argv, PETSC_NULL, help); CHKERRQ(ierr);

  com = PETSC_COMM_WORLD;
  ierr = MPI_Comm_rank(com, &rank); CHKERRQ(ierr);
  ierr = MPI_Comm_size(com, &size); CHKERRQ(ierr);
  
  { // explicit scoping does deconstructors before PetscFinalize() 
    IceGrid g(com, rank, size);
    PISMEISGREENPDDCoupler ppdd;
    PISMConstOceanCoupler  pcoc;
 
    ierr = verbosityLevelFromOptions(); CHKERRQ(ierr);

    IceGRNModel    m(g);

    ierr = verbPrintf(1, com, "PGRN  (PISM EISMINT-Greenland mode)\n"); CHKERRQ(ierr);
    ierr = m.setExecName("pgrn"); CHKERRQ(ierr);

    ierr = m.attachAtmospherePCC(ppdd); CHKERRQ(ierr);
    ierr = m.attachEISGREENPDDPCC(ppdd); CHKERRQ(ierr);
    ierr = m.attachOceanPCC(pcoc); CHKERRQ(ierr);  // note this reports initialization to stdout even though
                                                   // EISMINT-Greenland has no ice shelves (-ocean_kill)

    ierr = m.setFromOptions(); CHKERRQ(ierr);
    ierr = m.initFromOptions(); CHKERRQ(ierr);
 
    ierr = m.run(); CHKERRQ(ierr);
    ierr = verbPrintf(2, com, "done with run ... \n"); CHKERRQ(ierr);

    ierr = m.writeFiles("grn_exper.nc"); CHKERRQ(ierr);
  }

  ierr = PetscFinalize(); CHKERRQ(ierr);
  return 0;
}
