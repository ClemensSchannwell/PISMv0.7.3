// Copyright (C) 2007 Ryan Woodard

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

#ifndef __iceRYANModel_hh
#define __iceRYANModel_hh

#include <petscda.h>
/* #include <vector> */
#include "../base/grid.hh"
#include "../base/materials.hh"
#include "../eismint/iceEISModel.hh"

struct RandomnessACML {
  PetscInt    lseed, lstate, n, genid, subid, i, info;
  IS          seed, state;
  PetscScalar xmu, var;
  Vec         x;
};


class IceRYANModel : public IceEISModel {

public:
  IceRYANModel(IceGrid &g, IceType &i);
  virtual PetscErrorCode setFromOptions();
  virtual PetscErrorCode initFromOptions();

protected:
  RandomnessACML randomnessacml;
  PetscErrorCode initRandomnessACML();
  virtual PetscErrorCode additionalAtStartTimestep(); // formerly "perturbAcc()"
  virtual PetscErrorCode summaryPrintLine(
    const PetscTruth printPrototype, const PetscTruth tempAndAge,
    const PetscScalar year, const PetscScalar dt, 
    const PetscInt tempskipCount, const char adaptReason,
    const PetscScalar volume_kmcube, const PetscScalar area_kmsquare,
    const PetscScalar meltfrac, const PetscScalar H0, const PetscScalar T0);

private:
  char        accname;
  PetscInt    mySeed;
};

#endif /* __iceRYANModel_hh */

