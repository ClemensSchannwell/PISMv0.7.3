// Copyright (C) 2010, 2011 Constantine Khroulev
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

#include <sys/file.h>
#include <time.h>

#include "PSExternal.hh"
#include "PISMIO.hh"
#include "PISMVars.hh"
#include "PISMTime.hh"
#include "IceGrid.hh"
#include "pism_options.hh"

PSExternal::~PSExternal() {
  int done = 1;

  // tell the EBM driver to stop:
  if (grid.rank == 0) {
    MPI_Send(&done, 1, MPI_INT, 0, TAG_EBM_STOP, inter_comm);
  }
}

//! Initialize the PSExternal model.
PetscErrorCode PSExternal::init(PISMVars &vars) {
  PetscErrorCode ierr;

  ierr = verbPrintf(2, grid.com,
                    "* Initializing the PISM surface model running an external program\n"
                    "  to compute top-surface boundary conditions...\n"); CHKERRQ(ierr); 

  ierr = acab.create(grid, "acab", false); CHKERRQ(ierr);
  ierr = acab.set_attrs(
            "climate_from_PISMSurfaceModel",  // FIXME: can we do better?
            "ice-equivalent surface mass balance (accumulation/ablation) rate",
	    "m s-1",  // m *ice-equivalent* per second
	    "land_ice_surface_specific_mass_balance");  // CF standard_name
	    CHKERRQ(ierr);
  ierr = acab.set_glaciological_units("m year-1"); CHKERRQ(ierr);
  acab.write_in_glaciological_units = true;
  acab.set_attr("comment", "positive values correspond to ice gain");

  // annual mean air temperature at "ice surface", at level below all firn
  //   processes (e.g. "10 m" or ice temperatures)
  ierr = artm.create(grid, "artm", false); CHKERRQ(ierr);
  ierr = artm.set_attrs(
            "climate_from_PISMSurfaceModel",  // FIXME: can we do better?
            "annual average ice surface temperature, below firn processes",
            "K", 
            "");  // PROPOSED CF standard_name = land_ice_surface_temperature_below_firn
  CHKERRQ(ierr);

  vector<string> ebm_var_names;
  bool ebm_input_set, ebm_output_set, ebm_command_set;
  ierr = PetscOptionsBegin(grid.com, "", "PSExternal model options", ""); CHKERRQ(ierr);
  {
    bool flag;
    ierr = PISMOptionsReal("-update_interval", "Energy balance model update interval, years",
			   update_interval, flag); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ebm_input_file", "Name of the file an external boundary model will read data",
                             ebm_input, ebm_input_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ebm_output_file",
                             "Name of the file into which an external boundary model will write B.C.",
                             ebm_output, ebm_output_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ebm_command",
                             "Command (with options) running an external boundary model",
                             ebm_command, ebm_command_set); CHKERRQ(ierr);

    ierr = PISMOptionsStringArray("-ebm_vars",
                                  "Comma-separated list of variables an EBM needs to compute B.C.s",
                                  "usurf,topg", ebm_var_names, flag); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  update_interval = convert(update_interval, "years", "seconds");

  for (unsigned int i = 0; i < ebm_var_names.size(); ++i) {
    IceModelVec *var = vars.get(ebm_var_names[i]);

    if (var) {
      ebm_vars.push_back(var);
    } else {
      ierr = verbPrintf(2, grid.com,
                        "WARNING: variable %s is not available\n", ebm_var_names[i].c_str());
    }
  }

  // The first time an EBM is run to pre-compute B.C. it is done after a half
  // of the interval, i.e. in the middle. Then this variable is reset to
  // update_interval.
  ebm_update_interval = 0.5 * update_interval;

  // Initialize the EBM driver:
  if (grid.rank == 0) {
    char command[PETSC_MAX_PATH_LEN];
    strncpy(command, ebm_command.c_str(), PETSC_MAX_PATH_LEN);
    MPI_Send(command, PETSC_MAX_PATH_LEN, MPI_CHAR, 0, TAG_EBM_COMMAND, inter_comm);
  }

  if (! (ebm_input_set && ebm_output_set && ebm_command_set)) {
    PetscPrintf(grid.com,
                "PISM ERROR: you need to specify all three of -ebm_input_file, -ebm_output_file and -ebm_command.\n");

    // tell the EBM side to stop:
    if (grid.rank == 0) {
      int done = 1;
      MPI_Send(&done, 1, MPI_INT, 0, TAG_EBM_STOP, inter_comm);
    }

    PISMEnd();
  }

  return 0;
}

