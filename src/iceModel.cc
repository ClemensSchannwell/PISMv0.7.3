// Copyright (C) 2004-2006 Jed Brown and Ed Bueler
//
// This file is part of Pism.
//
// Pism is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// Pism is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with Pism; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cmath>
#include <cstring>
#include <petscda.h>

#include "iceModel.hh"

// following numerical values have some significance; see updateSurfaceElevationAndMask() below
const int IceModel::MASK_SHEET = 1;
const int IceModel::MASK_DRAGGING = 2;
const int IceModel::MASK_FLOATING = 3;
// (modMask(mask[i][j]) == MASK_FLOATING) is criteria for floating; ..._OCEAN0 only used if -ocean_kill 
const int IceModel::MASK_FLOATING_OCEAN0 = 7;

//used in iMutil.C
const PetscScalar IceModel::DEFAULT_ADDED_TO_SLOPE_FOR_DIFF_IN_ADAPTIVE = 1.0e-4;
const PetscScalar IceModel::DEFAULT_ADDED_TO_GDMAX_ADAPT = 1.0e-2;
const PetscScalar IceModel::DEFAULT_ADAPT_TIMESTEP_RATIO = 0.12;  // yes, I'm confident this is o.k.

//used in iMIO.C
const PetscScalar IceModel::DEFAULT_h_VALUE_MISSING = 0.0;
const PetscScalar IceModel::DEFAULT_H_VALUE_MISSING = 0.0;
const PetscScalar IceModel::DEFAULT_BED_VALUE_MISSING = -5000.0;
const PetscScalar IceModel::DEFAULT_ACCUM_VALUE_MISSING = -0.5/ secpera;
const PetscScalar IceModel::DEFAULT_SURF_TEMP_VALUE_MISSING = 270.0;

//used in iMvelocity.C
const PetscScalar IceModel::DEFAULT_MINH_MACAYEAL = 10.0;  // m; minimum thickness for MacAyeal velocity computation
const PetscScalar IceModel::DEFAULT_MIN_SHEET_TO_DRAGGING = 50.0;   // m/a; critical SIA speed for switch SIA --> MacAyeal
const PetscScalar IceModel::DEFAULT_MAX_SPEED_DRAGGING_TO_SHEET = 5.0;  // m/a; crit Mac speed for switch MacAyeal --> SIA
const PetscScalar IceModel::DEFAULT_MAX_SPEEDSIA_DRAGGING_TO_SHEET = 50.0;    // m/a; crit SIA speed for switch MacAyeal --> SIA
const PetscScalar IceModel::DEFAULT_MAXSLOPE_MACAYEAL = 1.0e-3; // no units/pure number; cap to avoid bad behavior
const PetscInt    IceModel::DEFAULT_MAX_ITERATIONS_MACAYEAL = 50;
const PetscScalar IceModel::DEFAULT_EPSILON_MACAYEAL = 1.0e15;  // kg m^-1 s^-1;  initial amount of (denominator) regularization in computation of effective viscosity
const PetscScalar IceModel::DEFAULT_EPSILON_MULTIPLIER_MACAYEAL = 4.0;  // no units/pure number; epsilon goes up by this ratio when
// previous value failed
const PetscScalar IceModel::DEFAULT_VERT_VEL_MACAYEAL = 0.0;  // temp evolution uses this value; incompressibility not satisfied
const PetscScalar IceModel::DEFAULT_MAX_VEL_FOR_CFL = 1000.0 / secpera;  // 10 km/a
const PetscScalar IceModel::DEFAULT_BASAL_DRAG_COEFF_MACAYEAL = 2.0e9; // Pa s m^-1 Hulbe & MacAyeal (1999), p. 25,356
//used in iMvelocity.C and iMutil.C
const PetscScalar IceModel::DEFAULT_MIN_TEMP_FOR_SLIDING = 273.0;  // note less than ice.meltingTemp;
// if above this value then decide to slide
const PetscScalar IceModel::DEFAULT_INITIAL_AGE_YEARS = 1000.0;  // age to start age computation
const PetscScalar IceModel::DEFAULT_GRAIN_SIZE = 0.001;  // size of grains when assumed constant; for gk ice
const PetscScalar IceModel::DEFAULT_OCEAN_HEAT_FLUX = 1.0;  // 1 W/m^2; about 8 times more heating than peak of Shapiro&Ritzwoller geo heat flux map (i.e. about 130 mW/m^2)


