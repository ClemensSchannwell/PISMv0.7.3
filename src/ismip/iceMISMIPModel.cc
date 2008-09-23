// Copyright (C) 2008 Ed Bueler
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

#include <cstring>
#include <petscda.h>
#include "../base/pism_const.hh"
#include "../base/grid.hh"
#include "../base/materials.hh"
#include "../base/iceModel.hh"
#include "iceMISMIPModel.hh"


/* 
This derived class illustrates the bug-creation problem that
if the signature of a virtual method of the base class IceModel is modified,
then there is a good chance the behavior of MISMIP runs will change but
there will be no compile time error to indicate it.

The temporary solution is indicated by the print commands with
"MAKE SURE THIS IS REALLY BEING USED!!" below.  Both for 
IceType --> MISMIPIce and for IceModel --> IceMISMIPModel.
The use of COMM_SELF in these print commands is reasonably important
because, for instance, if COMM_WORLD is used, and we run 
with 8 processors, then some processors "own" entirely floating ice 
so they never call the print statement and then no output at all occurs.
 */
 
MISMIPIce::MISMIPIce() {
  rho = 900.0;
  n = 3;
  setA(4.6416e-24);
}


PetscErrorCode MISMIPIce::setA(const PetscScalar myA) {
  A_MISMIP = myA;
  B_MISMIP = pow(A_MISMIP, - 1.0 / ((PetscScalar) n));
  return 0;
}


PetscErrorCode MISMIPIce::printInfo(const int thresh, MPI_Comm com) {
  PetscErrorCode ierr;
  ierr = verbPrintf(thresh, com, 
    "Using MISMIP ice w  rho=%6.2f, grav=%6.4f, n=%6.4f, and A=%6.4e.\n",
    rho, grav, n, A_MISMIP); CHKERRQ(ierr);
  return 0;
}


PetscScalar MISMIPIce::flow(const PetscScalar stress, const PetscScalar temp,
                            const PetscScalar pressure) const {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"MISMIPIce::flow()\n");
  return A_MISMIP * pow(stress,n-1);
}


PetscScalar MISMIPIce::effectiveViscosity(const PetscScalar regularization,
                           const PetscScalar u_x, const PetscScalar u_y,
                           const PetscScalar v_x, const PetscScalar v_y,
                           const PetscScalar temp, const PetscScalar pressure) const  {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"MISMIPIce::effectiveViscosity()\n");
  const PetscScalar nn = (PetscScalar) n;
  const PetscScalar alpha = 0.5 * PetscSqr(u_x) + 0.5 * PetscSqr(v_y)
                             + 0.5 * PetscSqr(u_x + v_y) + 0.25 * PetscSqr(u_y + v_x);
  return 0.5 * B_MISMIP * pow(regularization + alpha, - (nn - 1.0) / (2.0 * nn));
}

PetscScalar MISMIPIce::effectiveViscosityColumn(const PetscScalar regularization,
                           const PetscScalar H, const PetscInt kbelowH,
                           const PetscInt nlevels, PetscScalar *zlevels,
                           const PetscScalar u_x, const PetscScalar u_y,
                           const PetscScalar v_x, const PetscScalar v_y,
                           const PetscScalar *T1, const PetscScalar *T2) const  {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"MISMIPIce::effectiveViscosityColumn()\n");
  // DESPITE NAME, does *not* return effective viscosity; returns viscosity times thickness.
  // NOTE: temp and pressure args to effectiveViscosity() ignored
  return H * effectiveViscosity(regularization, u_x, u_y, v_x, v_y, 0.0, 0.0);
}


PetscScalar MISMIPIce::softnessParameter(const PetscScalar T) const {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"MISMIPIce::softnessParameter()\n");
  return A_MISMIP;
}


PetscScalar MISMIPIce::hardnessParameter(const PetscScalar T) const {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"MISMIPIce::hardnessParameter()\n");
  return B_MISMIP;
}



IceMISMIPModel::IceMISMIPModel(IceGrid &g, MISMIPIce *mismip_i) : 
      IceModel(g, (IceType*)mismip_i), mismip_ice(mismip_i) {

  // some are the defaults, while some are merely in a valid range;
  //   see IceMISMIPModel::setFromOptions() for decent values
  modelnum = 1;
  exper = 1;
  sliding = 'a';
  gridmode = 1;
  stepindex = 1;
  initialthickness = 10.0; // m
  runtimeyears = 3.0e4; // a
  strcpy(initials,"ABC");
  writeExtras = PETSC_FALSE;
  steadyOrGoalAchieved = PETSC_FALSE;
  tviewcreated = PETSC_FALSE;
  m_MISMIP = 1.0/3.0; // power
  C_MISMIP = 7.624e6; // Pa m^(−1/3) s^(1/3)
  regularize_MISMIP = 0.01 / secpera; // 0.01 m/a
  dHdtnorm_atol = 1.0e-4;  // m/a
  rstats.xg = -1.0;  // deliberately invalid
}


IceMISMIPModel::~IceMISMIPModel() {
  // this destructor gets called even if user does *not* choose -mismip
  if (tviewcreated == PETSC_TRUE)   PetscViewerDestroy(tviewfile);
}


