// Copyright (C) 2012 PISM Authors
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

#ifndef _PISMHYDROLOGY_H_
#define _PISMHYDROLOGY_H_

#include "PISMComponent.hh"
#include "iceModelVec.hh"

//! \brief The PISM subglacial hydrology model interface.
/*!
THIS IS A DRAFT!!

PISMHydrology is a timestepping component (PISMComponent_TS) but it does not use
PISM's main ice dynamics time steps.  Rather, when update() is called it advances
its internal time to the new goal t+dt using its own internal time steps, and
using the ice geometry and basal sliding velocity as time-independent (i.e.
explicit or one-way coupled) fields.  Thus the frequency of coupling is determined
by the agent that calls the update() method.

Perhaps this will be a virtual base class.  There are two prospective implementations,
one being the old bwat diffusion currently implemented in iMhydrology.cc and the
other being the new van Pelt & Bueler model documented at
  https://github.com/bueler/hydrolakes
For now, it is just implementing the new model.
 */
class PISMHydrology : public PISMComponent_TS
{
public:
  PISMHydrology(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PISMHydrology() {}

/* FIXME:
  A PISM component needs to implement the following I/O methods:
  \li add_vars_to_output(), which adds variable names to the list of fields that need
  to be written.
  \li define_variables(), which defines variables to be written and writes variable metadata.
  \li write_variables(), which writes data itself.
*/

  virtual PetscErrorCode init(PISMVars &vars);

  using PISMComponent_TS::update;
  virtual PetscErrorCode update(PetscReal my_t, PetscReal my_dt);

  virtual PetscErrorCode water_layer_thickness(IceModelVec2S &result);

  virtual PetscErrorCode water_pressure(IceModelVec2S &result);

protected:
  // this model's state
  IceModelVec2S W,      // water layer thickness
                P;      // water pressure
  // this model's auxiliary variables
  IceModelVec2S Po,     // overburden pressure
                cbase,  // sliding speed of overlying ice
                psi;    // hydraulic potential
  IceModelVec2Int known;// mask for (boundary) locations where subglacial hydrology state is known
  IceModelVec2Stag V,   // components are
                        //   V(i,j,0) = alpha(i,j) = east-edge centered  x-component of water velocity
                        //   V(i,j,1) = beta(i,j)  = north-edge centered y-component of water velocity
                   Wstag,// edge-centered (staggered) W values (averaged from regular)
                   Qstag;// edge-centered (staggered) advection fluxes
  // this model's workspace variables
  IceModelVec2S Wnew, Pnew;
  // pointers into IceModel; these describe the ice sheet and the source
  IceModelVec2S *bed,   // bedrock elevation
                *thk,   // ice thickness
                *usurf, // ice surface elevation
                *bmelt; // ice sheet basal melt rate
  IceModelVec2V *Ubase; // ice sliding velocity

  PISMVars *variables;

  PetscReal standard_gravity, ice_density, fresh_water_density;
  PetscReal c1, c2, K, Aglen, nglen, Wr, c0, E0, Y0;

  virtual PetscErrorCode allocate();

  virtual PetscErrorCode update_ice_functions(IceModelVec2S &result_Po, IceModelVec2S &result_cbase);
  virtual PetscErrorCode P_from_W_steady(IceModelVec2S &result);
  virtual PetscErrorCode velocity_staggered(IceModelVec2Stag &result);

};

#endif /* _PISMHYDROLOGY_H_ */