PetscErrorCode getFlowLawFromUser(MPI_Comm com, IceType* &ice, PetscInt &flowLawNum) {
    PetscErrorCode ierr;
    PetscTruth     flowlawSet = PETSC_FALSE, useGK = PETSC_FALSE;

    ierr = PetscOptionsGetInt(PETSC_NULL, "-law", &flowLawNum, &flowlawSet); CHKERRQ(ierr);
    ierr = PetscOptionsHasName(PETSC_NULL, "-gk", &useGK); CHKERRQ(ierr);  // option included for backward compat
    if (useGK==PETSC_TRUE) {
      flowlawSet = PETSC_TRUE;
      flowLawNum = 4;
    }
    if (flowlawSet == PETSC_TRUE) {
      ierr = PetscPrintf(com, 
          "  [using flow law %d"
          " (where 0=Paterson-Budd,1=cold P-B,2=warm P-B,3=Hooke,4=Goldsby-Kohlstedt)]\n",
          flowLawNum); CHKERRQ(ierr);
    }
    switch (flowLawNum) {
      case 0: // Paterson-Budd
        ice = new ThermoGlenIce;  
        break;
      case 1: // cold part of P-B
        ice = new ThermoGlenArrIce;  
        break;
      case 2: // warm part of P-B
        ice = new ThermoGlenArrIceWarm;  
        break;
      case 3: // Hooke
        ice = new ThermoGlenIceHooke;
        break;
      case 4: // Goldsby Kohlstedt
        ice = new HybridIce;  
        break;
      case 5: // Goldsby Kohlstedt stripped down
        ice = new HybridIceStripped;  
        break;
      default:
        SETERRQ(1,"\nflow law number for to initialize IceModel must be 0,1,2,3,4,5\n");
    }
    return 0;
}


IceModel::IceModel(IceGrid &g, IceType &i): grid(g), ice(i) {
  PetscErrorCode ierr;

  createVecs_done = PETSC_FALSE;
  ierr = setDefaults();
  if (ierr != 0) {
    PetscPrintf(grid.com, "Error setting defaults.\n");
    PetscEnd();
  }        
}


IceModel::~IceModel() {
  destroyVecs();
  destroyViewers();
}