PetscErrorCode IceMISMIPModel::printBasalAndIceInfo() {
  PetscErrorCode ierr;
  if (m_MISMIP == 1.0) {
    ierr = verbPrintf(2, grid.com, 
      "Using MISMIP sliding w  tau_b = - C u,  C=%5.4e.\n", C_MISMIP); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2, grid.com, 
      "Using MISMIP sliding w  tau_b = - C (|u|^2 + eps^2)^{(m-1)/2} u,\n"
      "   m=%5.4f, C=%5.4e, and eps = %5.4f m/a.\n",
      m_MISMIP, C_MISMIP, regularize_MISMIP * secpera); CHKERRQ(ierr);
  }
  ierr = mismip_ice->printInfo(2, grid.com); CHKERRQ(ierr);
  //ierr = verbPrintf(2,grid.com,"IceModel.ice-> info: n=%7.3f, A(T=273.15)=%10.3e\n",
  //                  ice->exponent(),ice->softnessParameter(273.15)); CHKERRQ(ierr);
  return 0;
}


PetscScalar IceMISMIPModel::basalDragx(PetscScalar **tauc,
                                       PetscScalar **u, PetscScalar **v,
                                       PetscInt i, PetscInt j) const {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"IceMISMIPModel::basalDragx()\n");
  return basalIsotropicDrag(u, v, i, j);
}


PetscScalar IceMISMIPModel::basalDragy(PetscScalar **tauc,
                                       PetscScalar **u, PetscScalar **v,
                                       PetscInt i, PetscInt j) const {
  // MAKE SURE THIS IS REALLY BEING USED!!:
  //PetscPrintf(PETSC_COMM_SELF,"IceMISMIPModel::basalDragy()\n");
  return basalIsotropicDrag(u, v, i, j);
}


PetscScalar IceMISMIPModel::basalIsotropicDrag(
            PetscScalar **u, PetscScalar **v, PetscInt i, PetscInt j) const {

  PetscScalar       myC = C_MISMIP;
  if (m_MISMIP == 1.0) {
    return myC;
  } else {
    const PetscScalar magsliding = PetscSqr(u[i][j]) + PetscSqr(v[i][j])
                                   + PetscSqr(regularize_MISMIP);
    return myC * pow(magsliding, (m_MISMIP - 1.0) / 2.0);
  }
}


