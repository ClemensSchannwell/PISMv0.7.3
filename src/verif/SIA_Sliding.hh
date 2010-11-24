// Copyright (C) 2004--2010 Jed Brown, Ed Bueler and Constantine Khroulev
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

#ifndef _SIA_SLIDING_H_
#define _SIA_SLIDING_H_

#include "ShallowStressBalance.hh"

/*!
 * This class implements an SIA sliding law.
 *
 * It is used by pismv test E \b only, hence the code dumplication (the surface
 * gradient code is from SIAFD).
 */
class SIA_Sliding : public ShallowStressBalance
{
public:
  SIA_Sliding(IceGrid &g, IceBasalResistancePlasticLaw &b, IceFlowLaw &i, EnthalpyConverter &e,
              const NCConfigVariable &conf)
    : ShallowStressBalance(g, b, i, e, conf) {}
  virtual ~SIA_Sliding() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode update(bool fast);

protected:
  virtual PetscErrorCode compute_surface_gradient(IceModelVec2Stag &h_x, IceModelVec2Stag &h_y);

  virtual PetscErrorCode surface_gradient_eta(IceModelVec2Stag &h_x, IceModelVec2Stag &h_y);
  virtual PetscErrorCode surface_gradient_haseloff(IceModelVec2Stag &h_x, IceModelVec2Stag &h_y);
  virtual PetscErrorCode surface_gradient_mahaffy(IceModelVec2Stag &h_x, IceModelVec2Stag &h_y);

  virtual PetscScalar basalVelocitySIA(PetscScalar /*x*/, PetscScalar /*y*/,
                                       PetscScalar H, PetscScalar T,
                                       PetscScalar /*alpha*/, PetscScalar mu,
                                       PetscScalar min_T) const;
  IceModelVec2Mask *mask;
  IceModelVec2S *thickness, *surface, *bed, work_2d;
  IceModelVec3 *enthalpy;
  IceModelVec2Stag work_2d_stag[2]; // for the surface gradient
  double standard_gravity;
};

#endif /* _SIA_SLIDING_H_ */