PetscErrorCode IceModel::createVecs() {
  PetscErrorCode ierr;

  if (createVecs_done) {
    ierr = destroyVecs(); CHKERRQ(ierr);
  }
  
  ierr = DACreateLocalVector(grid.da3, &vu); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vv); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vw); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vSigma); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vT); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vtau); CHKERRQ(ierr);
  ierr = VecDuplicate(vu, &vgs); CHKERRQ(ierr);

  ierr = DACreateLocalVector(grid.da3b, &vTb); CHKERRQ(ierr);

  ierr = DACreateLocalVector(grid.da2, &vh); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vH); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vbed); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vAccum); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vTs); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vMask); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vGhf); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vubar); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vvbar); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vbasalMeltRate); CHKERRQ(ierr);
  ierr = VecDuplicate(vh, &vuplift); CHKERRQ(ierr);

  ierr = VecDuplicateVecs(vh, 2, &vuvbar); CHKERRQ(ierr);

  ierr = VecDuplicateVecs(vh, nWork2d, &vWork2d); CHKERRQ(ierr);
  ierr = VecDuplicateVecs(vu, nWork3d, &vWork3d); CHKERRQ(ierr);

  ierr = DACreateGlobalVector(grid.da2, &g2); CHKERRQ(ierr);
  ierr = DACreateGlobalVector(grid.da3, &g3); CHKERRQ(ierr);
  ierr = DACreateGlobalVector(grid.da3b, &g3b); CHKERRQ(ierr);

  const PetscInt M = 2 * grid.p->Mx * grid.p->My;
  ierr = MatCreateMPIAIJ(grid.com, PETSC_DECIDE, PETSC_DECIDE, M, M,
                         13, PETSC_NULL, 13, PETSC_NULL,
                         &MacayealStiffnessMatrix); CHKERRQ(ierr);
  ierr = VecCreateMPI(grid.com, PETSC_DECIDE, M, &MacayealX); CHKERRQ(ierr);
  ierr = VecDuplicate(MacayealX, &MacayealRHS); CHKERRQ(ierr);
  ierr = VecCreateSeq(PETSC_COMM_SELF, M, &MacayealXLocal);
  ierr = VecScatterCreate(MacayealX, PETSC_NULL, MacayealXLocal, PETSC_NULL,
                          &MacayealScatterGlobalToLocal); CHKERRQ(ierr);
  ierr = KSPCreate(grid.com, &MacayealKSP); CHKERRQ(ierr);

  createVecs_done = PETSC_TRUE;
  return 0;
}


PetscErrorCode IceModel::destroyVecs() {
  PetscErrorCode ierr;

  ierr = bedDefCleanup(); CHKERRQ(ierr);

  ierr = VecDestroy(vu); CHKERRQ(ierr);
  ierr = VecDestroy(vv); CHKERRQ(ierr);
  ierr = VecDestroy(vw); CHKERRQ(ierr);
  ierr = VecDestroy(vSigma); CHKERRQ(ierr);
  ierr = VecDestroy(vT); CHKERRQ(ierr);
  ierr = VecDestroy(vtau); CHKERRQ(ierr);
  ierr = VecDestroy(vgs); CHKERRQ(ierr);

  ierr = VecDestroy(vh); CHKERRQ(ierr);
  ierr = VecDestroy(vH); CHKERRQ(ierr);
  ierr = VecDestroy(vbed); CHKERRQ(ierr);
  ierr = VecDestroy(vAccum); CHKERRQ(ierr);
  ierr = VecDestroy(vTs); CHKERRQ(ierr);
  ierr = VecDestroy(vMask); CHKERRQ(ierr);
  ierr = VecDestroy(vGhf); CHKERRQ(ierr);
  ierr = VecDestroy(vubar); CHKERRQ(ierr);
  ierr = VecDestroy(vvbar); CHKERRQ(ierr);
  ierr = VecDestroy(vbasalMeltRate); CHKERRQ(ierr);
  ierr = VecDestroy(vuplift); CHKERRQ(ierr);

  ierr = VecDestroyVecs(vuvbar, 2); CHKERRQ(ierr);
  ierr = VecDestroyVecs(vWork3d, nWork3d); CHKERRQ(ierr);
  ierr = VecDestroyVecs(vWork2d, nWork2d); CHKERRQ(ierr);

  ierr = VecDestroy(g2); CHKERRQ(ierr);
  ierr = VecDestroy(g3); CHKERRQ(ierr);
  ierr = VecDestroy(g3b); CHKERRQ(ierr);

  ierr = KSPDestroy(MacayealKSP); CHKERRQ(ierr);
  ierr = MatDestroy(MacayealStiffnessMatrix); CHKERRQ(ierr);
  ierr = VecDestroy(MacayealX); CHKERRQ(ierr);
  ierr = VecDestroy(MacayealRHS); CHKERRQ(ierr);
  ierr = VecDestroy(MacayealXLocal); CHKERRQ(ierr);
  ierr = VecScatterDestroy(MacayealScatterGlobalToLocal); CHKERRQ(ierr);

  return 0;
}


void IceModel::setTimeStepYears(PetscScalar y) {
  dt = y * secpera;
  doAdaptTimeStep = PETSC_FALSE;
}