PetscErrorCode IceMISMIPModel::setFromOptions() {
  PetscErrorCode ierr;

  // from Table 4
  const PetscScalar Aexper1or2[10] = {0.0, // zero position not used
                        4.6416e-24,  2.1544e-24,  1.0e-24,
                        4.6416e-25,  2.1544e-25,  1.0e-25,
                        4.6416e-26,  2.1544e-26,  1.0e-26};
  // from Table 5
  const PetscScalar timeexper3a[14] = {0.0, // zero position not used
                        3.0e4, 1.5e4, 1.5e4, 
                        1.5e4, 1.5e4, 3.0e4,
                        3.0e4, 1.5e4, 1.5e4,
                        3.0e4, 3.0e4, 3.0e4,
                        1.5e4};
  const PetscScalar Aexper3a[14] = {0.0, // zero position not used
                        3.0e-25, 2.5e-25, 2.0e-25,
                        1.5e-25, 1.0e-25, 5.0e-26,
                        2.5e-26, 5.0e-26, 1.0e-25,
                        1.5e-25, 2.0e-25, 2.5e-25,
                        3.0e-25};
  // from Table 6
  const PetscScalar timeexper3b[16] = {0.0, // zero position not used
                        3.0e4, 1.5e4, 1.5e4,
                        1.5e4, 1.5e4, 1.5e4,
                        1.5e4, 3.0e4, 1.5e4,
                        1.5e4, 1.5e4, 1.5e4,
                        1.5e4, 3.0e4, 1.5e4};   //  15th VALUE LABELED AS 16 IN Table 6 !?
  const PetscScalar Aexper3b[16] = {0.0, // zero position not used
                        1.6e-24, 1.4e-24, 1.2e-24,
                        1.0e-24, 8.0e-25, 6.0e-25,
                        4.0e-25, 2.0e-25, 4.0e-25,
                        6.0e-25, 8.0e-25, 1.0e-24,
                        1.2e-24, 1.4e-24, 1.6e-24};   //  15th VALUE LABELED AS 16 IN Table 6 !?

  // read major option    -mismip [1a|1b|2a|2b|3a|3b]
  char Ee[PETSC_MAX_PATH_LEN];
  strcpy(Ee,"");
  ierr = PetscOptionsGetString(PETSC_NULL, "-mismip", Ee, PETSC_MAX_PATH_LEN, PETSC_NULL); 
            CHKERRQ(ierr);
  if (strlen(Ee) != 2) {
    SETERRQ(1,"IceMISMIPModel ERROR:  '-mismip' must be followed by two char argument;\n"
              "  i.e. '-mismip Xx' where Xx=1a,1b,2a,2b,3a,3b");
  } else {
    if ((Ee[0] < '1') || (Ee[0] > '3')) {
      SETERRQ(2,"IceMISMIPModel ERROR:  first character of string 'Xx' in"
                " '-mismip Xx' must be 1, 2, or 3");
    }
    exper = (int) Ee[0] - (int) '0';
    if ((Ee[1] == 'a') || (Ee[1] == 'b')) {
      sliding = Ee[1];
    } else {
      SETERRQ(3,"IceMISMIPModel ERROR:  second character of string 'Xx' in"
                " '-mismip Xx' must be a or b");
    }
  }

  // other options:
  // read option    -extras       [OFF]
  ierr = PetscOptionsHasName(PETSC_NULL, "-extras", &writeExtras); CHKERRQ(ierr);

  // read option    -initials     [ABC]
  ierr = PetscOptionsGetString(PETSC_NULL, "-initials", initials, 
            PETSC_MAX_PATH_LEN, PETSC_NULL);  CHKERRQ(ierr);
  if (strlen(initials) != 3) {
    ierr = verbPrintf(1,grid.com,"IceMISMIPModel WARNING:  Initials string"
                      " should usually be three chars long."); CHKERRQ(ierr);
  }

  // read option    -initialthk   [10.0]
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-initialthk", &initialthickness, PETSC_NULL);
           CHKERRQ(ierr);

  // read option    -model        [1]
  ierr = PetscOptionsGetInt(PETSC_NULL, "-model", &modelnum, PETSC_NULL); CHKERRQ(ierr);
  if ((modelnum < 1) || (modelnum > 2)) {
    SETERRQ(8,"IceMISMIPModel ERROR:  modelnum must be 1 or 2; '-model 1' or '-model 2'");
  }

  // read option    -steady_atol  [1.0e-4]
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-steady_atol", &dHdtnorm_atol, PETSC_NULL);
          CHKERRQ(ierr);

  // read option    -step         [1]
  ierr = PetscOptionsGetInt(PETSC_NULL, "-step", &stepindex, PETSC_NULL); CHKERRQ(ierr);
  if (stepindex < 1) {
    SETERRQ(4,"IceMISMIPModel ERROR:  run index N in '-run N' must be at least 1");
  }
  if ((exper == 1) || (exper == 2)) {
    if (stepindex > 9) {
      SETERRQ(5,"IceMISMIPModel ERROR:  run index N in '-run N' must be"
                " <= 9 in experiments 1 or 2");
    }
    runtimeyears = 3.0e4;
    ierr = mismip_ice->setA(Aexper1or2[stepindex]); CHKERRQ(ierr);  
  } else if (exper == 3) {
    if (sliding == 'a') {
      if (stepindex > 13) {
        SETERRQ(6,"IceMISMIPModel ERROR:  run index N in '-run N' must be"
                  " <= 13 in experiment 3a");
      }
      runtimeyears = timeexper3a[stepindex];
      ierr = mismip_ice->setA(Aexper3a[stepindex]); CHKERRQ(ierr);  
    } else if (sliding == 'b') {
      if (stepindex > 15) {
        SETERRQ(7,"IceMISMIPModel ERROR:  run index N in '-run N' must be"
                  " <= 15 in experiment 3b");
      }
      runtimeyears = timeexper3b[stepindex];
      ierr = mismip_ice->setA(Aexper3b[stepindex]); CHKERRQ(ierr);  
    } else {
      SETERRQ(99, "how did I get here?");
    }
  }

  doTemp                    = PETSC_FALSE;
  doPlasticTill             = PETSC_FALSE;
  doBedDef                  = PETSC_FALSE;

  isDrySimulation           = PETSC_FALSE;
  includeBMRinContinuity    = PETSC_FALSE;
  
  doOceanKill               = PETSC_TRUE;
  
  useSSAVelocity            = PETSC_TRUE;
  computeSurfGradInwardSSA  = PETSC_FALSE;
  useConstantHardnessForSSA = PETSC_FALSE;

  ierr = IceModel::setFromOptions(); CHKERRQ(ierr);  

  // models 1 vs 2
  if (modelnum == 1) {
    computeSIAVelocities = PETSC_FALSE;
    doSuperpose = PETSC_FALSE;
  } else if (modelnum == 2) {
    computeSIAVelocities = PETSC_TRUE;
    doSuperpose = PETSC_TRUE;
  } else {
    SETERRQ(98, "how did I get here?");    
  }

  // see Table 3
  if (sliding == 'a') {
    m_MISMIP = 1.0/3.0;
    C_MISMIP = 7.624e6;
  } else if (sliding == 'b') {
    m_MISMIP = 1.0;
    C_MISMIP = 7.2082e10;
  } else {
    SETERRQ(99, "how did I get here?");
  }
  regularize_MISMIP = 0.01 / secpera;
  
  return 0;
}