PetscErrorCode PSExternal::ice_surface_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = acab.copy_to(result); CHKERRQ(ierr); 

  return 0;
}

PetscErrorCode PSExternal::ice_surface_temperature(IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = artm.copy_to(result); CHKERRQ(ierr); 

  return 0;
}

PetscErrorCode PSExternal::max_timestep(PetscReal my_t, PetscReal &my_dt, bool &restrict) {
  double delta = convert(0.5 * update_interval, "years", "seconds");
  double next_update = ceil(my_t / delta) * delta;

  if (PetscAbs(next_update - my_t) < 1e-6)
    next_update = my_t + delta;
  
  my_dt = next_update - my_t;
  restrict = true;

  return 0;
}

//! \brief Update the surface mass balance field by reading from a file created
//! by an EBM. Also, write ice surface elevation and bed topography for an EBM to read.
PetscErrorCode PSExternal::update(PetscReal my_t, PetscReal my_dt) {
  PetscErrorCode ierr;

  if ((fabs(my_t - t) < 1e-12) &&
      (fabs(my_dt - dt) < 1e-12))
    return 0;

  t = my_t;
  dt = my_dt;

  // This convoluted comparison is here to make it update the B.C. at the
  // beginning of a run, when last_bc_update is NAN, plus later on when
  // necessary (any comparison with a NAN evaluates to "false").
  if (! (my_t + my_dt <= last_bc_update + update_interval) ) {

    if (ebm_is_running) {
      // EBM is running (pre-computing B.C.)
      ierr = wait(); CHKERRQ(ierr);
    } else {
      // EBM is not running (probably at the beginning of a run)
      ierr = run(my_t); CHKERRQ(ierr);
      ierr = wait(); CHKERRQ(ierr);
    }

    ierr = update_acab(); CHKERRQ(ierr);
    ierr = update_artm(); CHKERRQ(ierr);
    last_bc_update = my_t;
  } else if (my_t + my_dt > last_ebm_update + ebm_update_interval) {
    if (ebm_is_running) {
      ierr = wait(); CHKERRQ(ierr);
    }

    // we're at the end of a run, so no pre-computing is necessary.
    if (PetscAbs(my_t + my_dt - grid.time->end()) < 1e-12)
      return 0;

    // time to run EBM to pre-compute B.C.
    ierr = run(my_t); CHKERRQ(ierr);

    // EBM always runs at the beginning of a PISM run, then after 1/2 of an
    // update interval. After than it runs once per update interval, in the
    // middle of it.
    ebm_update_interval = update_interval;
  }

  return 0;
}

