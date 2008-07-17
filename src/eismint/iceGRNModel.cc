// Copyright (C) 2007-2008 Nathan Shemonski and Ed Bueler
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

#include <petscda.h>
#include <cstring>
#include <netcdf.h>
#include "../base/nc_util.hh"
#include "../base/grid.hh"
#include "../base/materials.hh"
#include "../base/iceModel.hh"
#include "../base/forcing.hh"
#include "iceGRNModel.hh"

const PetscScalar EISMINT_G_geothermal   = 0.050;      // J/m^2 s; geo. heat flux


IceGRNModel::IceGRNModel(IceGrid &g, IceType *i) : IceModel(g, i) {
  // only call parent's constructor; do all classes need constructors?
}


PetscErrorCode IceGRNModel::setFromOptions() {
  PetscErrorCode ierr;
  PetscTruth ssl2Set, ssl3Set, ccl3Set, gwl3Set;

  expernum = 1;  // SSL2 is the default
  ierr = PetscOptionsHasName(PETSC_NULL, "-ssl2", &ssl2Set); CHKERRQ(ierr);
  if (ssl2Set == PETSC_TRUE)   expernum = 1;
  ierr = PetscOptionsHasName(PETSC_NULL, "-ccl3", &ccl3Set); CHKERRQ(ierr);
  if (ccl3Set == PETSC_TRUE)   expernum = 3;
  ierr = PetscOptionsHasName(PETSC_NULL, "-gwl3", &gwl3Set); CHKERRQ(ierr);
  if (gwl3Set == PETSC_TRUE)   expernum = 4;

  ierr = PetscOptionsHasName(PETSC_NULL, "-ssl3", &ssl3Set); CHKERRQ(ierr);
  if (ssl3Set == PETSC_TRUE) {
    SETERRQ(1,"EISMINT-Greenland experiment SSL3 (-ssl3) is not implemented.\n"
            "Choose parameters yourself, by runtime options.\n");
  }

  enhancementFactor = 3;
  
  doOceanKill = PETSC_TRUE;
  
  if (expernum == 1) { // no bed deformation for steady state (SSL2)
    doBedDef = PETSC_FALSE;
  } else { // use Lingle-Clark bed deformation model for CCL3 and GWL3
    doBedDef = PETSC_TRUE;
    doBedIso = PETSC_FALSE;
  }
  
  muSliding = 0.0;  // no sliding in any case; perhaps develop an SSA variant??

  doTempSkip = PETSC_TRUE;
  tempskipMax = 20;

  // these flags turn off parts of the EISMINT-Greenland specification;
  //   use when extra/different data is available
  ierr = PetscOptionsHasName(PETSC_NULL, "-have_artm", &haveSurfaceTemps);
     CHKERRQ(ierr);
  ierr = PetscOptionsHasName(PETSC_NULL, "-no_EI_delete", &noEllesmereIcelandDelete);
     CHKERRQ(ierr);
  
  // note: user value for -e, and -gk, and so on, will override settings above
  ierr = IceModel::setFromOptions(); CHKERRQ(ierr);  
  return 0;
}


PetscErrorCode IceGRNModel::initFromOptions() {
  PetscErrorCode ierr;
  char inFile[PETSC_MAX_PATH_LEN];
  PetscTruth inFileSet, bootFileSet, nopddSet;
  
  // wait on init hook; possible regridding!:
  ierr = IceModel::initFromOptions(PETSC_FALSE); CHKERRQ(ierr);  

  ierr = PetscOptionsGetString(PETSC_NULL, "-if", inFile,
                               PETSC_MAX_PATH_LEN, &inFileSet); CHKERRQ(ierr);
  ierr = PetscOptionsGetString(PETSC_NULL, "-bif", inFile,
                               PETSC_MAX_PATH_LEN, &bootFileSet); CHKERRQ(ierr);
  ierr = PetscOptionsHasName(PETSC_NULL, "-no_pdd", &nopddSet); CHKERRQ(ierr);

  if (nopddSet == PETSC_TRUE) {
    doPDD = PETSC_FALSE;
  } else { 
    // in this case, turn the PDD on for this derived class, so no "-pdd" option
    //   is needed to turn it on
    doPDD = PETSC_TRUE;
    if (pddStuffCreated == PETSC_FALSE) {
      ierr = initPDDFromOptions(); CHKERRQ(ierr);
    }
    PetscTruth pddSummerWarmingSet, pddStdDevSet;
    ierr = PetscOptionsHasName(PETSC_NULL, "-pdd_summer_warming",
              &pddSummerWarmingSet); CHKERRQ(ierr);
    // note IceGRNModel::getSummerWarming() is below
    if (pddSummerWarmingSet == PETSC_TRUE) { 
      ierr = verbPrintf(1, grid.com, 
         "WARNING: -pdd_summer_warming option ignored.\n"
         "  Using EISMINT-GREENLAND summer temperature formula\n");
         CHKERRQ(ierr);
    }
    ierr = PetscOptionsHasName(PETSC_NULL, "-pdd_std_dev", &pddStdDevSet); 
       CHKERRQ(ierr);
    if (pddStdDevSet == PETSC_FALSE) {
      pddStdDev = 5.0;  // EISMINT-GREENLAND default; note we allow user to override
    }
  }
  
  if (inFileSet == PETSC_TRUE) {
    if (bootFileSet) {
      ierr = verbPrintf(1, grid.com, "WARNING: -bif and -if given; using -if\n");
         CHKERRQ(ierr);
    }
  } else if (bootFileSet == PETSC_TRUE) {
    // though default bootstrapping has set the new temperatures, we need to set 
    // the surface temp and geothermal flux at base and then set 3D temps again
    ierr = verbPrintf(2, grid.com,
         "geothermal flux set to EISMINT-Greenland value %f W/m^2\n",
         EISMINT_G_geothermal);
    ierr = VecSet(vGhf, EISMINT_G_geothermal); CHKERRQ(ierr);
    if (haveSurfaceTemps == PETSC_FALSE) {
      ierr = verbPrintf(2, grid.com, 
         "computing surface temps by EISMINT-Greenland elevation-latitude rule\n");
         CHKERRQ(ierr);
      ierr = updateTs(); CHKERRQ(ierr);
      ierr = verbPrintf(2, grid.com, 
         "filling in temperatures at depth using quartic guess\n");
         CHKERRQ(ierr);
      ierr = putTempAtDepth(); CHKERRQ(ierr);
    }
    if (noEllesmereIcelandDelete == PETSC_FALSE) {
      ierr = verbPrintf(2, grid.com, 
         "removing extra land (Ellesmere and Iceland) using EISMINT-Greenland rule\n");
         CHKERRQ(ierr);
      ierr = cleanExtraLand(); CHKERRQ(ierr);
    }
  } else {
    SETERRQ(2, "ERROR: IceGRNModel needs an input file\n");
  }

  if (!isInitialized()) {
    SETERRQ(1, "IceGRNModel has not been initialized.\n");
  }

  ierr = afterInitHook(); CHKERRQ(ierr);  // note regridding can happen here
  return 0;
}