PetscErrorCode IceMISMIPModel::initFromOptions() {
  PetscErrorCode ierr;
  bool infileused;
  PetscTruth inFileSet, bootFileSet;

  // check if input file was used
  ierr = PetscOptionsHasName(PETSC_NULL, "-if", &inFileSet); CHKERRQ(ierr);
  ierr = PetscOptionsHasName(PETSC_NULL, "-bif", &bootFileSet); CHKERRQ(ierr);
  infileused = ((inFileSet == PETSC_TRUE) || (bootFileSet == PETSC_TRUE));

  ierr = verbPrintf(2,grid.com, 
      "initializing MISMIP model %d, experiment %d%c, grid mode %d, step %d (A=%5.4e)\n", 
      modelnum,exper,sliding,gridmode,stepindex,
      mismip_ice->softnessParameter(273.15)); CHKERRQ(ierr);
  if (infileused) {
    ierr = verbPrintf(1,grid.com, 
       "IceMISMIPModel: -if or -bif option used; not using"
       "  certain MISMIP formulas to initialize\n");
       CHKERRQ(ierr);
  } else { // usual case: initialize grid and variables from MISMIP formulas
    ierr = grid.createDA(); CHKERRQ(ierr);
    ierr = createVecs(); CHKERRQ(ierr);

    const PetscScalar   L = 1800.0e3;      // Horizontal half-width of grid
    // NOTE: y takes place of x!!!
    ierr = determineSpacingTypeFromOptions(PETSC_FALSE); CHKERRQ(ierr);

    // FIXME:  is this Lz an adequate choice for thickness for all runs?
    const PetscScalar MISMIPmaxThick = 6000.0;
    
    //   (could be set in setFromOptions() according to experiment/run/...)
    // effect of double rescale is to compute grid.dy so we can get square cells
    //   (in horizontal)
    ierr = grid.rescale_and_set_zlevels(1000.0e3, L, MISMIPmaxThick,PETSC_TRUE,PETSC_FALSE);
             CHKERRQ(ierr); 
    const PetscScalar   Lx_desired = (grid.dy * grid.Mx) / 2.0;
    ierr = grid.rescale_and_set_zlevels(Lx_desired, L, MISMIPmaxThick,PETSC_TRUE,PETSC_FALSE);
             CHKERRQ(ierr); 

    // all of these relate to models which should be turned off ...
    ierr = VecSet(vHmelt, 0.0); CHKERRQ(ierr);
    // none use Goldsby-Kohlstedt or do age calc
    setInitialAgeYears(initial_age_years_default);
    ierr = VecSet(vuplift,0.0); CHKERRQ(ierr);  // no bed deformation
    ierr = VecSet(vTs, ice->meltingTemp); CHKERRQ(ierr);
    ierr = T3.setToConstant(ice->meltingTemp); CHKERRQ(ierr);
    ierr = Tb3.setToConstant(ice->meltingTemp); CHKERRQ(ierr);

    ierr = VecSet(vAccum, 0.3/secpera); CHKERRQ(ierr);

    ierr = VecSet(vH, initialthickness); CHKERRQ(ierr);

    ierr = setMISMIPBed(); CHKERRQ(ierr);
    ierr = setMISMIPMask(); CHKERRQ(ierr);
    ierr = verbPrintf(4,grid.com,"IceMISMIPModel: bed topography and mask stored\n");
              CHKERRQ(ierr);

    ierr = updateSurfaceElevationAndMask(); CHKERRQ(ierr);

    initialized_p = PETSC_TRUE;
  }
  
  ierr = IceModel::initFromOptions(PETSC_TRUE); CHKERRQ(ierr);  // regridding can happen here

  if (!isInitialized()) {
    SETERRQ(1, "ERROR: IceMISMIPModel has not been initialized!\n");
  }

  // option  -useSMthk foo.nc  :
  //   initialize from thickness in file  foo.nc;  usually this file is written by
  //   solverSM.py
  PetscTruth useSMthkSet;
  char SMthk_filename[PETSC_MAX_PATH_LEN];
  ierr = PetscOptionsGetString(PETSC_NULL, "-useSMthk", SMthk_filename, 
                PETSC_MAX_PATH_LEN, &useSMthkSet); CHKERRQ(ierr);
  if (useSMthkSet == PETSC_TRUE) {
    ierr = readThkFromSMResult(SMthk_filename); CHKERRQ(ierr);
  }

  // determine gridmode from My
  if (grid.My == 151) 
    gridmode = 1;
  else if (grid.My == 1501) 
    gridmode = 2;
  else
    gridmode = 3;

  // create prefix (e.g.) "EBU1_2b_M1_A3" for output files with names (e.g.)
  //   EBU1_2b_M1_A3.nc, EBU1_2b_M1_A3_t, EBU1_2b_M1_A3_ss, and EBU1_2b_M1_A3_f
  snprintf(mprefix, sizeof(mprefix), "%s%d_%d%c_M%d_A%d",
           initials, modelnum, exper, sliding, gridmode, stepindex);

  // if user says "-o foo"
  PetscTruth  oused;
  char        oname[PETSC_MAX_PATH_LEN];
  ierr = PetscOptionsGetString(PETSC_NULL, "-o", oname, PETSC_MAX_PATH_LEN, &oused);
           CHKERRQ(ierr);
  if (oused == PETSC_FALSE) {
    // act like user set the output name
    ierr = PetscOptionsSetValue("-o",mprefix);  CHKERRQ(ierr);
  }
  ierr = PetscOptionsGetString(PETSC_NULL, "-o", oname, PETSC_MAX_PATH_LEN, &oused);
           CHKERRQ(ierr);
  ierr = verbPrintf(2,grid.com,
       "IceMISMIPModel:  MISMIP options read.  Will save file\n"
       "  %s_t during run, %s.nc at end of run,\n"
       "  and files %s_ss, %s_f at end of run if\n"
       "  steady state achieved.\n",
       mprefix,oname,mprefix,mprefix); CHKERRQ(ierr);

  // use MISMIP runtimeyears UNLESS USER SPECIFIES A RUN LENGTH
  // use -y option, if given, to overwrite runtimeyears
  PetscTruth ySet, ysSet, yeSet;
  ierr = PetscOptionsHasName(PETSC_NULL, "-y", &ySet); CHKERRQ(ierr);
  ierr = PetscOptionsHasName(PETSC_NULL, "-ys", &ysSet); CHKERRQ(ierr);
  ierr = PetscOptionsHasName(PETSC_NULL, "-ye", &yeSet); CHKERRQ(ierr);
  if ( (ySet == PETSC_TRUE) || ( (ysSet == PETSC_TRUE) && (yeSet == PETSC_TRUE) ) ) {
    ierr = verbPrintf(2,grid.com,
      "IceMISMIPModel: ignoring MISMIP-specified run length and using value\n"
      "  from user option -y (or -ys and -ye)\n"); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2,grid.com,
      "IceMISMIPModel: setting run length to %5.2f years (from MISMIP specs)\n",
      runtimeyears); CHKERRQ(ierr);
    if (ysSet == PETSC_FALSE) {
      grid.year = 0.0;
      ierr = setStartYear(grid.year); CHKERRQ(ierr);
    }
    ierr = setEndYear(startYear + runtimeyears); CHKERRQ(ierr);
    yearsStartRunEndDetermined = PETSC_TRUE;
  }

  ierr = printBasalAndIceInfo(); CHKERRQ(ierr);
  
  // automatic parallel layout from DACreate2d(...PETSC_DECIDE...) in grid.cc
  //   LOOKS GOOD:
  //ierr = verbPrintf(1,grid.com,"result from DAView(da2,..):\n\n"); CHKERRQ(ierr);
  //ierr = DAView(grid.da2,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);
  //ierr = verbPrintf(1,grid.com,"\n\n"); CHKERRQ(ierr);

