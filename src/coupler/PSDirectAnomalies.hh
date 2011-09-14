// Copyright (C) 2011 Constantine Khroulev
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

#ifndef _PSDirectANOMALIES_H_
#define _PSDirectANOMALIES_H_

#include "PASDirectForcing.hh"

class PSDirectAnomalies : public PSDirectForcing
{
public:
  PSDirectAnomalies(IceGrid &g, const NCConfigVariable &conf)
    : PSDirectForcing(g, conf) {}
  virtual ~PSDirectAnomalies() {}

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years);
protected:
  IceModelVec2S mass_flux_0, mass_flux_in;
};

#endif /* _PSDirectANOMALIES_H_ */
