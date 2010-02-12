// Copyright (C) 2004-2010 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include "../base/grid.hh"
#include "../base/materials.hh"
#include "../base/iceModel.hh"
#include "iceEISModel.hh"


IceEISModel::IceEISModel(IceGrid &g, NCConfigVariable &conf, NCConfigVariable &conf_overrides)
  : IceModel(g, conf, conf_overrides) {
  expername = 'A';
  iceFactory.setType(ICE_PB);  // Paterson-Budd
}


//! Only executed if NOT initialized from file (-i).
PetscErrorCode IceEISModel::set_grid_defaults() {
  grid.Lx = 750e3;
  grid.Ly = 750e3;
  grid.Lz = 4e3;  // depend on auto-expansion to handle bigger thickness
  return 0;
}


//! Option -eisII determines the single character name of EISMINT II experiments.
/*! Example is "-eisII F".   Defaults to experiment A.  */
PetscErrorCode IceEISModel::set_expername_from_options() {
  PetscErrorCode      ierr;

  char                eisIIexpername[20];
  int                 temp;
  PetscTruth          EISIIchosen;
  ierr = PetscOptionsGetString(PETSC_NULL, "-eisII", eisIIexpername, 1, &EISIIchosen);
            CHKERRQ(ierr);
  if (EISIIchosen == PETSC_TRUE) {
    temp = eisIIexpername[0];
    if ((temp >= 'a') && (temp <= 'z'))   temp += 'A'-'a';  // capitalize if lower
    if ((temp >= 'A') && (temp <= 'L')) {
      expername = temp;
    } else {
      ierr = PetscPrintf(grid.com,
        "option -eisII must have value A, B, C, D, E, F, G, H, I, J, K, or L\n");
        CHKERRQ(ierr);
      PetscEnd();
    }
  }

  return 0;
}


