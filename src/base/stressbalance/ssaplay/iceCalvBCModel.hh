// Copyright (C) 2004-2011 Jed Brown, Ed Bueler and Constantine Khroulev
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

#ifndef __iceCalvBCModel_hh
#define __iceCalvBCModel_hh

#include <petscda.h>
#include <petscmat.h>
#include "iceModelVec.hh"
#include "iceExactSSAModel.hh"

class IceCalvBCModel : public IceExactSSAModel {
public:
    IceCalvBCModel(IceGrid &g, NCConfigVariable &config, NCConfigVariable &overrides, int mytest);
    ~IceCalvBCModel();
  virtual PetscErrorCode createVecs();
    PetscErrorCode writeCFfields(const char* default_filename);

protected:
    PetscErrorCode assembleSSAMatrix(const bool includeBasalShear,
                                     IceModelVec2S vNuH[2], Mat A);
    PetscErrorCode assembleSSARhs(Vec rhs);

    // CF = calving front
    IceModelVec2S   vsmoothCFmask;  // gradient of this gives normal dir to
                               // calving front, namely  ...
    IceModelVec2S   vnCF[2];    // ... this pair
    PetscErrorCode compute_nCF();
};

#endif /* __iceCalvBCModel_hh */

