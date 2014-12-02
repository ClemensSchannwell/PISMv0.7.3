// Copyright (C) 2010--2014 Ed Bueler, Constantine Khroulev, and David Maxwell
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
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

/* This file implements a test case for the ssa: constant flow. The rheology is
   nonlinear (i.e. n=3 in the Glen flow law) and the basal shear stress is a
   nonlinear function of velocity (peseudo-plastic flow with parameter q
   specified at runtime).

   The geometry consists of a constant surface slope in the positive
   x-direction, and a constant velocity is specified as a Dirichlet condition
   on the boundary that should lead to a constant solution in the interior.
   Because the solution is constant, the nonzero terms in the SSA are only the
   basal shear stress and the driving stress.
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
#include "basal_resistance.hh" // IceBasalResistancePlasticLaw
#include "PIO.hh"
#include "NCVariable.hh"
#include "SSAFEM.hh"
#include "SSAFD.hh"
#include "exactTestsIJ.h"
#include "SSATestCase.hh"
#include <math.h>
#include "pism_options.hh"
#include "Mask.hh"

#include "PetscInitializer.hh"
#include "error_handling.hh"

using namespace pism;

class SSATestCaseConst: public SSATestCase
{
public:
  SSATestCaseConst(MPI_Comm com, Config &c, double q): 
    SSATestCase(com, c), basal_q(q)
  {
    UnitSystem s = c.get_unit_system();

    L     = s.convert(50.0, "km", "m"); // 50km half-width
    H0    = 500;                        // m
    dhdx  = 0.005;                      // pure number
    nu0   = s.convert(30.0, "MPa year", "Pa s");
    tauc0 = 1.e4;               // Pa
  };
  
protected:
  virtual PetscErrorCode initializeGrid(int Mx,int My);

  virtual PetscErrorCode initializeSSAModel();

  virtual PetscErrorCode initializeSSACoefficients();

  virtual PetscErrorCode exactSolution(int i, int j, 
    double x, double y, double *u, double *v);

  double basal_q,
    L, H0, dhdx, nu0, tauc0;
};

PetscErrorCode SSATestCaseConst::initializeGrid(int Mx,int My)
{
  double Lx=L, Ly = L;
  grid = IceGrid::Shallow(m_com, config, Lx, Ly, Mx, My, NOT_PERIODIC);
  return 0;
}


PetscErrorCode SSATestCaseConst::initializeSSAModel()
{
  config.set_flag("do_pseudo_plastic_till", true);
  config.set_double("pseudo_plastic_q", basal_q);

  // Use a pseudo-plastic law with a constant q determined at run time
  config.set_flag("do_pseudo_plastic_till", true);

  // The following is irrelevant because we will force linear rheology later.
  enthalpyconverter = new EnthalpyConverter(config);

  return 0;
}

PetscErrorCode SSATestCaseConst::initializeSSACoefficients()
{

  // Force linear rheology
  ssa->strength_extension->set_notional_strength(nu0 * H0);
  ssa->strength_extension->set_min_thickness(0.5*H0);

  // The finite difference code uses the following flag to treat the non-periodic grid correctly.
  config.set_flag("compute_surf_grad_inward_ssa", true);

  // Set constant thickness, tauc
  bc_mask.set(MASK_GROUNDED);
  thickness.set(H0);
  tauc.set(tauc0);

  IceModelVec::AccessList list;
  list.add(vel_bc);
  list.add(bc_mask);
  list.add(bed);
  list.add(surface);

  for (Points p(*grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double myu, myv;
    const double myx = grid->x[i], myy=grid->y[j];

    bed(i,j) = -myx*(dhdx);
    surface(i,j) = bed(i,j) + H0;
      
    bool edge = ((j == 0) || (j == grid->My - 1)) || ((i==0) || (i==grid->Mx-1));
    if (edge) {
      bc_mask(i,j) = 1;
      exactSolution(i,j,myx,myy,&myu,&myv);
      vel_bc(i,j).u = myu;
      vel_bc(i,j).v = myv;
    }
  }
  
  vel_bc.update_ghosts();
  bc_mask.update_ghosts();
  bed.update_ghosts();
  surface.update_ghosts();

  ssa->set_boundary_conditions(bc_mask, vel_bc); 

  return 0;
}


PetscErrorCode SSATestCaseConst::exactSolution(int /*i*/, int /*j*/, 
 double /*x*/, double /*y*/, double *u, double *v)
{
  double earth_grav = config.get("standard_gravity"),
    tauc_threshold_velocity = config.get("pseudo_plastic_uthreshold",
                                         "m/year", "m/second"),
    ice_rho = config.get("ice_density");
  
  *u = pow(ice_rho * earth_grav * H0 * dhdx / tauc0, 1./basal_q)*tauc_threshold_velocity;
  *v = 0;
  return 0;
}