// FIXME: report on these flags: doTemp=false, doBedDef=false, doPlasticTill=false  ?
// check "-ssa" is set? check on -super option?

  // create ABC1_..._t file for every 50 year results
  strcpy(tfilename,mprefix);
  strcat(tfilename,"_t");
  ierr = PetscViewerASCIIOpen(grid.com, tfilename, &tviewfile); CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(tviewfile, PETSC_VIEWER_ASCII_DEFAULT); CHKERRQ(ierr);
  tviewcreated = PETSC_TRUE;

  return 0;
}


PetscErrorCode IceMISMIPModel::readThkFromSMResult(char* filename) {
  SETERRQ(99,"IceMISMIPModel::readThkFromSMResult() NOT YET IMPLEMENTED");
  // see bootstrap example
  return 0;
}


PetscErrorCode IceMISMIPModel::setMISMIPBed() {
  PetscErrorCode ierr;
  PetscScalar          **b;

  ierr = DAVecGetArray(grid.da2, vbed, &b); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
    
      // NOTE !!!!:   y  REPLACES   x   FOR VIEWING CONVENIENCE!
      const PetscScalar jfrom0 =
               static_cast<PetscScalar>(j)-static_cast<PetscScalar>(grid.My - 1)/2.0;
      const PetscScalar y = grid.dy * jfrom0;
      const PetscScalar xs = PetscAbs(y) / 750.0e3;  // scaled and symmetrical x coord

      if ((exper == 1) || (exper == 2)) {
        b[i][j] = 720.0 - 778.5 * xs;
      } else if (exper == 3) {
        const PetscScalar xs2 = xs * xs,
                          xs4 = xs2 * xs2,
                          xs6 = xs4 * xs2;
        b[i][j] = 729.0 - 2184.0 * xs2 + 1031.72 * xs4 - 151.72 * xs6;
      } else {
        SETERRQ(99,"how did I get here?");
      }

    }
  }
  ierr = DAVecRestoreArray(grid.da2, vbed, &b); CHKERRQ(ierr);

  // communicate b because it will be horizontally differentiated
  ierr = DALocalToLocalBegin(grid.da2, vbed, INSERT_VALUES, vbed); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vbed, INSERT_VALUES, vbed); CHKERRQ(ierr);
  return 0;
}



PetscErrorCode IceMISMIPModel::setMISMIPMask() {
  PetscErrorCode ierr;
  PetscScalar    **mask;

  const PetscScalar MISMIP_calving_front = 1600.0e3;

  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
    
      // NOTE !!!!:   y  REPLACES   x   FOR VIEWING CONVENIENCE!
      const PetscScalar jfrom0 =
               static_cast<PetscScalar>(j)-static_cast<PetscScalar>(grid.My - 1)/2.0;
      const PetscScalar y = grid.dy * jfrom0;
      if (PetscAbs(y) >= MISMIP_calving_front) {
        mask[i][j] = MASK_FLOATING_OCEAN0;
      } else {
        // note updateSurfaceElevationAndMask() will mark DRAGGING as FLOATING if it is floating
        mask[i][j] = MASK_DRAGGING;
      }

    }
  }
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);

  // communicate it
  ierr = DALocalToLocalBegin(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vMask, INSERT_VALUES, vMask); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceMISMIPModel::additionalAtStartTimestep() {
  // this is called at the beginning of each pass through time-stepping loop in IceModel::run()

  // go to next multiple of 50 years
  const PetscScalar tonext50 = (50.0 - fmod(grid.year, 50.0)) * secpera;
  if (maxdt_temporary < 0.0) {  // it has not been set
    maxdt_temporary = tonext50;
  } else {
    maxdt_temporary = PetscMin(maxdt_temporary, tonext50);
  }
  return 0;
}


PetscErrorCode IceMISMIPModel::additionalAtEndTimestep() {
  // this is called at the end of each pass through time-stepping loop in IceModel::run()

  PetscErrorCode  ierr;

  PetscScalar     infnormdHdt;
  ierr = VecNorm(vdHdt,NORM_INFINITY,&infnormdHdt); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&infnormdHdt, &rstats.dHdtnorm, grid.com); CHKERRQ(ierr);

//FIXME:  We need to write out the _ss, _f, _extras stuff even if steady state is not
//  Perhaps this will require an endOfRunHook()
//  if ((rstats.dHdtnorm * secpera < dHdtnorm_atol)  // if all points have dHdt < 10^-4 m/yr,
//      || (PetscAbs(endYear - grid.year) < 1.0e-4)) { // or if we are at the end of the run anyway

  if (rstats.dHdtnorm * secpera < dHdtnorm_atol) {  // if all points have dHdt < 10^-4 m/yr
    steadyOrGoalAchieved = PETSC_TRUE;
    // set the IceModel goal of endYear to the current year; causes immediate stop
    endYear = grid.year;  
    // report stopping to standard out
    ierr = verbPrintf(2,grid.com,
        "\nIceMISMIPModel: MISMIP steady state criterion (max|dH/dt| < %.2e m/yr) satisfied;\n"
        "                stopping at year=%.3f\n",dHdtnorm_atol,grid.year); CHKERRQ(ierr);
    // leave stopping stamp in output NetCDF file
    char str[HISTORY_STRING_LENGTH];
    snprintf(str, sizeof(str), 
       "MISMIP steady state criterion (max|dHdt| < %.2e m/yr) satisfied.\n"
       "Stopping.  Completed timestep year=%.3f.",dHdtnorm_atol,grid.year);
    stampHistory(str); 
  }
  return 0;
}


