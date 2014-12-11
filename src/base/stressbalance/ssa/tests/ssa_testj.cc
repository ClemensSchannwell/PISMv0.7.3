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

static char help[] =
  "\nSSA_TESTJ\n"
  "  Testing program for the finite element implementation of the SSA.\n"
  "  Does a time-independent calculation.  Does not run IceModel or a derived\n"
  "  class thereof. Uses verification test J. Also may be used in a PISM\n"
  "  software (regression) test.\n\n";

#include "pism_const.hh"
#include "pism_options.hh"
#include "iceModelVec.hh"
#include "flowlaws.hh" // IceFlowLaw
#include "basal_resistance.hh" // IceBasalResistancePlasticLaw
#include "PIO.hh"
#include "NCVariable.hh"
#include "SSAFEM.hh"
#include "SSAFD.hh"
#include "exactTestsIJ.h"
#include "SSATestCase.hh"
#include "Mask.hh"

#include "PetscInitializer.hh"
#include "error_handling.hh"

using namespace pism;

class SSATestCaseJ: public SSATestCase
{
public:
  SSATestCaseJ(MPI_Comm com, Config &c):
    SSATestCase(com, c)
  { };

protected:
  virtual PetscErrorCode initializeGrid(int Mx,int My);

  virtual PetscErrorCode initializeSSAModel();

  virtual PetscErrorCode initializeSSACoefficients();

  virtual PetscErrorCode exactSolution(int i, int j,
    double x, double y, double *u, double *v);

};

PetscErrorCode SSATestCaseJ::initializeGrid(int Mx,int My)
{

  double halfWidth = 300.0e3;  // 300.0 km half-width
  double Lx = halfWidth, Ly = halfWidth;
  grid = IceGrid::Shallow(m_com, config, Lx, Ly,
                          0.0, 0.0, // center: (x0,y0)
                          Mx, My, XY_PERIODIC);
  return 0;
}

PetscErrorCode SSATestCaseJ::initializeSSAModel()
{
  config.set_flag("do_pseudo_plastic_till", false);

  enthalpyconverter = new EnthalpyConverter(config);
  config.set_string("ssa_flow_law", "isothermal_glen");

  return 0;
}

PetscErrorCode SSATestCaseJ::initializeSSACoefficients()
{
  tauc.set(0.0);    // irrelevant for test J
  bed.set(0.0); // assures shelf is floating
  ice_mask.set(MASK_FLOATING);

  double enth0  = enthalpyconverter->getEnth(273.15, 0.01, 0.0); // 0.01 water fraction
  enthalpy.set(enth0);

  /* use Ritz et al (2001) value of 30 MPa yr for typical vertically-averaged viscosity */
  double ocean_rho = config.get("sea_water_density"),
    ice_rho = config.get("ice_density");
  const double nu0 = grid->convert(30.0, "MPa year", "Pa s"); /* = 9.45e14 Pa s */
  const double H0 = 500.;       /* 500 m typical thickness */

  // Test J has a viscosity that is independent of velocity.  So we force a
  // constant viscosity by settting the strength_extension
  // thickness larger than the given ice thickness. (max = 770m).
  ssa->strength_extension->set_notional_strength(nu0 * H0);
  ssa->strength_extension->set_min_thickness(800);

  IceModelVec::AccessList list;
  list.add(thickness);
  list.add(surface);
  list.add(bc_mask);
  list.add(vel_bc);

  for (Points p(*grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double junk1, myu, myv, H;
    const double myx = grid->x(i), myy = grid->y(j);

    // set H,h on regular grid
    exactJ(myx, myy, &H, &junk1, &myu, &myv);

    thickness(i,j) = H;
    surface(i,j) = (1.0 - ice_rho / ocean_rho) * H; // FIXME issue #15

    // special case at center point: here we set vel_bc at (i,j) by marking
    // this grid point as SHEET and setting vel_bc approriately
    if ((i == (grid->Mx())/2) && (j == (grid->My())/2)) {
      bc_mask(i,j) = 1;
      vel_bc(i,j).u = myu;
      vel_bc(i,j).v = myv;
    }
  }

  // communicate what we have set
  surface.update_ghosts();
  thickness.update_ghosts();
  bc_mask.update_ghosts();
  vel_bc.update_ghosts();

  ssa->set_boundary_conditions(bc_mask, vel_bc);

  return 0;
}

PetscErrorCode SSATestCaseJ::exactSolution(int /*i*/, int /*j*/,
                                           double x, double y,
                                           double *u, double *v)
{
  double junk1, junk2;
  exactJ(x, y, &junk1, &junk2, u, v);
  return 0;
}


int main(int argc, char *argv[]) {
  PetscErrorCode  ierr;

  MPI_Comm com = MPI_COMM_WORLD;

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
                  "usage of SSA_TESTJ:\n"
                  "  run ssafe_test -Mx <number> -My <number> -ssa_method <fd|fem>\n"
                  "\n");
    }

    // Parameters that can be overridden by command line options
    int Mx=61;
    int My=61;
    std::string output_file = "ssa_test_j.nc";

    std::set<std::string> ssa_choices;
    ssa_choices.insert("fem");
    ssa_choices.insert("fd");
    std::string driver = "fem";

    ierr = PetscOptionsBegin(com, "", "SSA_TESTJ options", "");
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

    SSATestCaseJ testcase(com,config);
    testcase.init(Mx,My,ssafactory);
    testcase.run();
    testcase.report("J");
    ierr = testcase.write(output_file);
  }
  catch (...) {
    handle_fatal_errors(com);
  }

  return 0;
}