int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;

  MPI_Comm com = MPI_COMM_WORLD;  // won't be used except for rank,size

  PetscInitializer petsc(argc, argv, help);

  com = PETSC_COMM_WORLD;
  
  /* This explicit scoping forces destructors to be called before PetscFinalize() */
  try {  
    UnitSystem unit_system;
    Config config(com, "pism_config", unit_system),
      overrides(com, "pism_overrides", unit_system);
    init_config(com, config, overrides);

    setVerbosityLevel(5);

    PetscBool usage_set, help_set;
    ierr = PetscOptionsHasName(NULL, "-usage", &usage_set);
    PISM_PETSC_CHK(ierr, "PetscOptionsHasName");
    ierr = PetscOptionsHasName(NULL, "-help", &help_set);
    PISM_PETSC_CHK(ierr, "PetscOptionsHasName");
    if ((usage_set==PETSC_TRUE) || (help_set==PETSC_TRUE)) {
      PetscPrintf(com,
                  "\n"
                  "usage of SSA_TEST_CONST:\n"
                  "  run ssa_test_const -Mx <number> -My <number> -ssa_method <fd|fem>\n"
                  "\n");
    }

    // Parameters that can be overridden by command line options
    int Mx=61;
    int My=61;
    double basal_q = 1.; // linear
    std::string output_file = "ssa_test_const.nc";

    std::set<std::string> ssa_choices;
    ssa_choices.insert("fem");
    ssa_choices.insert("fd");
    std::string driver = "fem";

    ierr = PetscOptionsBegin(com, "", "SSA_TEST_CONST options", "");
    PISM_PETSC_CHK(ierr, "PetscOptionsBegin");
    {
      bool flag;
      int my_verbosity_level;
      OptionsInt("-Mx", "Number of grid points in the X direction", 
                 Mx, flag);
      OptionsInt("-My", "Number of grid points in the Y direction", 
                 My, flag);

      OptionsList("-ssa_method", "Algorithm for computing the SSA solution",
                  ssa_choices, driver, driver, flag);
             
      OptionsReal("-ssa_basal_q", "Exponent q in the pseudo-plastic flow law",
                  basal_q, flag);                                                      
      OptionsString("-o", "Set the output file name", 
                    output_file, flag);

      OptionsInt("-verbose", "Verbosity level",
                 my_verbosity_level, flag);
      if (flag) {
        setVerbosityLevel(my_verbosity_level);
      }
    }
    ierr = PetscOptionsEnd();
    PISM_PETSC_CHK(ierr, "PetscOptionsEnd");

    // Determine the kind of solver to use.
    SSAFactory ssafactory = NULL;
    if (driver == "fem") {
      ssafactory = SSAFEMFactory;
    } else if (driver == "fd") {
      ssafactory = SSAFDFactory;
    } else {
      /* can't happen */
    }

    SSATestCaseConst testcase(com,config,basal_q);
    testcase.init(Mx,My,ssafactory);
    testcase.run();
    testcase.report("const");
    testcase.write(output_file);
  }
  catch (...) {
    handle_fatal_errors(com);
  }

  return 0;
}