PetscErrorCode IceEISModel::setFromOptions() {
  PetscErrorCode      ierr;

  ierr = set_expername_from_options(); CHKERRQ(ierr);
  
  // optionally allow override of updateHmelt == PETSC_FALSE for EISMINT II
  ierr = check_option("-track_Hmelt", updateHmelt); CHKERRQ(ierr);

  ierr = verbPrintf(2,grid.com, 
    "setting parameters for surface mass balance and temperature in EISMINT II experiment %c ... \n", 
    expername); CHKERRQ(ierr);
  // EISMINT II specified values for parameters
  S_b = 1.0e-2 * 1e-3 / secpera;    // Grad of accum rate change
  S_T = 1.67e-2 * 1e-3;           // K/m  Temp gradient
  // these are for A,E,G,H,I,K:
  M_max = 0.5 / secpera;  // Max accumulation
  R_el = 450.0e3;           // Distance to equil line (accum=0)
  T_min = 238.15;
  switch (expername) {
    case 'B':  // supposed to start from end of experiment A and:
      T_min = 243.15;
      break;
    case 'C':
    case 'J':
    case 'L':  // supposed to start from end of experiment A (for C;
               //   resp I and K for J and L) and:
      M_max = 0.25 / secpera;
      R_el = 425.0e3;
      break;
    case 'D':  // supposed to start from end of experiment A and:
      R_el = 425.0e3;
      break;
    case 'F':  // start with zero ice and:
      T_min = 223.15;
      break;
  }

  // if user specifies Tmin, Tmax, Mmax, Sb, ST, Rel, then use that (override above)
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-Tmin", &T_min, NULL); CHKERRQ(ierr);
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-Tmax", &T_max, NULL); CHKERRQ(ierr);
  PetscScalar myMmax, mySb, myST, myRel;
  PetscTruth  paramSet;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-Mmax", &myMmax, &paramSet); CHKERRQ(ierr);
  if (paramSet == PETSC_TRUE)     M_max = myMmax / secpera;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-Sb", &mySb, &paramSet); CHKERRQ(ierr);
  if (paramSet == PETSC_TRUE)     S_b = mySb * 1e-3 / secpera;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-ST", &myST, &paramSet); CHKERRQ(ierr);
  if (paramSet == PETSC_TRUE)     S_T = myST * 1e-3;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-Rel", &myRel, &paramSet); CHKERRQ(ierr);
  if (paramSet == PETSC_TRUE)     R_el = myRel * 1e3;

  ierr = IceModel::setFromOptions();  CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceEISModel::init_physics() {
  PetscErrorCode ierr;

  ierr = IceModel::init_physics(); CHKERRQ(ierr);

  // see EISMINT II description; choose no ocean interaction, purely SIA, and E=1
  config.set_flag("is_dry_simulation", true);
  config.set_flag("use_ssa_velocity", false);
  config.set("enhancement_factor", 1.0);

  // basal melt does not change computation of vertical velocity:
  config.set_flag("include_bmr_in_continuity", false);

  // Make bedrock thermal material properties into ice properties.  Note that
  // zero thickness bedrock layer is the default, but we want the ice/rock
  // interface segment to have geothermal flux applied directly to ice without
  // jump in material properties at base.
  config.set("bedrock_thermal_density", ice->rho);
  config.set("bedrock_thermal_conductivity", ice->k);
  config.set("bedrock_thermal_specific_heat_capacity", ice->c_p);

  return 0;
}


PetscErrorCode IceEISModel::init_couplers() {
  PetscErrorCode      ierr;

  ierr = IceModel::init_couplers(); CHKERRQ(ierr);

  ierr = verbPrintf(2,grid.com,
    "  setting surface mass balance and surface temperature variables ...\n");
    CHKERRQ(ierr);

  PetscTruth i_set;
  char filename[PETSC_MAX_PATH_LEN];
  ierr = PetscOptionsGetString(PETSC_NULL, "-i",
			       filename, PETSC_MAX_PATH_LEN, &i_set); CHKERRQ(ierr);
  if (i_set) {
    ierr = verbPrintf(2,grid.com,
      "  (values from file %s ignored)\n", filename); CHKERRQ(ierr);
  }

  // now fill in accum and surface temp
  ierr = artm.begin_access(); CHKERRQ(ierr);
  ierr = acab.begin_access(); CHKERRQ(ierr);

  PetscScalar cx = grid.Lx, cy = grid.Ly;
  if (expername == 'E') {  cx += 100.0e3;  cy += 100.0e3;  } // shift center
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // r is distance from center of grid; if E then center is shifted (above)
      const PetscScalar r = sqrt( PetscSqr(-cx + grid.dx*i)
                                  + PetscSqr(-cy + grid.dy*j) );
      // set accumulation from formula (7) in (Payne et al 2000)
      acab(i,j) = PetscMin(M_max, S_b * (R_el-r));
      // set surface temperature
      artm(i,j) = T_min + S_T * r;  // formula (8) in (Payne et al 2000)
    }
  }

  ierr = artm.end_access(); CHKERRQ(ierr);
  ierr = acab.end_access(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceEISModel::generateTroughTopography() {
  PetscErrorCode  ierr;
  // computation based on code by Tony Payne, 6 March 1997:
  //    http://homepages.vub.ac.be/~phuybrec/eismint/topog2.f
  
  const PetscScalar    b0 = 1000.0;  // plateau elevation
  const PetscScalar    L = 750.0e3;  // half-width of computational domain
  const PetscScalar    w = 200.0e3;  // trough width
  const PetscScalar    slope = b0/L;
  const PetscScalar    dx61 = (2*L) / 60; // = 25.0e3
  ierr = vbed.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscScalar nsd = i * grid.dx, ewd = j * grid.dy;
      if (    (nsd >= (27 - 1) * dx61) && (nsd <= (35 - 1) * dx61)
           && (ewd >= (31 - 1) * dx61) && (ewd <= (61 - 1) * dx61) ) {
        vbed(i,j) = 1000.0 - PetscMax(0.0, slope * (ewd - L) * cos(pi * (nsd - L) / w));
      } else {
        vbed(i,j) = 1000.0;
      }
    }
  }
  ierr = vbed.end_access(); CHKERRQ(ierr);

  ierr = verbPrintf(2,grid.com,
               "trough bed topography stored by IceEISModel::generateTroughTopography()\n");
               CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceEISModel::generateMoundTopography() {
  PetscErrorCode  ierr;
  // computation based on code by Tony Payne, 6 March 1997:
  //    http://homepages.vub.ac.be/~phuybrec/eismint/topog2.f
  
  const PetscScalar    slope = 250.0;
  const PetscScalar    w = 150.0e3;  // mound width
  ierr = vbed.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscScalar nsd = i * grid.dx, ewd = j * grid.dy;
      vbed(i,j) = PetscAbs(slope * sin(pi * ewd / w) + slope * cos(pi * nsd / w));
    }
  }
  ierr = vbed.end_access(); CHKERRQ(ierr);

  ierr = verbPrintf(2,grid.com,
    "mound bed topography stored by IceEISModel::generateTroughTopography()\n");
    CHKERRQ(ierr);
  return 0;
}


