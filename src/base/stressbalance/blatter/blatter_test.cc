// Copyright (C) 2011, 2012, 2013 PISM Authors
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
  "The executable for testing the Blatter stress balance solver.\n";

#include "BlatterStressBalance.hh"
#include "IceGrid.hh"
#include "PIO.hh"
#include "PISMVars.hh"
#include "flowlaws.hh"
#include "enthalpyConverter.hh"
#include "basal_resistance.hh"
#include "pism_options.hh"
#include "PISMTime.hh"
#include "POConstant.hh"

static PetscErrorCode get_grid_from_file(string filename, IceGrid &grid) {
  PetscErrorCode ierr;
  int grid_Mz = grid.Mz;
  double grid_Lz = grid.Lz;

  PIO nc(grid, "guess_mode");

  ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);
  ierr = nc.inq_grid("bedrock_altitude", &grid, NOT_PERIODIC); CHKERRQ(ierr);
  ierr = nc.close(); CHKERRQ(ierr);

  if (grid.Mz == 2) {
    grid.Mz = grid_Mz;
    grid.Lz = grid_Lz;
  }

  grid.compute_vertical_levels();
  grid.compute_nprocs();
  grid.compute_ownership_ranges();

  ierr = grid.allocate(); CHKERRQ(ierr);

  ierr = grid.printInfo(1); CHKERRQ(ierr);

  return 0;
}

//! \brief Read data from an input file.
static PetscErrorCode read_input_data(string filename, PISMVars &variables,
				      EnthalpyConverter &EC) {
  PetscErrorCode ierr;
  // Get the names of all the variables allocated earlier:
  set<string> vars = variables.keys();

  for (set<string>::iterator i = vars.begin(); i != vars.end(); ++i) {
    if (*i != "enthalpy") {
      ierr = variables.get(*i)->regrid(filename, true); CHKERRQ(ierr);
    } else {
      PetscReal T = 263.15, omega = 0.0, pressure = 0.0, E;
      EC.getEnth(T, omega, pressure, E);
      ierr = variables.get(*i)->set(E); CHKERRQ(ierr);
    }
  }

  return 0;
}

