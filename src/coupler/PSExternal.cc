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

#include "PSExternal.hh"
#include "PISMIO.hh"
#include <sys/file.h>
#include <time.h>

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
  string pism_input;
  LocalInterpCtx *lic;
  bool regrid;
  int start;

  ierr = verbPrintf(2, grid.com,
                    "* Initializing the PISM surface model running an external program\n"
                    "  to compute top-surface boundary conditions...\n"); CHKERRQ(ierr); 

  usurf = dynamic_cast<IceModelVec2S*>(vars.get("surface_altitude"));
  if (!usurf) { SETERRQ(1, "ERROR: Surface elevation is not available"); }

  topg = dynamic_cast<IceModelVec2S*>(vars.get("bedrock_altitude"));
  if (!topg) { SETERRQ(1, "ERROR: Bed elevation is not available"); }

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

  // artm_0 is the initial condition; artm_0 = artm(t_0) + gamma*usurf(t_0)
  ierr = artm_0.create(grid, "usurf", false); CHKERRQ(ierr);
  ierr = artm_0.set_attrs("internal", "ice upper surface elevation",
                           "m", "surface_altitude"); CHKERRQ(ierr);

  ierr = find_pism_input(pism_input, lic, regrid, start); CHKERRQ(ierr); 

  if (regrid) {
    ierr = artm_0.regrid(pism_input.c_str(), *lic, true); CHKERRQ(ierr);
    ierr =   artm.regrid(pism_input.c_str(), *lic, true); CHKERRQ(ierr);
  } else {
    ierr = artm_0.read(pism_input.c_str(), start); CHKERRQ(ierr);
    ierr =   artm.read(pism_input.c_str(), start); CHKERRQ(ierr);
  }

  delete lic;

  bool ebm_input_set, ebm_output_set, ebm_command_set;
  ierr = PetscOptionsBegin(grid.com, "", "PSExternal model options", ""); CHKERRQ(ierr);
  {
    bool flag;
    ierr = PISMOptionsReal("-lapse_rate", "Air temperature lapse rate, degrees K per kilometer",
			   gamma, flag); CHKERRQ(ierr);
    ierr = PISMOptionsReal("-update_interval", "Energy balance model update interval, years",
			   update_interval, flag); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ebm_input_file", "Name of the file an external boundary model will read data",
                             ebm_input, ebm_input_set); CHKERRQ(ierr);
    ierr = PISMOptionsString("-ebm_output_file",
                             "Name of the file into which an external boundary model will write B.C.",
                             ebm_output, ebm_output_set); CHKERRQ(ierr);
    ierr = PISMOptionsString("-ebm_command", "The command (with options) running an external boundary model",
                             ebm_command, ebm_command_set); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);
  

  gamma = gamma / 1000;         // convert to K/meter

  // Use gamma to compute the initial condition:
  ierr = artm_0.scale(gamma); CHKERRQ(ierr);
  ierr = artm_0.add(1.0, artm); CHKERRQ(ierr);

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

PetscErrorCode PSExternal::ice_surface_mass_flux(PetscReal t_years, PetscReal dt_years,
                                            IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = update(t_years, dt_years); CHKERRQ(ierr);

  ierr = acab.copy_to(result); CHKERRQ(ierr); 

  return 0;
}

PetscErrorCode PSExternal::ice_surface_temperature(PetscReal t_years, PetscReal dt_years,
                                              IceModelVec2S &result) {
  PetscErrorCode ierr;

  ierr = update(t_years, dt_years); CHKERRQ(ierr);

  ierr = artm.copy_to(result); CHKERRQ(ierr); 

  return 0;
}

PetscErrorCode PSExternal::max_timestep(PetscReal t_years, PetscReal &dt_years) {
  PetscErrorCode ierr;
  double delta = 0.5 * update_interval;
  double next_update = ceil(t_years / delta) * delta;

  if (PetscAbs(next_update - t_years) < 1e6)
    next_update = t_years + delta;
  
  dt_years = next_update - t_years;

  return 0;
}

