// Copyright (C) 2009-2012 Constantine Khroulev
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

#include <sstream>
#include <algorithm>

#include "iceModel.hh"
#include "PIO.hh"
#include "PISMStressBalance.hh"
#include "PISMDiagnostic.hh"
#include "PISMTime.hh"
#include "pism_options.hh"

//! Initializes the code writing scalar time-series.
PetscErrorCode IceModel::init_timeseries() {
  PetscErrorCode ierr;
  bool ts_file_set, ts_times_set, ts_vars_set;
  string times, vars;
  bool append;

  ierr = PetscOptionsBegin(grid.com, "", "Options controlling scalar diagnostic time-series", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsString("-ts_file", "Specifies the time-series output file name",
			     ts_filename, ts_file_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ts_times", "Specifies a MATLAB-style range or a list of requested times",
			     times, ts_times_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ts_vars", "Specifies a comma-separated list of veriables to save",
			     vars, ts_vars_set); CHKERRQ(ierr);

    // default behavior is to move the file aside if it exists already; option allows appending
    ierr = PISMOptionsIsSet("-ts_append", append); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);


  if (ts_file_set ^ ts_times_set) {
    ierr = PetscPrintf(grid.com,
      "PISM ERROR: you need to specity both -ts_file and -ts_times to save"
      "diagnostic time-series.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }

  // If neither -ts_filename nor -ts_times is set, we're done.
  if (!ts_file_set && !ts_times_set) {
    save_ts = false;
    return 0;
  }

  save_ts = true;

  ierr = parse_times(grid.com, config, times, ts_times);
  if (ierr != 0) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: parsing the -ts_times argument failed.\n"); CHKERRQ(ierr);
    PISMEnd();
  }

  if (ts_times.size() == 0) {
    PetscPrintf(grid.com, "PISM ERROR: no argument for -ts_times option.\n");
    PISMEnd();
  }

  ierr = verbPrintf(2, grid.com, "saving scalar time-series to '%s'; ",
		    ts_filename.c_str()); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com, "times requested: %s\n", times.c_str()); CHKERRQ(ierr);

  current_ts = 0;


  string var_name;
  if (ts_vars_set) {
    ierr = verbPrintf(2, grid.com, "variables requested: %s\n", vars.c_str()); CHKERRQ(ierr);
    istringstream arg(vars);

    while (getline(arg, var_name, ','))
      ts_vars.insert(var_name);

  } else {
    var_name = config.get_string("ts_default_variables");
    istringstream arg(var_name);

    while (getline(arg, var_name, ' ')) {
      if (!var_name.empty()) // this ignores multiple spaces separating variable names
	ts_vars.insert(var_name);
    }
  }


  PIO nc(grid.com, grid.rank, "netcdf3");
  ierr = nc.open(ts_filename, NC_WRITE, append); CHKERRQ(ierr);
  ierr = nc.close(); CHKERRQ(ierr);

  ierr = write_metadata(ts_filename, false); CHKERRQ(ierr);

  // set the output file:
  map<string,PISMTSDiagnostic*>::iterator j = ts_diagnostics.begin();
  while (j != ts_diagnostics.end()) {
    ierr = (j->second)->init(ts_filename); CHKERRQ(ierr);
    ++j;
  }

  // ignore times before (and including) the beginning of the run:
  while (current_ts < ts_times.size() && ts_times[current_ts] < grid.time->start())
    current_ts++;

  if (ts_times.size() == current_ts) {
    save_ts = false;
    return 0;
  }

  vector<double> tmp(ts_times.size() - current_ts);
  for (unsigned int k = 0; k < tmp.size(); ++k)
    tmp[k] = ts_times[current_ts + k];

  ts_times = tmp;
  current_ts = 0;

  return 0;
}

//! Write time-series.
PetscErrorCode IceModel::write_timeseries() {
  PetscErrorCode ierr;

  // return if no time-series requested
  if (!save_ts) return 0;

  // return if wrote all the records already
  if (current_ts == ts_times.size())
    return 0;

  // return if did not yet reach the time we need to save at
  if (ts_times[current_ts] > grid.time->current())
    return 0;
  
  for (set<string>::iterator j = ts_vars.begin(); j != ts_vars.end(); ++j) {
    PISMTSDiagnostic *diag = ts_diagnostics[*j];

    if (diag != NULL) {
      ierr = diag->update(grid.time->current() - dt, grid.time->current()); CHKERRQ(ierr);
    }
  }


  // Interpolate to put them on requested times:
  for (; current_ts < ts_times.size() && ts_times[current_ts] <= grid.time->current(); current_ts++) {

    // the very first time (current_ts == 0) defines the left endpoint of the
    // first time interval; we don't write a report at that time
    if (current_ts == 0)
      continue;

    for (set<string>::iterator j = ts_vars.begin(); j != ts_vars.end(); ++j) {
      PISMTSDiagnostic *diag = ts_diagnostics[*j];

      if (diag != NULL) {
        ierr = diag->save(ts_times[current_ts - 1], ts_times[current_ts]); CHKERRQ(ierr);
      }
    }
  }

  return 0;
}