PetscErrorCode IceMISMIPModel::writeMISMIPFinalFiles() {
  PetscErrorCode ierr;
  //ierr = verbPrintf(1,grid.com,
  //     "\nENTERING writeMISMIPFinalFiles() WITH startYear=%f,runtimeyears=%f,grid.year=%f\n",
  //     startYear,runtimeyears,grid.year); CHKERRQ(ierr);
  if (PetscAbs(startYear + runtimeyears - grid.year) < 1.0e-4) {
    steadyOrGoalAchieved = PETSC_TRUE;
  }
  if (steadyOrGoalAchieved == PETSC_TRUE) {
    // get stats in preparation for writing final files
    ierr = getRoutineStats(); CHKERRQ(ierr);
    ierr = getMISMIPStats(); CHKERRQ(ierr);
    // write ASCII file ABC1_1b_M1_A1_ss and ABC1_1b_M1_A1_f;
    char  ssfilename[PETSC_MAX_PATH_LEN], ffilename[PETSC_MAX_PATH_LEN];
    strcpy(ssfilename,mprefix);
    strcat(ssfilename,"_ss");    
    strcpy(ffilename,mprefix);
    strcat(ffilename,"_f");    
    ierr = verbPrintf(2, grid.com, 
            "\nIceMISMIPModel:  steady state achieved or specified run time completed.\n"
              "                 writing files %s and %s",
            ssfilename, ffilename); CHKERRQ(ierr);
    ierr = writeMISMIPasciiFile('s',ssfilename); CHKERRQ(ierr);
    ierr = writeMISMIPasciiFile('f',ffilename); CHKERRQ(ierr);
    // optionally write ABC1_1b_M1_A1_ss
    if (writeExtras == PETSC_TRUE) {
      char  efilename[PETSC_MAX_PATH_LEN];
      strcpy(efilename,mprefix);
      strcat(efilename,"_extras");    
      ierr = verbPrintf(2, grid.com, " and %s", efilename); CHKERRQ(ierr);
      ierr = writeMISMIPasciiFile('e',efilename); CHKERRQ(ierr);
    }
  }
  return 0;
}


PetscErrorCode IceMISMIPModel::writeMISMIPasciiFile(const char mismiptype, char* filename) {
  PetscErrorCode ierr;
  PetscViewer  view;
  ierr = PetscViewerASCIIOpen(grid.com, filename, &view); CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(view, PETSC_VIEWER_ASCII_DEFAULT); CHKERRQ(ierr);
  // just get all Vecs which might be needed
  PetscScalar     **H, **h, **bed;
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vh, &h); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbed, &bed); CHKERRQ(ierr);
  if (mismiptype == 'f') {
    ierr = PetscViewerASCIIPrintf(view,"%10.4f %10.2f\n", rstats.xg / 1000.0, grid.year);
               CHKERRQ(ierr);
  } else {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscScalar
        jfrom0 = static_cast<PetscScalar>(j)
                                 - static_cast<PetscScalar>(grid.My - 1)/2.0,
        y = grid.dy * jfrom0;
      if (y >= 0) {
        if (mismiptype == 's') {
          ierr = PetscViewerASCIISynchronizedPrintf(view,
               "%10.2f %10.4f\n", y / 1000.0, H[grid.xs][j]); CHKERRQ(ierr);
        } else { // mismiptype == 'e'
          ierr = PetscViewerASCIISynchronizedPrintf(view,
                 "%10.4f %10.4f\n", h[grid.xs][j], bed[grid.xs][j]); CHKERRQ(ierr);
        }
      } else { // write empty string to make sure all processors write;
               // perhaps it is a bug in PETSc that this seems to be necessary?
        ierr = PetscViewerASCIISynchronizedPrintf(view,""); CHKERRQ(ierr);
      }
    }
  }
  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vh, &h); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vbed, &bed); CHKERRQ(ierr);
  ierr = PetscViewerFlush(view); CHKERRQ(ierr);
  ierr = PetscViewerDestroy(view); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode IceMISMIPModel::getMISMIPStats() {
  // run this only after getRoutineStats() is called
  
  PetscErrorCode  ierr;
  PetscScalar     **H, **b, **q;
  
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbed, &b); CHKERRQ(ierr);

  ierr = VecPointwiseMult(vWork2d[0], vvbar, vH); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vWork2d[0], &q); CHKERRQ(ierr);
  // q[i][j] is signed flux in y direction, in units of m^2/s
  
  mstats.x1 = rstats.xg;
  mstats.x2 = rstats.xg - grid.dy;
  mstats.x3 = rstats.xg + grid.dy;
  
  PetscScalar myh2 = 0.0, myh3 = 0.0, myb1 = -1e6, myb2 = -1e6, myb3 = -1e6, 
              myq1 = -1e20, myq2 = -1e20, myq3 = -1e20;
  const int jg = (int)floor(rstats.jg + 0.1);

  mstats.h1 = rstats.hxg;  // already computed
  if ( (jg >= grid.ys) && (jg < grid.ys + grid.ym)
       && (grid.xs == 0)                             ) {  // if (0,jg) is in ownership
    myb1 = b[0][jg];
    myq1 = q[0][jg];
  }
  ierr = PetscGlobalMax(&myb1, &mstats.b1, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&myq1, &mstats.q1, grid.com); CHKERRQ(ierr);

  if ( (jg-1 >= grid.ys) && (jg-1 < grid.ys + grid.ym)
       && (grid.xs == 0)                             ) {  // if (0,jg-1) is in ownership
    myh2 = H[0][jg-1];
    myb2 = b[0][jg-1];
    myq2 = q[0][jg-1];
  }
  ierr = PetscGlobalMax(&myh2, &mstats.h2, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&myb2, &mstats.b2, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&myq2, &mstats.q2, grid.com); CHKERRQ(ierr);

  if ( (jg+1 >= grid.ys) && (jg+1 < grid.ys + grid.ym)
       && (grid.xs == 0)                             ) {  // if (0,jg+1) is in ownership
    myh3 = H[0][jg+1];
    myb3 = b[0][jg+1];
    myq3 = q[0][jg+1];
  }
  ierr = PetscGlobalMax(&myh3, &mstats.h3, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&myb3, &mstats.b3, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalMax(&myq3, &mstats.q3, grid.com); CHKERRQ(ierr);

  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vbed, &b); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vWork2d[0], &q); CHKERRQ(ierr);

  // perform MISMIP diagnostic computation here, to estimate dxg/dt:
  //   d xg            a - dq/dx
  //   ---- = -----------------------------
  //    dt     dh/dx - (rhow/rhoi) (db/dx)
  const PetscScalar dqdx = (mstats.q1 - mstats.q2) / (mstats.x1 - mstats.x2),
                    dhdx = (mstats.h1 - mstats.h2) / (mstats.x1 - mstats.x2),
                    dbdx = (mstats.b1 - mstats.b2) / (mstats.x1 - mstats.x2);
  mstats.dxgdt = ((0.3/secpera) - dqdx) / (dhdx - (ocean.rho/mismip_ice->rho) * dbdx);  
  return 0;
}