//! \brief Write data to an output file.
static PetscErrorCode write_data(string filename, PISMVars &variables) {
  PetscErrorCode ierr;
  // Get the names of all the variables allocated earlier:
  set<string> vars = variables.keys();

  for (set<string>::iterator i = vars.begin(); i != vars.end(); ++i) {
    ierr = variables.get(*i)->write(filename); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Allocate IceModelVec2S variables.
static PetscErrorCode allocate_variables(IceGrid &grid, PISMVars &variables) {
  PetscErrorCode ierr;
  IceModelVec2S *thk, *topg, *tauc;
  IceModelVec3 *enthalpy;

  thk  = new IceModelVec2S;
  topg = new IceModelVec2S;
  tauc = new IceModelVec2S;
  enthalpy = new IceModelVec3;

  ierr = thk->create(grid, "thk", true); CHKERRQ(ierr);
  ierr = thk->set_attrs("", "ice thickness",
			"m", "land_ice_thickness"); CHKERRQ(ierr);
  ierr = variables.add(*thk); CHKERRQ(ierr);

  ierr = topg->create(grid, "topg", true); CHKERRQ(ierr);
  ierr = topg->set_attrs("", "bedrock surface elevation",
			"m", "bedrock_altitude"); CHKERRQ(ierr);
  ierr = variables.add(*topg); CHKERRQ(ierr);

  ierr = tauc->create(grid, "tauc", true); CHKERRQ(ierr);
  ierr = tauc->set_attrs("diagnostic",
                         "yield stress for basal till (plastic or pseudo-plastic model)",
                         "Pa", ""); CHKERRQ(ierr);
  ierr = variables.add(*tauc); CHKERRQ(ierr);

  ierr = enthalpy->create(grid, "enthalpy", true, 1); CHKERRQ(ierr);
  ierr = enthalpy->set_attrs("model_state",
			     "ice enthalpy (includes sensible heat, latent heat, pressure)",
			     "J kg-1", ""); CHKERRQ(ierr);
  ierr = variables.add(*enthalpy); CHKERRQ(ierr);

  return 0;
}

//! \brief De-allocate IceModelVec2S variables.
static PetscErrorCode deallocate_variables(PISMVars &variables) {
  // Get the names of all the variables allocated earlier:
  set<string> vars = variables.keys();

  set<string>::iterator i = vars.begin();
  while (i != vars.end()) {
    IceModelVec *var = variables.get(*i);
    delete var;
    i++;
  }

  return 0;
}

int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;

  MPI_Comm    com;  // won't be used except for rank,size
  PetscMPIInt rank, size;
  PetscLogStage cold, hot;
  bool compare_cold_and_hot = false;

  ierr = PetscInitialize(&argc, &argv, PETSC_NULL, help); CHKERRQ(ierr);

  ierr = PetscLogStageRegister("Cold", &cold); CHKERRQ(ierr);
  ierr = PetscLogStageRegister("Hot ", &hot); CHKERRQ(ierr);

  com = PETSC_COMM_WORLD;
  ierr = MPI_Comm_rank(com, &rank); CHKERRQ(ierr);
  ierr = MPI_Comm_size(com, &size); CHKERRQ(ierr);

  /* This explicit scoping forces destructors to be called before PetscFinalize() */
  {
    NCConfigVariable config, overrides;
    ierr = init_config(com, rank, config, overrides); CHKERRQ(ierr);
    ierr = set_config_from_options(com, config); CHKERRQ(ierr);
    ierr = setVerbosityLevel(2);

    ierr = verbPrintf(2, com,
                      "BLATTER_TEST: testing the Blatter stress balance solver.\n"); CHKERRQ(ierr);

    PetscBool usage_set, help_set;
    ierr = PetscOptionsHasName(PETSC_NULL, "-usage", &usage_set); CHKERRQ(ierr);
    ierr = PetscOptionsHasName(PETSC_NULL, "-help", &help_set); CHKERRQ(ierr);
    if ((usage_set==PETSC_TRUE) || (help_set==PETSC_TRUE)) {
      PetscPrintf(com,
                  "\n"
                  "usage of BLATTER_TEST:\n"
                  "  run siafd_test -i input.nc -o output.nc\n"
                  "\n");
    }

    IceGrid grid(com, rank, size, config);

    string input_file, output_file = "blatter_test.nc";
    ierr = PetscOptionsBegin(grid.com, "", "BLATTER_TEST options", ""); CHKERRQ(ierr);
    {
      bool flag;
      int Mz;
      double Lz;
      ierr = PISMOptionsString("-i", "Set the input file name",
                               input_file, flag); CHKERRQ(ierr);
      if (! flag) {
        PetscPrintf(grid.com, "BLATTER_TEST ERROR: -i is required.\n");
        PISMEnd();
      }
      ierr = PISMOptionsString("-o", "Set the output file name",
                               output_file, flag); CHKERRQ(ierr);
      ierr = PISMOptionsIsSet("-compare", "Compare \"cold\" and \"hot\" runs.",
			      compare_cold_and_hot); CHKERRQ(ierr);
      ierr = PISMOptionsInt("-Mz", "Number of vertical levels in the PISM grid",
			    Mz, flag); CHKERRQ(ierr);
      if (flag == true) {
	grid.Mz = Mz;
      }

      ierr = PISMOptionsReal("-Lz", "Vertical extent of the PISM grid",
			     Lz, flag); CHKERRQ(ierr);
      if (flag == true) {
	grid.Lz = Lz;;
      }
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    ierr = get_grid_from_file(input_file, grid); CHKERRQ(ierr);

    PISMVars variables;
    ierr = allocate_variables(grid, variables); CHKERRQ(ierr);

    EnthalpyConverter EC(config);

    // This is never used (but it is a required argument of the
    // PISMStressBalance constructor).
    // It will be used eventually, though.
    IceBasalResistancePlasticLaw basal(config, grid.get_unit_system());

    POConstant ocean(grid, config);

    ierr =  read_input_data(input_file, variables, EC); CHKERRQ(ierr);

    PetscLogStagePush(cold);
    BlatterStressBalance blatter(grid, basal, EC, config);
    // Initialize the Blatter solver:
    ierr = blatter.init(variables); CHKERRQ(ierr);
    ierr = blatter.update(false); CHKERRQ(ierr);
    PetscLogStagePop();

    if (compare_cold_and_hot) {
      PetscLogStagePush(hot);
      ierr = blatter.update(false); CHKERRQ(ierr);
      PetscLogStagePop();
    }

    // Write results to an output file:
    PIO pio(grid, grid.config.get_string("output_format"));

    ierr = pio.open(output_file, PISM_WRITE); CHKERRQ(ierr);
    ierr = pio.def_time(config.get_string("time_dimension_name"),
                        config.get_string("calendar"),
                        grid.time->CF_units_string()); CHKERRQ(ierr);
    ierr = pio.append_time(config.get_string("time_dimension_name"), 0.0);

    set<string> blatter_vars;
    blatter_vars.insert("u_sigma");
    blatter_vars.insert("v_sigma");
    ierr = blatter.define_variables(blatter_vars, pio, PISM_DOUBLE); CHKERRQ(ierr);
    ierr = blatter.write_variables(blatter_vars, pio); CHKERRQ(ierr);

    ierr = pio.close(); CHKERRQ(ierr);

    ierr = write_data(output_file, variables); CHKERRQ(ierr);

    IceModelVec3 *u, *v;
    IceModelVec2V *velbar;
    ierr = blatter.get_horizontal_3d_velocity(u, v); CHKERRQ(ierr);
    ierr = blatter.get_2D_advective_velocity(velbar); CHKERRQ(ierr);

    ierr = u->write(output_file); CHKERRQ(ierr);
    ierr = v->write(output_file); CHKERRQ(ierr);
    ierr = velbar->write(output_file); CHKERRQ(ierr);


    ierr =  deallocate_variables(variables); CHKERRQ(ierr);

  }
  ierr = PetscFinalize(); CHKERRQ(ierr);
  return 0;
}
