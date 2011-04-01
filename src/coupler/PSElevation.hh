// Copyright (C) 2011 Andy Aschwanden and Constantine Khroulev
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

#ifndef _PSELEVATION_H_
#define _PSELEVATION_H_

#include "PISMSurface.hh"
#include "iceModelVec2T.hh"

//! \brief A class implementing a elevation-dependent temperature and mass balance model.

class PSElevation : public PISMSurfaceModel {
public:
  PSElevation(IceGrid &g, const NCConfigVariable &conf)
    : PISMSurfaceModel(g, conf)
  {};

  virtual PetscErrorCode init(PISMVars &vars);
  //! This surface model does not use an atmosphere model.
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input)
  { delete input; }

  // Does not have an atmosphere model.
  virtual void get_diagnostics(map<string, PISMDiagnostic*> &/*dict*/) {}

  virtual PetscErrorCode ice_surface_mass_flux(PetscReal t_years, PetscReal dt_years,
						 IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(PetscReal t_years, PetscReal dt_years,
						 IceModelVec2S &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
  virtual void add_vars_to_output(string keyword, set<string> &result);
protected:
  IceModelVec2S acab, artm;
  IceModelVec2S *usurf;
  PetscReal T_ELA, z_ELA,z_min,z_max,dTdz,dacdz,dabdz;
  PetscTruth elev_set;

};

#endif /* _PSELEVATION_H_ */
