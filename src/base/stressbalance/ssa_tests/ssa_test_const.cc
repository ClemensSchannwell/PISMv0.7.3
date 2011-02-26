// Copyright (C) 2010--2011 Ed Bueler, Constantine Khroulev, and David Maxwell
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

/* This file implements a test case for the ssa:  constant flow.  The rheology is nonlinear (i.e. n=3 in the Glen flow law)
   and the basal shear stress is a nonlinear function of velocity (peseudo-plastic flow with parameter q specified at runtime).
   The geometry consists of a constant surface slope in the positive x-direction, and a constant velocity is specified as a Dirichlet
   condition on the boundary that should lead to a constant solution in the interior.  Because the solution is constant, the nonzero
   terms in the SSA are only the basal shear stress and the driving stress.
*/


static char help[] =
  "\nSSA_TEST_CONST\n"
  "  Testing program for the finite element implementation of the SSA.\n"
  "  Does a time-independent calculation.  Does not run IceModel or a derived\n"
  "  class thereof.Also may be used in a PISM\n"
  "  software (regression) test.\n\n";

#include "pism_const.hh"
#include "iceModelVec.hh"
#include "flowlaws.hh" // IceFlowLaw
#include "materials.hh" // IceBasalResistancePlasticLaw
#include "PISMIO.hh"
#include "NCVariable.hh"
#include "SSAFEM.hh"
#include "SSAFD.hh"
#include "exactTestsIJ.h"
#include "SSATestCase.hh"
#include <math.h>
class SSATestCaseConst: public SSATestCase
{
public:
  SSATestCaseConst( MPI_Comm com, PetscMPIInt rank, 
                 PetscMPIInt size, NCConfigVariable &config, PetscScalar q ): 
                 SSATestCase(com,rank,size,config), basal_q(q)
  { };
  
protected:
  virtual PetscErrorCode initializeGrid(PetscInt Mx,PetscInt My);

  virtual PetscErrorCode initializeSSAModel();

  virtual PetscErrorCode initializeSSACoefficients();

  virtual PetscErrorCode exactSolution(PetscInt i, PetscInt j, 
    PetscReal x, PetscReal y, PetscReal *u, PetscReal *v );

  PetscScalar basal_q;
};

const PetscScalar L=50.e3; // 50km half-width
const PetscScalar H0=500; // m
const PetscScalar dhdx = 0.005; // pure number, slope of surface & bed
const PetscScalar nu0 = 30.0 * 1.0e6 * secpera; /* = 9.45e14 Pa s */
const PetscScalar tauc0 = 1.e4; // Pa


PetscErrorCode SSATestCaseConst::initializeGrid(PetscInt Mx,PetscInt My)
{
  PetscReal Lx=L, Ly = L; 
  init_shallow_grid(grid,Lx,Ly,Mx,My,NONE);
}


PetscErrorCode SSATestCaseConst::initializeSSAModel()
{
  // Use a pseudo-plastic law with a constant q determined at run time
  basal = new IceBasalResistancePlasticLaw(
         config.get("plastic_regularization") / secpera,
         false, // do not force a pure-plastic law
         basal_q,
         config.get("pseudo_plastic_uthreshold") / secpera);

  // The following is irrelevant because we will force linear rheology later.
  ice = new CustomGlenIce(grid.com, "", config);

  // I believe this is irrelevant as well.
  enthalpyconverter = new EnthalpyConverter(config);
  return 0;
}

PetscErrorCode SSATestCaseConst::initializeSSACoefficients()
{
  PetscErrorCode ierr;
  PetscScalar    **ph, **pbed;
  
  // Force linear rheology
  ssa->strength_extension->set_notional_strength(nu0 * H0);
  ssa->strength_extension->set_min_thickness(4000*10);

  // The finite difference code uses the following flag to treat the non-periodic grid correctly.
  config.set_flag("compute_surf_grad_inward_ssa", true);

  // Set constant thickness, tauc
  ierr = mask.set(MASK_DRAGGING_SHEET); CHKERRQ(ierr);
  ierr = thickness.set(H0); CHKERRQ(ierr);
  ierr = tauc.set(tauc0); CHKERRQ(ierr);
  


  ierr = vel_bc.begin_access(); CHKERRQ(ierr);
  ierr = mask.begin_access(); CHKERRQ(ierr);
  ierr = bed.begin_access(); CHKERRQ(ierr);
  ierr = surface.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      PetscScalar junk, myu, myv;
      const PetscScalar myx = grid.x[i], myy=grid.y[j];

      bed(i,j) = -myx*(dhdx);
      surface(i,j) = bed(i,j) + H0;
      
      bool edge = ( (j == 0) || (j == grid.My - 1) ) || ( (i==0) || (i==grid.Mx-1) );
      if (edge) {
        mask(i,j) = MASK_SHEET;
        exactSolution(i,j,myx,myy,&myu,&myv);
        vel_bc(i,j).u = myu;
        vel_bc(i,j).v = myv;
      }
    }
  } 
  ierr = vel_bc.end_access(); CHKERRQ(ierr);
  ierr = mask.end_access(); CHKERRQ(ierr);
  ierr = bed.end_access(); CHKERRQ(ierr);
  ierr = surface.end_access(); CHKERRQ(ierr);
  
  
  ierr = vel_bc.beginGhostComm(); CHKERRQ(ierr);
  ierr = vel_bc.endGhostComm(); CHKERRQ(ierr);
  ierr = mask.beginGhostComm(); CHKERRQ(ierr);
  ierr = mask.endGhostComm(); CHKERRQ(ierr);
  ierr = bed.beginGhostComm(); CHKERRQ(ierr);
  ierr = bed.endGhostComm(); CHKERRQ(ierr);  
  ierr = surface.beginGhostComm(); CHKERRQ(ierr);
  ierr = surface.endGhostComm(); CHKERRQ(ierr);  

  ierr = ssa->set_boundary_conditions(mask, vel_bc); CHKERRQ(ierr); 

  return 0;
}