PetscErrorCode IceGRNModel::additionalAtStartTimestep() {
  PetscErrorCode ierr;

  // for all experiments, at each time step we need to recompute
  // surface temperatures from surface elevation and latitude, unless the 
  // user supplies an additional map of mean annual surface temps
  if (haveSurfaceTemps == PETSC_FALSE) {
    ierr = updateTs(); CHKERRQ(ierr);
  }

  if (expernum == 4) {  // for GWL3 apply global warming temperature forcing
    PetscScalar t_increase;
    PetscScalar age = grid.year - startYear;
    if (age <= 80.0) {
      t_increase = age * 0.035;
    } else if (age <= 500.0) {
      t_increase = 2.8 + (age - 80.0) * 0.0017;
    } else {
      t_increase = 3.514;
    }
    ierr = VecShift(vTs,t_increase); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode IceGRNModel::calculateMeanAnnual(
                    PetscScalar h, PetscScalar lat, PetscScalar *val) {
  // EISMINT-Greenland surface temperature model
  PetscScalar Z = PetscMax(h, 20 * (lat - 65));
  *val = 49.13 - 0.007992 * Z - 0.7576 * (lat);
  return 0;
}


PetscScalar IceGRNModel::getSummerWarming(
     const PetscScalar elevation, const PetscScalar latitude, const PetscScalar Ta) const {
  // this is virtual in IceModel
  // EISMINT-Greenland summer surface temperature model (expressed as
  // warming above mean annual); Ta,Ts in deg C; Ta is mean annual, Ts is summer peak
  const PetscScalar Ts = 30.38 - 0.006277 * elevation - 0.3262 * latitude;
  return Ts - Ta;
}


PetscErrorCode IceGRNModel::updateTs() {
  PetscErrorCode ierr;
  PetscScalar val;
  PetscScalar **Ts, **lat, **h;
  
  ierr = verbPrintf(4, grid.com, 
     "recomputing surface temperatures according to EISMINT-Greenland rule"
     " and setting TsOffset=0.0\n");
     CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vTs, &Ts); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vLatitude, &lat); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vh, &h); CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j<grid.ys+grid.ym; ++j) {
      calculateMeanAnnual(h[i][j], lat[i][j], &val);
      Ts[i][j] = val + ice->meltingTemp;
    }
  }
  ierr = DAVecRestoreArray(grid.da2, vTs, &Ts); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vLatitude, &lat); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vh, &h); CHKERRQ(ierr);
  
  TsOffset = 0.0;  // see IceModel::updateForcing(); note old offset is not 
                   //    in vTs because vTs is totally recomputed
  return 0;
}


PetscErrorCode IceGRNModel::ellePiecewiseFunc(PetscScalar lon, PetscScalar *lat) {
  float l1_x1 = -68.18, l1_y1 = 80.1;
  float l1_x2 = -62, l1_y2 = 82.24;
  float m, b;  // piecewise boundaries

  m = (l1_y1 - l1_y2) / (l1_x1 - l1_x2);
  b = (l1_y2) - m * (l1_x2);
  *lat = m * lon + b;
  return 0;
}


PetscErrorCode IceGRNModel::cleanExtraLand(){
  PetscErrorCode ierr;
  PetscScalar lat_line;
  // make mask SE of the following point into FLOATING_OCEAN0
  float ice_lon = 30, ice_lat = 67;
  PetscScalar **lat, **lon, **mask;

  ierr = DAVecGetArray(grid.da2, vLatitude, &lat); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vLongitude, &lon); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j<grid.ys+grid.ym; ++j) {
      ellePiecewiseFunc(lon[i][j], &lat_line);
      if (lat[i][j]>lat_line) {
          mask[i][j] = MASK_FLOATING_OCEAN0;
      } else if (lat[i][j] < ice_lat && lon[i][j] > -ice_lon) {
        mask[i][j] = MASK_FLOATING_OCEAN0;
      }
    }
  }
  ierr = DAVecRestoreArray(grid.da2, vLatitude, &lat); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vLongitude, &lon); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  // when mask is changed we must communicate the ghosted values
  //   because neighbor's mask matters
  ierr = DALocalToLocalBegin(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);
  return 0;
}