//! Initialize the code saving spatially-variable diagnostic quantities.
PetscErrorCode IceModel::init_extras() {
  PetscErrorCode ierr;
  bool split, times_set, file_set, save_vars;
  string times, vars;
  current_extra = 0;

  ierr = PetscOptionsBegin(grid.com, "", "Options controlling 2D and 3D diagnostic output", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsString("-extra_file", "Specifies the output file",
			     extra_filename, file_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-extra_times", "Specifies times to save at",
			     times, times_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-extra_vars", "Spacifies a comma-separated list of variables to save",
			     vars, save_vars); CHKERRQ(ierr);

    ierr = PISMOptionsIsSet("-extra_split", "Specifies whether to save to separate files",
			    split); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  if (file_set ^ times_set) {
    PetscPrintf(grid.com,
      "PISM ERROR: you need to specify both -extra_file and -extra_times to save spatial time-series.\n");
    PISMEnd();
  }

  if (!file_set && !times_set) {
    save_extra = false;
    return 0;
  }

  ierr = parse_times(grid.com, config, times, extra_times);
  if (ierr != 0) {
    PetscPrintf(grid.com, "PISM ERROR: parsing the -extra_times argument failed.\n");
    PISMEnd();
  }
  if (extra_times.size() == 0) {
    PetscPrintf(grid.com, "PISM ERROR: no argument for -extra_times option.\n");
    PISMEnd();
  }

  save_extra = true;
  extra_file_is_ready = false;
  split_extra = false;

  if (split) {
    split_extra = true;
  } else if (!ends_with(extra_filename, ".nc")) {
    ierr = verbPrintf(2, grid.com,
		      "PISM WARNING: spatial time-series file name '%s' does not have the '.nc' suffix!\n",
		      extra_filename.c_str());
    CHKERRQ(ierr);
  }

  if (split) {
    ierr = verbPrintf(2, grid.com, "saving spatial time-series to '%s+year.nc'; ",
		      extra_filename.c_str()); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2, grid.com, "saving spatial time-series to '%s'; ",
		      extra_filename.c_str()); CHKERRQ(ierr);
  }

  if (extra_times.size() > 500) {
    ierr = verbPrintf(2, grid.com,
		      "PISM WARNING: more than 500 times requested. This might fill your hard-drive!\n");
    CHKERRQ(ierr);
  }

  ierr = verbPrintf(2, grid.com, "times requested: %s\n", times.c_str()); CHKERRQ(ierr);

  string var_name;
  if (save_vars) {
    ierr = verbPrintf(2, grid.com, "variables requested: %s\n", vars.c_str()); CHKERRQ(ierr);
    istringstream arg(vars);

    while (getline(arg, var_name, ','))
      extra_vars.insert(var_name);

  } else {
    ierr = verbPrintf(2, grid.com, "PISM WARNING: -extra_vars was not set."
                      " Writing model_state, mapping and climate_steady variables...\n"); CHKERRQ(ierr);

    set<string> vars_set = variables.keys();

    set<string>::iterator i = vars_set.begin();
    while (i != vars_set.end()) {
      IceModelVec *var = variables.get(*i);

      string intent = var->string_attr("pism_intent");
      if ((intent == "model_state") ||
          (intent == "mapping") ||
          (intent == "climate_steady")) {
	extra_vars.insert(*i);
      }
      i++;
    }

    if (stress_balance)
      stress_balance->add_vars_to_output("small", extra_vars);

  }
  if (extra_vars.size() == 0) {
    ierr = verbPrintf(2, grid.com, 
       "PISM WARNING: no variables list after -extra_vars ... writing empty file ...\n"); CHKERRQ(ierr);
  }

  return 0;
}