PetscErrorCode SSATestCaseConst::exactSolution(PetscInt i, PetscInt j, 
 PetscReal x, PetscReal y, PetscReal *u, PetscReal *v)
{
  PetscScalar earth_grav = config.get("standard_gravity");
  PetscScalar tauc_threshold_velocity = config.get("pseudo_plastic_uthreshold") / secpera;
  
  *u = pow(ice->rho * earth_grav * H0 * dhdx / tauc0, 1./basal_q)*tauc_threshold_velocity;
  *v = 0;
  
}


int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;

  MPI_Comm    com;  // won't be used except for rank,size
  PetscMPIInt rank, size;

  ierr = PetscInitialize(&argc, &argv, PETSC_NULL, help); CHKERRQ(ierr);

  com = PETSC_COMM_WORLD;
  ierr = MPI_Comm_rank(com, &rank); CHKERRQ(ierr);
  ierr = MPI_Comm_size(com, &size); CHKERRQ(ierr);
  
  /* This explicit scoping forces destructors to be called before PetscFinalize() */
  {  
    NCConfigVariable config, overrides;
    ierr = init_config(com, rank, config, overrides); CHKERRQ(ierr);

    ierr = setVerbosityLevel(5); CHKERRQ(ierr);

    PetscTruth usage_set, help_set;
    ierr = PetscOptionsHasName(PETSC_NULL, "-usage", &usage_set); CHKERRQ(ierr);
    ierr = PetscOptionsHasName(PETSC_NULL, "-help", &help_set); CHKERRQ(ierr);
    if ((usage_set==PETSC_TRUE) || (help_set==PETSC_TRUE)) {
      PetscPrintf(com,
                  "\n"
                  "usage of SSA_TESTJ:\n"
                  "  run ssafe_test -Mx <number> -My <number> -ssa <fd|fem>\n"
                  "\n");
    }

    // Parameters that can be overridden by command line options
    PetscInt Mx=61;
    PetscInt My=61;
    PetscScalar basal_q = 1.; // linear
    string output_file = "ssa_test_const.nc";
    string driver = "fem";

    ierr = PetscOptionsBegin(com, "", "SSAFD_TEST options", ""); CHKERRQ(ierr);
    {
      bool flag;
      ierr = PISMOptionsInt("-Mx", "Number of grid points in the X direction", 
                                                      Mx, flag); CHKERRQ(ierr);
      ierr = PISMOptionsInt("-My", "Number of grid points in the Y direction", 
                                                      My, flag); CHKERRQ(ierr);
      ierr = PISMOptionsString("-ssa_method", "Algorithm for computing the SSA solution",
                                                  driver, flag); CHKERRQ(ierr);                                                      
      ierr = PISMOptionsString("-ssa_basal_q", "Exponent q in the pseudo-plastic flow law",
                                                  driver, flag); CHKERRQ(ierr);                                                      
      ierr = PISMOptionsString("-o", "Set the output file name", 
                                              output_file, flag); CHKERRQ(ierr);
    }
    ierr = PetscOptionsEnd(); CHKERRQ(ierr);

    // Determine the kind of solver to use.
    SSAFactory ssafactory;
    if(driver.compare("fem") == 0) ssafactory = SSAFEMFactory;
    else if(driver.compare("fd") == 0) ssafactory = SSAFDFactory;
    else SETERRQ(1,"SSA algorithm argument should be one of -ssa fe or -ssa fem");

    SSATestCaseConst testcase(com,rank,size,config,basal_q);
    ierr = testcase.init(Mx,My,ssafactory); CHKERRQ(ierr);
    ierr = testcase.run(); CHKERRQ(ierr);
    ierr = testcase.report(); CHKERRQ(ierr);
    ierr = testcase.write(output_file); CHKERRQ(ierr);
  }

  ierr = PetscFinalize(); CHKERRQ(ierr);
  return 0;
}