void IceModel::setMaxTimeStepYears(PetscScalar y) {
  maxdt = y * secpera;
  doAdaptTimeStep = PETSC_TRUE;
}

void IceModel::setAdaptTimeStepRatio(PetscScalar c) {
  adaptTimeStepRatio = c;
}

PetscErrorCode IceModel::setStartYear(PetscScalar y0) {
  startYear = y0;

  return 0;
}

PetscErrorCode IceModel::setEndYear(PetscScalar ye) {
    
  if (ye < startYear)   {
    SETERRQ(1, "ERROR: endYear < startYear\n");
  } else {
    endYear = ye;
    relativeEndYear = PETSC_FALSE;
  }
    
  return 0;
}

PetscErrorCode IceModel::setRunYears(PetscScalar y) {
  PetscErrorCode ierr;
    
  ierr = setEndYear(startYear + y); CHKERRQ(ierr);
  relativeEndYear = PETSC_TRUE;

  return 0;
}

void  IceModel::setInitialAgeYears(PetscScalar d) {
  PetscErrorCode ierr;
  
  ierr = VecSet(vtau, d*secpera);
}

void IceModel::setShowViewers(PetscTruth show_viewers) {
  showViewers = show_viewers;
}

void IceModel::setDoMassBal(PetscTruth do_mb) {
  doMassBal = do_mb;
}

void IceModel::setDoVelocity(PetscTruth do_v) {
  doVelocity = do_v;
}

void IceModel::setDoTemp(PetscTruth do_temp) {
  doTemp = do_temp;
}

void IceModel::setDoGrainSize(PetscTruth do_gs) {
  doGrainSize = do_gs;
}

void IceModel::setDoBedDef(PetscTruth do_bd) {
  doBedDef = do_bd;
}

void IceModel::setDoBedIso(PetscTruth do_iso) {
  doBedIso = do_iso;
}

void IceModel::setIsDrySimulation(PetscTruth is_dry) {
  isDrySimulation = is_dry;
}

void IceModel::setBeVerbose(PetscTruth verbose) {
  beVerbose = verbose;
}

void IceModel::setAllGMaxVelocities(PetscScalar uvw_for_cfl) {
  gmaxu=uvw_for_cfl;
  gmaxv=uvw_for_cfl;
  gmaxw=uvw_for_cfl;
}

void IceModel::setThermalBedrock(PetscTruth tb) {
  thermalBedrock = tb;
}

void IceModel::setOceanKill(PetscTruth ok) {
  doOceanKill = ok;
}

void IceModel::setUseMacayealVelocity(PetscTruth umv) {
  useMacayealVelocity = umv;
}

void IceModel::setConstantNuForMacAyeal(PetscScalar nu) {
  useConstantNuForMacAyeal = PETSC_TRUE;
  constantNuForMacAyeal = nu;
}

void IceModel::setMacayealEpsilon(PetscScalar meps) {
  macayealEpsilon = meps;
}

void IceModel::setMacayealRelativeTolerance(PetscScalar mrc) {
  macayealRelativeTolerance = mrc;
}

void IceModel::setEnhancementFactor(PetscScalar e) {
  enhancementFactor = e;
}

void IceModel::setMuSliding(PetscScalar mu) {
  muSliding = mu;
}

void IceModel::setGlobalMinTemp(PetscScalar mintemp) {
  globalMinTemp = mintemp;
}


void IceModel::setTempskip(PetscInt ts) {
  tempskip = ts;
}

void IceModel::setGSIntervalYears(PetscScalar years) {
  gsIntervalYears = years;
}

void IceModel::setBedDefIntervalYears(PetscScalar years) {
  bedDefIntervalYears = years;
}

void IceModel::setAllowRegridding(PetscTruth p) {
  allowRegridding = p;
}

void IceModel::setIsothermalFlux(PetscTruth use) {
  useIsothermalFlux = use;
}

