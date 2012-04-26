// Copyright (C) 2011, 2012 PISM Authors
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

#include "PO_delta_SMB.hh"

PO_delta_SMB::PO_delta_SMB(IceGrid &g, const NCConfigVariable &conf, PISMOceanModel* in)
  : PScalarForcing<PISMOceanModel,POModifier>(g, conf, in)
{
  option_prefix = "-ocean_delta_mass_flux_file";
  offset_name = "delta_mass_flux";
  offset = new Timeseries(&grid, offset_name, config.get_string("time_dimension_name"));
  offset->set_units("m s-1", "");
  offset->set_dimension_units(grid.time->units(), "");
  offset->set_attr("long_name", "ice-shelf-base mass flux offsets");
}

PetscErrorCode PO_delta_SMB::init(PISMVars &vars) {
  PetscErrorCode ierr;

  ierr = input_model->init(vars); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "* Initializing ice shelf base mass flux forcing using scalar offsets...\n"); CHKERRQ(ierr);

  ierr = init_internal(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PO_delta_SMB::shelf_base_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr = input_model->shelf_base_mass_flux(result); CHKERRQ(ierr);
  ierr = offset_data(result); CHKERRQ(ierr);
  return 0;
}
