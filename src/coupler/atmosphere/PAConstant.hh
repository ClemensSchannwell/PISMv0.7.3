// Copyright (C) 2011 PISM Authors
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

#ifndef _PACONSTANT_H_
#define _PACONSTANT_H_

#include "iceModelVec.hh"
#include "PISMAtmosphere.hh"

//! \brief A class implementing a constant-in-time atmosphere model. Reads data
//! from a PISM input file.
class PAConstant : public PISMAtmosphereModel {
public:
  PAConstant(IceGrid &g, const NCConfigVariable &conf)
    : PISMAtmosphereModel(g, conf) {};
  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal my_t, PetscReal my_dt)
  { t = my_t; dt = my_dt; return 0; } // do nothing
  virtual PetscErrorCode mean_precip(IceModelVec2S &result);
  virtual PetscErrorCode mean_annual_temp(IceModelVec2S &result);
  virtual PetscErrorCode begin_pointwise_access();
  virtual PetscErrorCode end_pointwise_access();
  virtual PetscErrorCode temp_time_series(int i, int j, int N,
					  PetscReal *ts, PetscReal *values);
  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
  virtual PetscErrorCode temp_snapshot(IceModelVec2S &result);
protected:
  string input_file;
  IceModelVec2S precip, temperature;
  NCSpatialVariable airtemp_var;
};

#endif /* _PACONSTANT_H_ */