void IceModel::setNoSpokes(PetscInt level) {
  noSpokesLevel = level;
}

void IceModel::setIsothermalFlux(PetscTruth use, PetscScalar n, PetscScalar A) {
  setIsothermalFlux(use);
  isothermalFlux_n_exponent = n;
  isothermalFlux_A_softness = A;
}

PetscTruth IceModel::isInitialized() const {
  return initialized_p;
}


PetscErrorCode IceModel::updateSurfaceElevationAndMask() {
  // should be called when either ice thickness or bed elevation change, to 
  // maintain consistency of geometry
  PetscErrorCode ierr;
  PetscScalar **h, **bed, **H, **mask, ***T;
  const int MASK_GROUNDED_TO_DETERMINE = 999;

  ierr = DAVecGetArray(grid.da2, vh, &h); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbed, &bed); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da3, vT, &T); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // take this opportunity to check that H[i][j] >= 0
      if (H[i][j] < 0) {
        SETERRQ2(1,"Thickness negative at point i=%d, j=%d",i,j);
      }

      const PetscScalar hgrounded = bed[i][j] + H[i][j];

      if (isDrySimulation == PETSC_TRUE) {
        h[i][j] = hgrounded;
        // don't update mask; potentially one would want to do MacAyeal
        //   dragging ice shelf in dry case and/or ignor mean sea level elevation
      } else {

        const PetscScalar hfloating = (1-ice.rho/ocean.rho) * H[i][j];
        if (modMask(mask[i][j]) == MASK_FLOATING) {
          // check t=whether you are actually floating or grounded
          if (hgrounded > hfloating+1.0) {
            mask[i][j] = MASK_GROUNDED_TO_DETERMINE;
            h[i][j] = hgrounded; // actually grounded so update h
          } else {
            h[i][j] = hfloating; // actually floating so update h
          }
        } else { // deal with grounded ice according to mask
          if (hgrounded > hfloating-1.0) {
            h[i][j] = hgrounded; // actually grounded so update h
          } else {
            mask[i][j] = MASK_FLOATING;
            h[i][j] = hfloating; // actually floating so update h
          }
        }

        if (intMask(mask[i][j]) == MASK_GROUNDED_TO_DETERMINE) {
          if (useMacayealVelocity != PETSC_TRUE) {
            mask[i][j] = MASK_SHEET;
          } else {
            // if frozen to bed or essentially frozen to bed then make it SHEET
            if (T[i][j][0] + ice.beta_CC_grad * H[i][j] 
                         < DEFAULT_MIN_TEMP_FOR_SLIDING) { 
              mask[i][j] = MASK_SHEET;
            } else {
              // determine type of grounded ice by vote-by-neighbors
              //   (BOX stencil neighbors!):
              const PetscScalar neighmasksum = 
                modMask(mask[i-1][j+1]) + modMask(mask[i][j+1]) + modMask(mask[i+1][j+1]) +
                modMask(mask[i-1][j])   +                       + modMask(mask[i+1][j])  +
                modMask(mask[i-1][j-1]) + modMask(mask[i][j-1]) + modMask(mask[i+1][j-1]);
              // make SHEET if either all neighbors are SHEET or at most one is 
              //   DRAGGING; if any are floating then ends up DRAGGING:
              if (neighmasksum <= (7*MASK_SHEET + MASK_DRAGGING + 0.1)) { 
                mask[i][j] = MASK_SHEET;
              } else { // otherwise make DRAGGING
                mask[i][j] = MASK_DRAGGING;
              }
            }
          }
        }
        
      }

    }
  }

  ierr = DAVecRestoreArray(grid.da3, vT, &T); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vh, &h); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vbed, &bed); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);

  ierr = DALocalToLocalBegin(grid.da2, vh, INSERT_VALUES, vh); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vh, INSERT_VALUES, vh); CHKERRQ(ierr);
  ierr = DALocalToLocalBegin(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceModel::massBalExplicitStep() {
  const PetscScalar   dx = grid.p->dx, dy = grid.p->dy;
  PetscErrorCode ierr;
  PetscScalar **H, **Hnew, **uvbar[2];
  PetscScalar **u, **v, **accum, **basalMeltRate, **mask;
  Vec vHnew = vWork2d[0];

  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbasalMeltRate, &basalMeltRate); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vAccum, &accum); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vuvbar[0], &uvbar[0]); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vuvbar[1], &uvbar[1]); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vubar, &u); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vvbar, &v); CHKERRQ(ierr);
  ierr = VecCopy(vH, vHnew); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vHnew, &Hnew); CHKERRQ(ierr);

  PetscScalar icecount = 0.0;
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (H[i][j] > 0.0)  icecount++;
      PetscScalar divQ;
      if (intMask(mask[i][j]) == MASK_SHEET) { // staggered grid Div(Q) (really for Q = D grad h)
        divQ =
          (uvbar[0][i][j] * 0.5*(H[i][j] + H[i+1][j])
           - uvbar[0][i-1][j] * 0.5*(H[i-1][j] + H[i][j])) / dx
          + (uvbar[1][i][j] * 0.5*(H[i][j] + H[i][j+1])
             - uvbar[1][i][j-1] * 0.5*(H[i][j-1] + H[i][j])) / dy;
      } else { // upwinded, regular grid Div(Q), for Q = Ubar H, computed as
               //     Div(Q) = U . grad H + Div(U) H
        divQ =
          u[i][j] * (u[i][j] < 0 ? H[i+1][j]-H[i][j] : H[i][j]-H[i-1][j]) / dx
          + v[i][j] * (v[i][j] < 0 ? H[i][j+1]-H[i][j] : H[i][j]-H[i][j-1]) / dy
          + H[i][j] * ((u[i+1][j]-u[i-1][j])/(2*dx) + (v[i][j+1]-v[i][j-1])/(2*dy));
      }
      PetscScalar dHdt = accum[i][j] - basalMeltRate[i][j] - divQ;

      Hnew[i][j] += dHdt * dt;
      if ((Hnew[i][j] < 0) ||
          ((doOceanKill == PETSC_TRUE) && (intMask(mask[i][j]) == MASK_FLOATING_OCEAN0)) )
        Hnew[i][j] = 0.0;
    }
  }

  ierr = DAVecRestoreArray(grid.da2, vbasalMeltRate, &basalMeltRate); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vAccum, &accum); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vuvbar[0], &uvbar[0]); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vuvbar[1], &uvbar[1]); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vubar, &u); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vvbar, &v); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vHnew, &Hnew); CHKERRQ(ierr);

  // compute dH/dt (thickening rate)
  ierr = VecWAXPY(vWork2d[1], -1, vH, vHnew); CHKERRQ(ierr);
  ierr = VecScale(vWork2d[1],1.0/dt); CHKERRQ(ierr);
  if (dhView != PETSC_NULL) { // -g option: view m/year rate of change
    ierr = DALocalToGlobal(grid.da2, vWork2d[1], INSERT_VALUES, g2); CHKERRQ(ierr);
    ierr = VecScale(g2, secpera); CHKERRQ(ierr); // to report in m/a
    ierr = VecView(g2, dhView); CHKERRQ(ierr);
  }
  // average value of dH/dt; also d(volume)/dt
  PetscScalar gicecount;
  ierr = PetscGlobalSum(&icecount, &gicecount, grid.com); CHKERRQ(ierr);
  ierr = VecSum(vWork2d[1], &gdHdtav); CHKERRQ(ierr);
  dvoldt = gdHdtav * grid.p->dx * grid.p->dy;  // m^3/s
  gdHdtav = gdHdtav / gicecount; // m/s

  // finally copy vHnew into vH (and communicate ghosted values at same time)
  ierr = DALocalToLocalBegin(grid.da2, vHnew, INSERT_VALUES, vH); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vHnew, INSERT_VALUES, vH); CHKERRQ(ierr);

  // update h and mask
  ierr = updateSurfaceElevationAndMask(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceModel::run() {
  PetscErrorCode  ierr;

  ierr = initSounding(); CHKERRQ(ierr);
  ierr = PetscPrintf(grid.com,
          "$$$$$      YEAR (+   STEP):     VOL    AREA    MELTF     THICK0     TEMP0\n");
  CHKERRQ(ierr);
  ierr = PetscPrintf(grid.com, "$$$$$"); CHKERRQ(ierr);
  ierr = summary(true,true); CHKERRQ(ierr);  // report starting state

  PetscInt    it = 0;
  PetscScalar dt_temp = 0.0;
  bool tempAgeStep;

  // main loop for time evolution
  for (PetscScalar year = startYear; year < endYear; year += dt/secpera, it++) {
    // compute bed deformation, which only depends on current thickness and bed elevation
    if (doBedDef == PETSC_TRUE) {
      ierr = bedDefStepIfNeeded(); CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(grid.com, "$"); CHKERRQ(ierr);
    }

    // always do vertically-average velocity calculation; only update velocities at depth if
    // needed for temp and age calculation
    tempAgeStep = ((it % tempskip == 0) && (doTemp == PETSC_TRUE));
    // if (doVelocity == PETSC_TRUE) // flag ignored
    ierr = velocity(tempAgeStep); CHKERRQ(ierr);
    ierr = PetscPrintf(grid.com, tempAgeStep ? "v" : "V" ); CHKERRQ(ierr);

    // now that velocity field is up to date, compute grain size
    if (doGrainSize == PETSC_TRUE) {
      ierr = updateGrainSizeIfNeeded(); CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(grid.com, "$"); CHKERRQ(ierr);
    }
    
    // adapt time step using velocities just computed
    dt = PetscMin(maxdt, (endYear-year) * secpera);  // don't go past end; "propose" this
    if (doAdaptTimeStep == PETSC_TRUE) {
      if (doMassBal == PETSC_TRUE) {
        ierr = adaptTimeStepDiffusivity();  CHKERRQ(ierr);
      }
      if (doTemp == PETSC_TRUE) {
        ierr = adaptTimeStepCFL();  CHKERRQ(ierr);  // if tempskip > 1 then here dt is reduced
                                                    // by a factor of tempskip
      }
    }
    // IceModel::dt is now set correctly according to mass-balance and CFL criteria

    ierr = PetscPrintf(PETSC_COMM_SELF, "\n[rank=%d, it=%d, year=%f, dt=%f]", grid.rank, it, year, dt/secpera); CHKERRQ(ierr);

    dt_temp += dt;
    grid.p->year += dt / secpera;  // adopt it
    
    if ((doTemp == PETSC_TRUE) &&  (tempAgeStep)) {
      ierr = temperatureStep(PETSC_FALSE, dt_temp); CHKERRQ(ierr);  // also does age
      dt_temp = 0.0;
      ierr = PetscPrintf(grid.com, "t"); CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(grid.com, "$"); CHKERRQ(ierr);
    }
    
    if (doMassBal == PETSC_TRUE) {
      ierr = massBalExplicitStep(); CHKERRQ(ierr);
      ierr = PetscPrintf(grid.com, "f"); CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(grid.com, "$"); CHKERRQ(ierr);
    }
    
    ierr = summary(tempAgeStep,true); CHKERRQ(ierr);

    ierr = updateViewers(); CHKERRQ(ierr);
  }
  
  return 0;
}

// not no range checking in these two:
int IceModel::intMask(PetscScalar maskvalue) {
  return static_cast<int>(floor(maskvalue + 0.5));
}

int IceModel::modMask(PetscScalar maskvalue) {
  int intmask = static_cast<int>(floor(maskvalue + 0.5));
  if (intmask > MASK_FLOATING) {
    return intmask - 4;
  } else {
    return intmask;
  }
}

