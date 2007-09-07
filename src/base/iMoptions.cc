// Copyright (C) 2004-2007 Jed Brown and Ed Bueler
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
#include <cmath>
#include "iceModel.hh"

//! Read runtime (command line) options and set the corresponding parameter or flag.
/*!
This is called by a driver program, assuming it would like to use command line options.

In fact this procedure only reads the majority of the options.  Some are read in 
initFromOptions(), writeFiles(), and setStartRunEndYearsFromOptions(), among other places.

Note there are no options to directly set \c dx, \c dy, \c dz, \c Lbz, and \c year as the user 
should not directly set these grid parameters.  There are, however, options for directly 
setting \c Mx, \c My, \c Mz, \c Mbz and also \c Lx, \c Ly, \c Lz.
 */
PetscErrorCode  IceModel::setFromOptions() {
  PetscErrorCode ierr;
  PetscTruth  my_useConstantNu, MxSet, MySet, MzSet, MbzSet,
              maxdtSet, my_useConstantHardness, noMassConserve, noTemp, bedDeflc; 
  PetscScalar my_maxdt, my_nu, regVelSchoof, my_barB;
  PetscInt    my_Mx, my_My, my_Mz, my_Mbz;

  // OptionsBegin/End probably has no effect for now, but perhaps some day PETSc will show a GUI which
  // allows users to set options using this.
  ierr = PetscOptionsBegin(grid.com,PETSC_NULL,"IceModel options (in PISM)",PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-adapt_ratio", &adaptTimeStepRatio,
                               PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsHasName(PETSC_NULL, "-bed_def_lc", &bedDeflc); CHKERRQ(ierr);  
  ierr = PetscOptionsHasName(PETSC_NULL, "-bed_def_iso", &doBedIso); CHKERRQ(ierr);
  if ((doBedIso == PETSC_TRUE) || (bedDeflc == PETSC_TRUE))    doBedDef = PETSC_TRUE;

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-constant_nu", &my_nu, &my_useConstantNu); CHKERRQ(ierr);
  // user gives nu in MPa yr (e.g. Ritz value is 30.0)
  if (my_useConstantNu == PETSC_TRUE)    setConstantNuForSSA(my_nu  * 1.0e6 * secpera);

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-constant_hardness", &my_barB, &my_useConstantHardness);
           CHKERRQ(ierr);
  // user gives \bar B in 
  if (my_useConstantHardness == PETSC_TRUE) {
    useConstantHardnessForSSA = PETSC_TRUE;
    constantHardnessForSSA = my_barB;
  }

  // regular size viewers
  ierr = PetscOptionsGetString(PETSC_NULL, "-d", diagnostic, PETSC_MAX_PATH_LEN, PETSC_NULL); 
            CHKERRQ(ierr);
  if (showViewers == PETSC_FALSE)    strcpy(diagnostic, "\0");

  // big viewers (which will override regular viewers)
  ierr = PetscOptionsGetString(PETSC_NULL, "-dbig", diagnosticBIG, PETSC_MAX_PATH_LEN, PETSC_NULL); 
            CHKERRQ(ierr);
  if (showViewers == PETSC_FALSE)    strcpy(diagnosticBIG, "\0");

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-e", &enhancementFactor, PETSC_NULL); CHKERRQ(ierr);

// note "-gk" is in use for specifying Goldsby-Kohlstedt ice

// note "-id" is in use for sounding location

// note "-if" is in use for input file name

  // This switch turns off vertical integration in the isothermal case.  That
  // is, the horizontal flux of ice is computed as an analytical function of the
  // thickness and the surface slope.  The Glen power n=3 and a fixed softness
  // parameter A = 10^{-16} Pa^{-3} a^{-1} are used.  These are set in
  // IceModel::setDefaults().
  ierr = PetscOptionsHasName(PETSC_NULL, "-isoflux", &useIsothermalFlux); CHKERRQ(ierr);

// note "-jd" is in use for sounding location

// note "-kd" is in use for horizontal slicing (in viewers and dumpToFileMatlab)

// note -Lx, -Ly, -Lz are all checked in [iMutil.cc]IceModel::afterInitHook()

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-maxdt", &my_maxdt, &maxdtSet); CHKERRQ(ierr);
  if (maxdtSet == PETSC_TRUE)    setMaxTimeStepYears(my_maxdt);

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-mu_sliding", &muSliding, PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetInt(PETSC_NULL, "-Mx", &my_Mx, &MxSet); CHKERRQ(ierr);
  if (MxSet == PETSC_TRUE)   grid.p->Mx = my_Mx;

  ierr = PetscOptionsGetInt(PETSC_NULL, "-My", &my_My, &MySet); CHKERRQ(ierr);
  if (MySet == PETSC_TRUE)   grid.p->My = my_My;

  ierr = PetscOptionsGetInt(PETSC_NULL, "-Mz", &my_Mz, &MzSet); CHKERRQ(ierr);
  if (MzSet == PETSC_TRUE)   grid.p->Mz = my_Mz;

  ierr = PetscOptionsGetInt(PETSC_NULL, "-Mbz", &my_Mbz, &MbzSet); CHKERRQ(ierr);
  if (MbzSet == PETSC_TRUE)   grid.p->Mbz = my_Mbz;

  ierr = PetscOptionsHasName(PETSC_NULL, "-no_bmr_in_vert", &includeBMRinContinuity); CHKERRQ(ierr);

  ierr = PetscOptionsHasName(PETSC_NULL, "-no_mass", &noMassConserve); CHKERRQ(ierr);
  if (noMassConserve == PETSC_TRUE)    doMassConserve = PETSC_FALSE;

  // -no_spokes K for K=0,1,2,... turns on smoothing of spokes by smoothing Sigma 
  // (e.g. in EISMINT experiment F) values K>3 not recommended (lots of communication!)
  ierr = PetscOptionsGetInt(PETSC_NULL, "-no_spokes", &noSpokesLevel, PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsHasName(PETSC_NULL, "-no_temp", &noTemp); CHKERRQ(ierr);
  if (noTemp == PETSC_TRUE)   doTemp = PETSC_FALSE;

// note "-o" is in use for output file name

  // whether or not to kill ice if original condition was ice-free ocean
  ierr = PetscOptionsHasName(PETSC_NULL, "-ocean_kill", &doOceanKill); CHKERRQ(ierr);

// note "-of" is in use for output file format; see iMIO.cc

  // use a plastic basal till mechanical model
  ierr = PetscOptionsHasName(PETSC_NULL, "-plastic", &doPlasticTill); CHKERRQ(ierr);

  PetscTruth regVelSet;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-reg_vel_schoof", &regVelSchoof, &regVelSet);
           CHKERRQ(ierr);
  if (regVelSet == PETSC_TRUE)   regularizingVelocitySchoof = regVelSchoof/secpera;
    
  
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-reg_length_schoof", &regularizingLengthSchoof,
           PETSC_NULL); CHKERRQ(ierr);
  
// note "-regrid" is in use for regrid file name; see iMregrid.cc

// note "-regrid_vars" is in use for regrid variable names; see iMregrid.cc

  ierr = PetscOptionsHasName(PETSC_NULL, "-ssa", &useSSAVelocity); CHKERRQ(ierr);
  
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-ssa_eps", &ssaEpsilon, PETSC_NULL); CHKERRQ(ierr);
  
  // option to save linear system in Matlab-readable ASCII format at end of each
  // numerical solution of SSA equations; can be given with or without filename prefix
  // (i.e. "-ssa_matlab " or "-ssa_matlab foo" are both legal; in former case get 
  // "pism_SSA_[year].m" if "pism_SSA" is default prefix, and in latter case get "foo_[year].m")
  ierr = PetscOptionsHasName(PETSC_NULL, "-ssa_matlab", &ssaSystemToASCIIMatlab); CHKERRQ(ierr);
  if (ssaSystemToASCIIMatlab == PETSC_TRUE) {
    char tempPrefix[PETSC_MAX_PATH_LEN];
    ierr = PetscOptionsGetString(PETSC_NULL, "-ssa_matlab", tempPrefix, 
             PETSC_MAX_PATH_LEN, PETSC_NULL); CHKERRQ(ierr);
    if (strlen(tempPrefix) > 0) {
      strcpy(ssaMatlabFilePrefix, tempPrefix);
    } // otherwise keep default prefix, whatever it was
  }

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-ssa_rtol", &ssaRelativeTolerance,
           PETSC_NULL); CHKERRQ(ierr);
  
  // apply "glaciological superposition to low order", i.e. add SIA results to those of 
  // SSA equations where DRAGGING
  ierr = PetscOptionsHasName(PETSC_NULL, "-super", &doSuperpose); CHKERRQ(ierr);

  /* This controls allows more than one mass continuity steps per temperature/age step */
  ierr = PetscOptionsGetInt(PETSC_NULL, "-tempskip", &tempskipMax, &doTempSkip); CHKERRQ(ierr);

  // till pw_fraction, till cohesion, and till friction angle are only relevant in
  //   IceModel::updateYieldStressFromHmelt()
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-till_pw_fraction", &plastic_till_pw_fraction,
           PETSC_NULL); CHKERRQ(ierr);

  ierr = PetscOptionsGetScalar(PETSC_NULL, "-till_cohesion", &plastic_till_c_0, PETSC_NULL);
           CHKERRQ(ierr);

  PetscScalar till_theta;
  PetscTruth  till_thetaSet;
  ierr = PetscOptionsGetScalar(PETSC_NULL, "-till_friction_angle", &till_theta, &till_thetaSet);
           CHKERRQ(ierr);
  if (till_thetaSet == PETSC_TRUE)    plastic_till_mu = tan((pi/180.0)*till_theta);

  // verbosity options: more info to standard out.  see iMutil.cc
  ierr = verbosityLevelFromOptions(); CHKERRQ(ierr);

// note -ys, -ye, -y options are read in setStartRunEndYearsFromOptions()
 
  ierr = setSoundingFromOptions(); CHKERRQ(ierr);

  ierr = PetscOptionsEnd(); CHKERRQ(ierr);
  return 0;
}

