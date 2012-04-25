// Copyright (C) 2008-2012 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
// Gudfinna Adalgeirsdottir and Andy Aschwanden
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

// Implementation of the atmosphere model using constant-in-time precipitation
// and a cosine yearly cycle for near-surface air temperatures.

#include "PAYearlyCycle.hh"
#include "PISMTime.hh"
#include "IceGrid.hh"

//! Allocates memory and reads in the precipitaion data.
PetscErrorCode PAYearlyCycle::init(PISMVars &vars) {
  PetscErrorCode ierr;
  bool regrid = false;
  int start = -1;

  variables = &vars;

  snow_temp_july_day = config.get("snow_temp_july_day");

  // Allocate internal IceModelVecs:
  ierr = temp_ma.create(grid, "airtemp_ma", false); CHKERRQ(ierr);
  ierr = temp_ma.set_attrs("diagnostic",
			   "mean annual near-surface air temperature (without sub-year time-dependence or forcing)",
			   "K", 
			   ""); CHKERRQ(ierr);  // no CF standard_name ??
  ierr = temp_ma.set_attr("source", reference);

  ierr = temp_mj.create(grid, "airtemp_mj", false); CHKERRQ(ierr);
  ierr = temp_mj.set_attrs("diagnostic",
			   "mean July near-surface air temperature (without sub-year time-dependence or forcing)",
			   "Kelvin",
			   ""); CHKERRQ(ierr);  // no CF standard_name ??
  ierr = temp_mj.set_attr("source", reference);

  ierr = precipitation.create(grid, "precipitation", false); CHKERRQ(ierr);
  ierr = precipitation.set_attrs("climate_state", 
			      "mean annual ice-equivalent precipitation rate",
			      "m s-1", 
			      ""); CHKERRQ(ierr); // no CF standard_name ??
  ierr = precipitation.set_glaciological_units("m year-1");
  precipitation.write_in_glaciological_units = true;
  precipitation.time_independent = true;

  ierr = find_pism_input(precip_filename, regrid, start); CHKERRQ(ierr);

  // read precipitation rate from file
  ierr = verbPrintf(2, grid.com, 
		    "    reading mean annual ice-equivalent precipitation rate 'precipitation'\n"
		    "      from %s ... \n",
		    precip_filename.c_str()); CHKERRQ(ierr); 
  if (regrid) {
    ierr = precipitation.regrid(precip_filename.c_str(), true); CHKERRQ(ierr); // fails if not found!
  } else {
    ierr = precipitation.read(precip_filename.c_str(), start); CHKERRQ(ierr); // fails if not found!
  }
  string precip_history = "read from " + precip_filename + "\n";

  ierr = precipitation.set_attr("history", precip_history); CHKERRQ(ierr);

  airtemp_var.init_2d("airtemp", grid);
  airtemp_var.set_string("pism_intent", "diagnostic");
  airtemp_var.set_string("long_name",
                         "snapshot of the near-surface air temperature");
  ierr = airtemp_var.set_units("K"); CHKERRQ(ierr);

  return 0;
}

void PAYearlyCycle::add_vars_to_output(string keyword, map<string,NCSpatialVariable> &result) {
  result["precipitation"] = precipitation.get_metadata();

  if (keyword == "big") {
    result["airtemp_ma"] = temp_ma.get_metadata();
    result["airtemp_mj"] = temp_mj.get_metadata();
    result["airtemp"] = airtemp_var;
  }
}

PetscErrorCode PAYearlyCycle::define_variables(set<string> vars, const PIO &nc, PISM_IO_Type nctype) {
  PetscErrorCode ierr;

  if (set_contains(vars, "airtemp")) {
    ierr = airtemp_var.define(nc, nctype, false); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_ma")) {
    ierr = temp_ma.define(nc, nctype); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_mj")) {
    ierr = temp_mj.define(nc, nctype); CHKERRQ(ierr);
  }

  if (set_contains(vars, "precipitation")) {
    ierr = precipitation.define(nc, nctype); CHKERRQ(ierr);
  }

  return 0;
}


PetscErrorCode PAYearlyCycle::write_variables(set<string> vars, string filename) {
  PetscErrorCode ierr;

  if (set_contains(vars, "airtemp")) {
    IceModelVec2S airtemp;
    ierr = airtemp.create(grid, "airtemp", false); CHKERRQ(ierr);
    ierr = airtemp.set_metadata(airtemp_var, 0); CHKERRQ(ierr);

    ierr = temp_snapshot(airtemp); CHKERRQ(ierr);

    ierr = airtemp.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_ma")) {
    ierr = temp_ma.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "airtemp_mj")) {
    ierr = temp_mj.write(filename.c_str()); CHKERRQ(ierr);
  }

  if (set_contains(vars, "precipitation")) {
    ierr = precipitation.write(filename.c_str()); CHKERRQ(ierr);
  }

  return 0;
}

//! Copies the stored precipitation field into result.
PetscErrorCode PAYearlyCycle::mean_precip(IceModelVec2S &result) {
  PetscErrorCode ierr;

  string precip_history = "read from " + precip_filename + "\n";

  ierr = precipitation.copy_to(result); CHKERRQ(ierr);
  ierr = result.set_attr("history", precip_history); CHKERRQ(ierr);

  return 0;
}

//! Copies the stored mean annual near-surface air temperature field into result.
PetscErrorCode PAYearlyCycle::mean_annual_temp(IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = temp_ma.copy_to(result); CHKERRQ(ierr);
  ierr = result.set_attr("history",
			 "computed using " + reference + "\n"); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::temp_time_series(int i, int j, int N,
                                               PetscReal *ts, PetscReal *values) {
  // constants related to the standard yearly cycle
  const PetscReal
    sperd = 8.64e4, // exact number of seconds per day
    julyday_fraction = (sperd / secpera) * snow_temp_july_day;

  for (PetscInt k = 0; k < N; ++k) {
    double tk = grid.time->year_fraction(ts[k]) - julyday_fraction;
    values[k] = temp_ma(i,j) + (temp_mj(i,j) - temp_ma(i,j)) * cos(2.0 * pi * tk);
  }

  return 0;
}

PetscErrorCode PAYearlyCycle::temp_snapshot(IceModelVec2S &result) {
  PetscErrorCode ierr;
  const PetscReal
    sperd = 8.64e4, // exact number of seconds per day
    julyday_fraction = (sperd / secpera) * snow_temp_july_day;

  double T = grid.time->year_fraction(t + 0.5 * dt) - julyday_fraction;

  ierr = temp_mj.add(-1.0, temp_ma, result); CHKERRQ(ierr); // tmp = temp_mj - temp_ma
  ierr = result.scale(cos(2 * pi * T)); CHKERRQ(ierr);
  ierr = result.add(1.0, temp_ma); CHKERRQ(ierr);
  // result = temp_ma + (temp_mj - temp_ma) * cos(radpersec * (T - julydaysec));

  string history = "computed using " + reference + "\n";
  ierr = result.set_attr("history", history); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::begin_pointwise_access() {
  PetscErrorCode ierr;

  ierr = temp_ma.begin_access(); CHKERRQ(ierr);
  ierr = temp_mj.begin_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PAYearlyCycle::end_pointwise_access() {
  PetscErrorCode ierr;

  ierr = temp_ma.end_access(); CHKERRQ(ierr);
  ierr = temp_mj.end_access(); CHKERRQ(ierr);

  return 0;
}