//! \brief Update the surface mass balance field by reading from a file created
//! by an EBM. Also, write ice surface elevation and bed topography for an EBM to read.
PetscErrorCode PSExternal::update(PetscReal t_years, PetscReal dt_years) {
  PetscErrorCode ierr;
  double delta = 0.5 * update_interval;

  if ((fabs(t_years - t) < 1e-12) &&
      (fabs(dt_years - dt) < 1e-12))
    return 0;

  if (t_years + dt_years < t + delta) {
    // the first half of the update interval
    return 0;
  }

  // we're either in the second half of the current interval or past the end of
  // it, so we need to write coupling fields and run an external model
  ierr = run(); CHKERRQ(ierr);
  last_update = t;

  if (t_years + dt_years < t + update_interval) {
    // still in the current update interval; we're done
    return 0;
  }

  // we're past the end of the current interval; we need to wait for an external model to 
  // finish computing the boundary conditions and then read them

  ierr = wait(); CHKERRQ(ierr);

  t  = t_years;
  dt = dt_years;

  // The actual update:

  // update PISM's b.c.:
  ierr = update_artm(); CHKERRQ(ierr);
  ierr = update_acab(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PSExternal::update_acab() {
  PetscErrorCode ierr;
  PISMIO nc(&grid);

  ierr = verbPrintf(2, grid.com, "Reading the accumulation/ablation rate from %s...\n",
                    ebm_output.c_str()); 

  grid_info gi;
  ierr = nc.open_for_reading(ebm_output.c_str()); CHKERRQ(ierr);
  ierr = nc.get_grid_info_2d(gi); CHKERRQ(ierr);
  ierr = nc.close(); CHKERRQ(ierr);
  LocalInterpCtx lic(gi, NULL, NULL, grid); // 2D only

  ierr = acab.regrid(ebm_output.c_str(), lic, true); CHKERRQ(ierr);

  return 0;
}

//! Update artm using an atmospheric lapse rate.
PetscErrorCode PSExternal::update_artm() {
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

//! Write fields that a model PISM is coupled to needs. Currently: usurf and topg.
PetscErrorCode PSExternal::write_coupling_fields() {
  PetscErrorCode ierr;
  PISMIO nc(&grid);

  ierr = nc.open_for_writing(ebm_input.c_str(),
                             true, true); CHKERRQ(ierr);
  // "append" (i.e. do not move the file aside) and check dimensions.

  // Determine if the file is empty; if it is, append to the time dimension,
  // otherwise overwrite the time stored in the time variable.
  int t_len;
  ierr = nc.get_dim_length("t", &t_len); CHKERRQ(ierr);

  if (t_len == 0) {
    ierr = nc.append_time(grid.year); CHKERRQ(ierr);
  } else {
    int t_varid;
    bool t_exists;
    ierr = nc.find_variable("t", &t_varid, t_exists); CHKERRQ(ierr);

    ierr = nc.put_dimension(t_varid, 1, &grid.year); CHKERRQ(ierr);
  }
  ierr = nc.close(); CHKERRQ(ierr);

  // write the fields an EBM needs:
  ierr = usurf->write(ebm_input.c_str()); CHKERRQ(ierr);
  ierr = topg->write(ebm_input.c_str()); CHKERRQ(ierr);

  return 0;
}

//! \brief Run an external model.
PetscErrorCode PSExternal::run() {
  PetscErrorCode ierr;
  int tmp = 1;

  ierr = write_coupling_fields(); CHKERRQ(ierr);

  if (grid.rank == 0) {
    MPI_Send(&tmp, 1, MPI_INT, 0, TAG_EBM_RUN, inter_comm);
  }

  MPI_Barrier(grid.com);

  return 0;
}

//! \brief Wait for an external model to create a file to read data from.
PetscErrorCode PSExternal::wait() {
  PetscErrorCode ierr;
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
  }

  // Broadcast status:
  MPI_Bcast(&ebm_status, 1, MPI_INT, 0, grid.com);

  if (ebm_status == EBM_STATUS_FAILED) {
    PetscPrintf(grid.com, "PISM ERROR: EBM run failed. Exiting...\n");
    PISMEnd();
  }

  return 0;
}