//! Only executed if NOT initialized from file (-i).
PetscErrorCode IceEISModel::set_vars_from_options() {
  PetscErrorCode ierr;

  // initialize from EISMINT II formulas
  ierr = verbPrintf(2,grid.com, 
    "initializing variables from EISMINT II experiment %c formulas ... \n", 
    expername); CHKERRQ(ierr);

  ierr = vbed.set(0.0);
  if ((expername == 'I') || (expername == 'J')) {
    ierr = generateTroughTopography(); CHKERRQ(ierr);
  } 
  if ((expername == 'K') || (expername == 'L')) {
    ierr = generateMoundTopography(); CHKERRQ(ierr);
  } 
  // communicate b in any case; it will be horizontally-differentiated
  ierr = vbed.beginGhostComm(); CHKERRQ(ierr);
  ierr = vbed.endGhostComm(); CHKERRQ(ierr);

  ierr = vHmelt.set(0.0); CHKERRQ(ierr);
  ierr = vbasalMeltRate.set(0.0); CHKERRQ(ierr);
  ierr = vGhf.set(0.042); CHKERRQ(ierr);  // EISMINT II value; J m-2 s-1

  ierr = vMask.set(MASK_SHEET); CHKERRQ(ierr);
  ierr = vuplift.set(0.0); CHKERRQ(ierr);  // no expers have uplift at start

  ierr = vtillphi.set(config.get("default_till_phi")); CHKERRQ(ierr);

  // if no -i file then starts with zero ice
  ierr = vh.set(0.0); CHKERRQ(ierr);
  ierr = vH.set(0.0); CHKERRQ(ierr);

  // this IceModel bootstrap method should do right thing because of variable
  //   settings above and init of coupler above
  ierr = putTempAtDepth(); CHKERRQ(ierr);

  return 0;
}


//! Reimplement IceModel::basalVelocitySIA().
/*!
Applies in SIA regions (mask = SHEET).  Generally not a recommended mechanism,
but called-for in EISMINT II.
 */
PetscScalar IceEISModel::basalVelocitySIA(
    PetscScalar /*x*/, PetscScalar /*y*/, PetscScalar H, PetscScalar T,
    PetscScalar /*alpha*/, PetscScalar /*mu*/, PetscScalar /*min_T*/) const {

  const PetscScalar  Bfactor = 1e-3 / secpera; // m s^-1 Pa^-1
  const PetscScalar  eismintII_temp_sliding = 273.15;  // slide if basal ice this temp
  
  if (expername == 'G') {
      return Bfactor * ice->rho * standard_gravity * H; 
  } else if (expername == 'H') {
      if (T + ice->beta_CC_grad * H > eismintII_temp_sliding) {
        return Bfactor * ice->rho * standard_gravity * H; // ditto case G
      } else {
        return 0.0;
      }
  }  
  return 0.0;  // zero sliding for other tests
}