PetscErrorCode IceMISMIPModel::getRoutineStats() {
  PetscErrorCode  ierr;

  PetscScalar     **mask, **H, **vbar;

  // these are in MKS; sans "g" are local to the processor; with "g" are global 
  //   across all processors; we only evaluate for x > 0
  PetscScalar     maxubar = 0.0, avubargrounded = 0.0, avubarfloating = 0.0, jg = 0.0,
                  Ngrounded = 0.0, Nfloating = 0.0;
  PetscScalar     gavubargrounded, gavubarfloating, gjg,
                  gNgrounded, gNfloating;

  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vvbar, &vbar); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {

      const PetscScalar jfrom0 =
               static_cast<PetscScalar>(j)-static_cast<PetscScalar>(grid.My - 1)/2.0;

      // grounding line xg is largest  y  so that  mask[i][j] != FLOATING
      //       and mask[i][j+1] == FLOATING
      if ( (jfrom0 > 0.0) && (H[i][j] > 0.0) 
           && (modMask(mask[i][j]) != MASK_FLOATING) 
           && (modMask(mask[i][j+1]) == MASK_FLOATING) ) {
        // NOTE !!!!:   y  REPLACES   x   FOR VIEWING CONVENIENCE!
        jg = PetscMax(jg,static_cast<PetscScalar>(j));
      }

      if ((jfrom0 > 0) && (H[i][j] > 0.0)) {
        // NOTE !!!!:   y  REPLACES   x   FOR VIEWING CONVENIENCE!
        if (vbar[i][j] > maxubar)  maxubar = vbar[i][j];
        if (modMask(mask[i][j]) != MASK_FLOATING) {
          Ngrounded += 1.0;
          avubargrounded += vbar[i][j];
        } else {
          Nfloating += 1.0;
          avubarfloating += vbar[i][j];        
        }
      }

    }
  }
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vvbar, &vbar); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&jg, &gjg, grid.com); CHKERRQ(ierr);
  rstats.jg = gjg;
  
  const PetscScalar gjgfrom0 =
          gjg - static_cast<PetscScalar>(grid.My - 1)/2.0;
  rstats.xg = gjgfrom0 * grid.dy;
  
  PetscScalar myhxg = 0.0;
  if ( (gjg >= grid.ys) && (gjg < grid.ys + grid.ym)
       && (grid.xs == 0)                             ) {  // if (0,gjg) is in ownership
    myhxg = H[0][static_cast<int>(gjg)]; // i.e. hxg = H[0][gjg]
  } else {
    myhxg = 0.0;
  } 
  ierr = PetscGlobalMax(&myhxg, &rstats.hxg, grid.com); CHKERRQ(ierr);

  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);

  ierr = PetscGlobalMax(&maxubar, &rstats.maxubar, grid.com); CHKERRQ(ierr);
    
  ierr = PetscGlobalSum(&Ngrounded, &gNgrounded, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avubargrounded, &gavubargrounded, grid.com); CHKERRQ(ierr);
  if (gNgrounded > 0)   gavubargrounded = gavubargrounded / gNgrounded;
  else                  gavubargrounded = 0.0;  // degenerate case
  rstats.avubarG = gavubargrounded;

  ierr = PetscGlobalSum(&Nfloating, &gNfloating, grid.com); CHKERRQ(ierr);
  ierr = PetscGlobalSum(&avubarfloating, &gavubarfloating, grid.com); CHKERRQ(ierr);
  if (gNfloating > 0)   gavubarfloating = gavubarfloating / gNfloating;
  else                  gavubarfloating = 0.0;  // degenerate case
  rstats.avubarF = gavubarfloating;

  // rstats.dHdtnorm already calculated in additionalAtEndTimestep()
  return 0;
}