PetscErrorCode PSExternal::update_artm() {
  PetscErrorCode ierr;

  ierr = verbPrintf(2, grid.com, "Reading the temperature at the top of the ice from %s for year = %1.1f...\n",
                    ebm_output.c_str(), grid.time->year(t)); 

  ierr = artm.regrid(ebm_output, true); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PSExternal::update_acab() {
  PetscErrorCode ierr;

  ierr = verbPrintf(2, grid.com, "Reading the accumulation/ablation rate from %s for year = %1.1f...\n",
                    ebm_output.c_str(), grid.time->year(t)); 

  ierr = acab.regrid(ebm_output, true); CHKERRQ(ierr);

  return 0;
}

//! Write fields that a model PISM is coupled to needs. Default: usurf and topg.
PetscErrorCode PSExternal::write_coupling_fields() {
  PetscErrorCode ierr;
  PISMIO nc(&grid);

  ierr = nc.open_for_writing(ebm_input, true, false); CHKERRQ(ierr);
  // "append" (i.e. do not move the file aside) and do not check dimensions.

  // Determine if the file is empty; if it is, create dimenstions and
  // dimensional variables, otherwise overwrite the time stored in the time
  // variable.
  unsigned int t_len;
  ierr = nc.get_dim_length(config.get_string("time_dimension_name"), &t_len); CHKERRQ(ierr);

  if (t_len == 0) {
    ierr = nc.create_dimensions(); CHKERRQ(ierr);
    ierr = nc.append_time(config.get_string("time_dimension_name"), grid.time->current()); CHKERRQ(ierr);
  } else {
    int t_varid;
    bool t_exists;
    ierr = nc.find_variable(config.get_string("time_dimension_name"),
                            &t_varid, t_exists); CHKERRQ(ierr);
    
    vector<double> time(1);
    time[0] = grid.time->current();
    ierr = nc.put_dimension(t_varid, time); CHKERRQ(ierr);
  }

  // define
  for (unsigned int i = 0; i < ebm_vars.size(); ++i) {
    IceModelVec *var = ebm_vars[i];
    ierr = var->define(nc, NC_DOUBLE); CHKERRQ(ierr);
  }

  ierr = nc.close(); CHKERRQ(ierr);

  // write
  for (unsigned int i = 0; i < ebm_vars.size(); ++i) {
    IceModelVec *var = ebm_vars[i];
    ierr = var->write(ebm_input); CHKERRQ(ierr);
  }

  return 0;
}

//! \brief Run an external model.
PetscErrorCode PSExternal::run(double my_t) {
  PetscErrorCode ierr;
  double year = (double)my_t;

  ierr = write_coupling_fields(); CHKERRQ(ierr);

  if (grid.rank == 0) {
    MPI_Send(&year, 1, MPI_DOUBLE, 0, TAG_EBM_RUN, inter_comm);
  }

  last_ebm_update = my_t;
  ebm_is_running = true;

  return 0;
}

//! \brief Wait for an external model to create a file to read data from.
PetscErrorCode PSExternal::wait() {
  int ebm_status;

  if (grid.rank == 0) {
    double sleep_interval = 0.01,       // seconds
      threshold = 60,                   // wait at most 1 minute
      message_interval = 5;             // print a message every 5 seconds
    struct timespec rq;
    rq.tv_sec = 0;
    rq.tv_nsec = (long)(sleep_interval*1e9); // convert to nanoseconds

    int wait_counter = 0,
      wait_message_counter = 1;

    MPI_Status status;
    while (wait_counter * sleep_interval < threshold) {
      int flag;
      MPI_Iprobe(0, TAG_EBM_STATUS, inter_comm, &flag, &status);

      if (flag)                 // we got a status message
        break;

      if (sleep_interval * wait_counter / message_interval  > wait_message_counter) {
        fprintf(stderr, "PISM: Waiting for a message from the EBM driver...\n");
        wait_message_counter++;
      }
      nanosleep(&rq, 0);
      wait_counter++;
    }

    if (sleep_interval * wait_counter >= threshold) {
      // exited the loop above because of a timeout
      fprintf(stderr, "ERROR: spent %1.1f minutes waiting for the EBM driver... Giving up...\n",
              threshold / 60.0);
      PISMEnd();
    }

    // fprintf(stderr, "PISM: Got a status message from EBM\n");

    // receive the EBM status
    MPI_Recv(&ebm_status, 1, MPI_INT,
             0, TAG_EBM_STATUS, inter_comm, NULL);

    MPI_Barrier(grid.com);
  } else {
    MPI_Barrier(grid.com);
  }

  // Broadcast status:
  MPI_Bcast(&ebm_status, 1, MPI_INT, 0, grid.com);

  if (ebm_status == EBM_STATUS_FAILED) {
    PetscPrintf(grid.com, "PISM ERROR: EBM run failed. Exiting...\n");
    PISMEnd();
  }

  ebm_is_running = false;

  return 0;
}


void PSExternal::add_vars_to_output(string keyword, set<string> &result) {
  if (keyword == "big") {
    result.insert("acab");
    result.insert("artm");
  }
}

PetscErrorCode PSExternal::define_variables(set<string> vars, const NetCDF3Wrapper &nc,
                                            nc_type nctype) {
  PetscErrorCode ierr;

  ierr = PISMSurfaceModel::define_variables(vars, nc, nctype); CHKERRQ(ierr);

  if (set_contains(vars, "artm")) {
    ierr = artm.define(nc, nctype); CHKERRQ(ierr); 
  }

  if (set_contains(vars, "acab")) {
    ierr = acab.define(nc, nctype); CHKERRQ(ierr); 
  }

  return 0;
}

PetscErrorCode PSExternal::write_variables(set<string> vars, string filename) {
  PetscErrorCode ierr;

  if (set_contains(vars, "artm")) {
    ierr = artm.write(filename); CHKERRQ(ierr);
  }

  if (set_contains(vars, "acab")) {
    ierr = acab.write(filename); CHKERRQ(ierr);
  }

  return 0;
}

/// The ALR variation

PetscErrorCode PSExternal_ALR::init(PISMVars &vars) {
  PetscErrorCode ierr;
  string pism_input;
  bool regrid;
  int start;

  ierr = PSExternal::init(vars); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "  [ using an atmospheric lapse rate correction for the temperature at the top of the ice ]\n");
  CHKERRQ(ierr);

  usurf = dynamic_cast<IceModelVec2S*>(vars.get("surface_altitude"));
  if (usurf == NULL) SETERRQ(grid.com, 1, "surface_altitude is not available");

  // artm_0 is the initial condition; artm_0 = artm(t_0) + gamma*usurf(t_0)
  ierr = artm_0.create(grid, "usurf", false); CHKERRQ(ierr);
  ierr = artm_0.set_attrs("internal", "ice upper surface elevation",
                           "m", "surface_altitude"); CHKERRQ(ierr);

  ierr = find_pism_input(pism_input, regrid, start); CHKERRQ(ierr); 

  if (regrid) {
    ierr = artm_0.regrid(pism_input, true); CHKERRQ(ierr);
    ierr =   artm.regrid(pism_input, true); CHKERRQ(ierr);
  } else {
    ierr = artm_0.read(pism_input, start); CHKERRQ(ierr);
    ierr =   artm.read(pism_input, start); CHKERRQ(ierr);
  }

  ierr = PetscOptionsBegin(grid.com, "", "PSExternal_ALR options", ""); CHKERRQ(ierr);
  {
    bool flag;
    ierr = PISMOptionsReal("-artm_lapse_rate", "Top of the ice temperature lapse rate, degrees K per kilometer",
			   gamma, flag); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  gamma = gamma / 1000;         // convert to K/meter

  // Use gamma to compute the initial condition:
  ierr = artm_0.scale(gamma); CHKERRQ(ierr);
  ierr = artm_0.add(1.0, artm); CHKERRQ(ierr);

  return 0;
}

//! Always add artm (it is needed for re-starting the lapse rate correction).
void PSExternal_ALR::add_vars_to_output(string keyword, set<string> &result) {
  result.insert("artm");

  if (keyword == "big") {
    result.insert("acab");
  }
}

//! Update artm using an atmospheric lapse rate.
PetscErrorCode PSExternal_ALR::update_artm() {
  PetscErrorCode ierr;

  ierr = usurf->begin_access(); CHKERRQ(ierr);
  ierr = artm.begin_access(); CHKERRQ(ierr);
  ierr = artm_0.begin_access(); CHKERRQ(ierr); 
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      artm(i,j) = artm_0(i,j) - gamma * (*usurf)(i,j);
    }
  }
  ierr = usurf->end_access(); CHKERRQ(ierr);
  ierr = artm.end_access(); CHKERRQ(ierr);
  ierr = artm_0.end_access(); CHKERRQ(ierr); 

  return 0;
}