//! Write spatially-variable diagnostic quantities.
PetscErrorCode IceModel::write_extras() {
  PetscErrorCode ierr;
  PIO nc(grid.com, grid.rank, "netcdf3");
  double saving_after = -1.0e30; // initialize to avoid compiler warning; this
				 // value is never used, because saving_after
				 // is only used if save_now == true, and in
				 // this case saving_after is guaranteed to be
				 // initialized. See the code below.
  char filename[PETSC_MAX_PATH_LEN];

  // determine if the user set the -save_at and -save_to options
  if (!save_extra)
    return 0;

  // do we need to save *now*?
  if ( current_extra < extra_times.size() &&
       (grid.time->current() >= extra_times[current_extra] ||
        fabs(grid.time->current() - extra_times[current_extra]) < 1) ) {
    // the condition above is "true" if we passed a requested time or got to
    // within 1 second from it

    saving_after = extra_times[current_extra];

    while (current_extra < extra_times.size() &&
           (extra_times[current_extra] <= grid.time->current() ||
            fabs(grid.time->current() - extra_times[current_extra]) < 1) )
      current_extra++;
  } else {
    // we don't need to save now, so just return
    return 0;
  }

  if (saving_after < grid.time->start()) {
    // Suppose a user tells PISM to write data at times 0:1000:10000. Suppose
    // also that PISM writes a backup file at year 2500 and gets stopped.
    //
    // When restarted, PISM will decide that it's time to write data for time
    // 2000, but
    // * that record was written already and
    // * PISM will end up writing at year 2500, producing a file containing one
    //   more record than necessary.
    //
    // This check makes sure that this never happens.
    return 0;
  }

  if (split_extra) {
    extra_file_is_ready = false;	// each time-series record is written to a separate file
    snprintf(filename, PETSC_MAX_PATH_LEN, "%s-%06.0f.nc",
	     extra_filename.c_str(), grid.time->year());
  } else {
    strncpy(filename, extra_filename.c_str(), PETSC_MAX_PATH_LEN);
  }

  ierr = verbPrintf(3, grid.com, 
		    "\nsaving spatial time-series to %s at %.5f a\n\n",
		    filename, grid.time->year());
  CHKERRQ(ierr);

  // create line for history in .nc file, including time of write
  string date_str = pism_timestamp();
  char tmp[TEMPORARY_STRING_LENGTH];
  snprintf(tmp, TEMPORARY_STRING_LENGTH,
	   "%s: %s saving spatial time-series record at %10.5f a\n",
	   date_str.c_str(), executable_short_name.c_str(), grid.time->year());

  if (!extra_file_is_ready) {

    // default behavior is to move the file aside if it exists already; option allows appending
    bool append;
    ierr = PISMOptionsIsSet("-extra_append", append); CHKERRQ(ierr);

    // Prepare the file:
    ierr = nc.open(filename, NC_WRITE, append); CHKERRQ(ierr);
    ierr = nc.def_time(config.get_string("time_dimension_name"),
                       config.get_string("calendar"),
                       grid.time->units()); CHKERRQ(ierr);
    ierr = nc.close(); CHKERRQ(ierr);

    ierr = write_metadata(filename); CHKERRQ(ierr); 

    extra_file_is_ready = true;
  }

  ierr = nc.open(filename, NC_WRITE, true); CHKERRQ(ierr);
  ierr = nc.append_time(config.get_string("time_dimension_name"),
                        grid.time->current()); CHKERRQ(ierr);
  ierr = nc.append_history(tmp); CHKERRQ(ierr);
  ierr = nc.close(); CHKERRQ(ierr);

  ierr = write_variables(filename, extra_vars, NC_FLOAT);  CHKERRQ(ierr);

  return 0;
}

//! Computes the maximum time-step we can take and still hit all the requested years.
/*!
  Sets restrict to 'false' if any time-step is OK.
 */
PetscErrorCode IceModel::extras_max_timestep(double my_t, double& my_dt, bool &restrict) {

  if (!save_extra) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  if (config.get_flag("extras_force_output_times") == false) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  vector<double>::iterator j;
  j = upper_bound(extra_times.begin(), extra_times.end(), my_t);

  if (j == extra_times.end()) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  my_dt = *j - my_t;
  restrict = true;

  // now make sure that we don't end up taking a time-step of less than 1
  // second long
  if (my_dt < 1) {
    if ((j + 1) != extra_times.end()) {
      my_dt = *(j + 1) - my_t;
      restrict = true;
    } else {
      my_dt = -1;
      restrict = false;
    }
  }

  return 0;
}

//! Computes the maximum time-step we can take and still hit all the requested years.
/*!
  Sets restrict to 'false' if any time-step is OK.
 */
PetscErrorCode IceModel::ts_max_timestep(double my_t, double& my_dt, bool &restrict) {

  if (!save_ts) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  // make sure that we hit the left endpoint of the first report interval
  if (my_t < ts_times[0]) {
    my_dt = ts_times[0] - my_t;
    restrict = true;
    return 0;
  }

  bool force_times;
  force_times = config.get_flag("ts_force_output_times");

  if (!force_times) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  vector<double>::iterator j;
  j = upper_bound(ts_times.begin(), ts_times.end(), my_t);

  if (j == ts_times.end()) {
    my_dt = -1;
    restrict = false;
    return 0;
  }

  my_dt = *j - my_t;
  restrict = true;

  return 0;
}

//! Flush scalar time-series.
PetscErrorCode IceModel::flush_timeseries() {
  PetscErrorCode ierr;
  // flush all the time-series buffers:
  for (set<string>::iterator j = ts_vars.begin(); j != ts_vars.end(); ++j) {
    PISMTSDiagnostic *diag = ts_diagnostics[*j];

    if (diag != NULL) {
      ierr = diag->flush(); CHKERRQ(ierr);
    }
  }

  return 0;
}