PetscErrorCode IceMISMIPModel::summaryPrintLine(
    const PetscTruth printPrototype, const PetscTruth tempAndAge,
    const PetscScalar year, const PetscScalar dt, 
    const PetscScalar volume_kmcube, const PetscScalar area_kmsquare,
    const PetscScalar meltfrac, const PetscScalar H0, const PetscScalar T0) {

/*
Because this model resolves the shelf and only uses the floatation criterion
to move the grounding line, we must give 17 numbers.  These numbers will go into
a reportable ascii file ABC1_1a_M1_A1_t.

The reported numbers are

   t  x_g  Volume  h(0,t)  h(x_g,t)
     x_1 h(x_1,t) b(x_1) q(x_1,t)       // last grounded point (i.e. x_1 = x_g)
     x_2 h(x_2,t) b(x_2) q(x_2,t)       // x_2 = x_1 - dx
     x_3 h(x_3,t) b(x_3) q(x_3,t)       // x_3 = x_1 + dx

The number of reported digits is

     8 chars (includes ".") for t [years]
     7 chars for x_*       [km]
     8 chars for Volume    [10^6 km^3]
     7 chars for h(*,t)    [m]
     7 chars for b(*,t)    [m]
     7 chars for q(*,t)    [m^2/year]

The format written to ABC1_1a_M1_A1_t has 137 columns, like this:

######## ####### ######## ####### ####### ####### ####### ####### ####### ####### ####### ####### ####### ####### ####### ####### #######

At verbosity 3 or higher, an additional stdout format fits in an 80 column line:

M  ######## ####### ######## ####### #######
   ####### ####### ####### ####### ####### ####### ####### #######
   ####### ####### ####### #######

A line
   [ d(xg)/dt = ####### m/yr by MISMIP computation ]
is written to stdout.  This grounding line motion rate
is computed as in MISMIP description, and finite differences.
*/

  PetscErrorCode ierr;
  if (printPrototype == PETSC_TRUE) {
    ierr = verbPrintf(2,grid.com,
      "P         YEAR:     ivol      h0      xg     hxg maxubar avubarG avubarF dHdtnorm\n");
    ierr = verbPrintf(2,grid.com,
      "U        years 10^6_km^3       m      km       m     m/a     m/a     m/a      m/a\n");
  } else {
    ierr = getRoutineStats(); CHKERRQ(ierr);
    ierr = verbPrintf(2,grid.com,
      "S %12.5f: %8.5f %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f %8.2e\n",
      year, volume_kmcube/1.0e6, 
      H0, rstats.xg / 1000.0, rstats.hxg, rstats.maxubar * secpera, 
      rstats.avubarG * secpera, rstats.avubarF * secpera,
      rstats.dHdtnorm * secpera); CHKERRQ(ierr);
    if (fabs(fmod(year, 50.0)) < 1.0e-6) {
      // write another line to ASCII file ABC1_1b_M1_A1_t; also write to stdout
      //   (given some verbosity level); note modest code redundancy
      ierr = verbPrintf(2, grid.com, 
             "[IceMISMIPModel:  adding t=%10.3f line to file %s;\n",
             year,tfilename); CHKERRQ(ierr);
      ierr = getMISMIPStats(); CHKERRQ(ierr);
      ierr = verbPrintf(3,grid.com,"M  ");
      ierr = verbPrintf(3,grid.com,
        "%8.2f %7.2f %8.5f %7.2f %7.2f ",
        year, rstats.xg / 1000.0, volume_kmcube/1.0e6, H0, rstats.hxg); CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(tviewfile,
        "%8.2f %7.2f %8.5f %7.2f %7.2f ",
        year, rstats.xg / 1000.0, volume_kmcube/1.0e6, H0, rstats.hxg); CHKERRQ(ierr);
      ierr = verbPrintf(3,grid.com,"\n   ");
      ierr = verbPrintf(3,grid.com,
        "%7.2f %7.2f %7.2f %7.0f %7.2f %7.2f %7.2f %7.0f ",
        mstats.x1 / 1000.0, mstats.h1, mstats.b1, mstats.q1 * secpera,
        mstats.x2 / 1000.0, mstats.h2, mstats.b2, mstats.q2 * secpera); CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(tviewfile,
        "%7.2f %7.2f %7.2f %7.0f %7.2f %7.2f %7.2f %7.0f ",
        mstats.x1 / 1000.0, mstats.h1, mstats.b1, mstats.q1 * secpera,
        mstats.x2 / 1000.0, mstats.h2, mstats.b2, mstats.q2 * secpera); CHKERRQ(ierr);
      ierr = verbPrintf(3,grid.com,"\n   ");
      ierr = verbPrintf(3,grid.com,
        "%7.2f %7.2f %7.2f %7.0f\n",
        mstats.x3 / 1000.0, mstats.h3, mstats.b3, mstats.q3 * secpera); CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(tviewfile,
        "%7.2f %7.2f %7.2f %7.0f\n",
        mstats.x3 / 1000.0, mstats.h3, mstats.b3, mstats.q3 * secpera); CHKERRQ(ierr);
      ierr = verbPrintf(2,grid.com,
        "   d(xg)/dt = %10.2f m/yr by MISMIP computation ]\n",
        mstats.dxgdt * secpera); CHKERRQ(ierr);
      
    }
  }
  return 0;
}

