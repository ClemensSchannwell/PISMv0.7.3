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
  "\nSSA_TESTCFBC\n"
  "  Testing program for PISM's implementations of the SSA.\n"
  "  Does a time-independent calculation.  Does not run IceModel or a derived\n"
  "  class thereof. Uses the van der Veen flow-line shelf geometry. Also may be\n"
  "  used in a PISM software (regression) test.\n\n";

#include "pism_const.hh"
#include "iceModelVec.hh"
#include "flowlaws.hh" // IceFlowLaw
#include "basal_resistance.hh" // IceBasalResistancePlasticLaw
#include "PIO.hh"
#include "NCVariable.hh"
#include "SSAFD.hh"
#include "SSATestCase.hh"
#include "pism_options.hh"
#include "Mask.hh"

#include "PetscInitializer.hh"
#include "error_handling.hh"

using namespace pism;

// thickness profile in the van der Veen solution
static double H_exact(double V0, double H0, double C, double x)
{
  const double Q0 = V0*H0;
  return pow(4 * C / Q0 * x + 1/pow(H0, 4), -0.25);
}

// velocity profile; corresponds to constant flux
static double u_exact(double V0, double H0, double C, double x)
{
  const double Q0 = V0*H0;
  return Q0 / H_exact(V0, H0, C, x);
}

class SSATestCaseCFBC: public SSATestCase
{
public:
  SSATestCaseCFBC(MPI_Comm com, Config &c)
    : SSATestCase(com, c)
  {
    UnitSystem s = c.get_unit_system();
    V0 = s.convert(300.0, "m/year", "m/second");
    H0 = 600.0;                 // meters
    C  = 2.45e-18;
  };

  virtual PetscErrorCode write_nuH(const std::string &filename);

protected:
  virtual PetscErrorCode initializeGrid(int Mx, int My);

  virtual PetscErrorCode initializeSSAModel();

  virtual PetscErrorCode initializeSSACoefficients();

  virtual PetscErrorCode exactSolution(int i, int j,
    double x, double y, double *u, double *v);

  double V0, //!< grounding line vertically-averaged velocity
    H0,      //!< grounding line thickness (meters)
    C;       //!< "typical constant ice parameter"
};

PetscErrorCode SSATestCaseCFBC::write_nuH(const std::string &filename) {

  SSAFD *ssafd = dynamic_cast<SSAFD*>(ssa);
  if (ssafd == NULL) {
    throw RuntimeError("ssa_test_cfbc error: have to use the SSAFD solver.");
  }

  SSAFD_nuH nuH(ssafd, grid, vars);

  IceModelVec* result;
  nuH.compute(result);

  result->write(filename);

  delete result;

  return 0;
}

PetscErrorCode SSATestCaseCFBC::initializeGrid(int Mx, int My)
{

  double halfWidth = 250.0e3;  // 500.0 km length
  double Lx = halfWidth, Ly = halfWidth;
  init_shallow_grid(grid, Lx, Ly, Mx, My, Y_PERIODIC);
  return 0;
}

PetscErrorCode SSATestCaseCFBC::initializeSSAModel()
{

  config.set_double("ice_softness", pow(1.9e8, -config.get("ssa_Glen_exponent")));
  config.set_flag("compute_surf_grad_inward_ssa", false);
  config.set_flag("calving_front_stress_boundary_condition", true);
  config.set_string("ssa_flow_law", "isothermal_glen");
  config.set_string("output_variable_order", "zyx");


  enthalpyconverter = new EnthalpyConverter(config);

  return 0;
}

PetscErrorCode SSATestCaseCFBC::initializeSSACoefficients()
{

  tauc.set(0.0);    // irrelevant
  bed.set(-1000.0); // assures shelf is floating


  double enth0  = enthalpyconverter->getEnth(273.15, 0.01, 0.0); // 0.01 water fraction
  enthalpy.set(enth0);

  IceModelVec::AccessList list;
  list.add(thickness);
  list.add(surface);
  list.add(bc_mask);
  list.add(vel_bc);
  list.add(ice_mask);

  double ocean_rho = config.get("sea_water_density"),
    ice_rho = config.get("ice_density");

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    const double x = grid.x[i];

    if (i != grid.Mx - 1) {
      thickness(i, j) = H_exact(V0, H0, C, x + grid.Lx);
      ice_mask(i, j)  = MASK_FLOATING;
    } else {
      thickness(i, j) = 0;
      ice_mask(i, j)  = MASK_ICE_FREE_OCEAN;
    }

    surface(i,j) = (1.0 - ice_rho / ocean_rho) * thickness(i, j);

    if (i == 0) {
      bc_mask(i, j)  = 1;
      vel_bc(i, j).u = V0;
      vel_bc(i, j).v = 0;
    } else {
      bc_mask(i, j)  = 0;
      vel_bc(i, j).u = 0;
      vel_bc(i, j).v = 0;
    }
  }


  // communicate what we have set
  surface.update_ghosts();

  thickness.update_ghosts();

  bc_mask.update_ghosts();

  ice_mask.update_ghosts();

  vel_bc.update_ghosts();

  ssa->set_boundary_conditions(bc_mask, vel_bc);

  return 0;
}

PetscErrorCode SSATestCaseCFBC::exactSolution(int i, int /*j*/,
                                              double x, double /*y*/,
                                              double *u, double *v)
{
  if (i != grid.Mx - 1) {
    *u = u_exact(V0, H0, C, x + grid.Lx);
  } else {
    *u = 0;
  }

  *v = 0;
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
                  "usage of SSA_TEST_CFBC:\n"
                  "  run ssa_test_cfbc -Mx <number> -My <number>\n"
                  "\n");
    }

    // Parameters that can be overridden by command line options
    int Mx=61;
    int My=61;
    std::string output_file = "ssa_test_cfbc.nc";

    ierr = PetscOptionsBegin(com, "", "SSA_TESTCFBC options", "");
    PISM_PETSC_CHK(ierr, "PetscOptionsBegin");
    {
      bool flag;
      int my_verbosity_level;
      OptionsInt("-Mx", "Number of grid points in the X direction",
                 Mx, flag);
      OptionsInt("-My", "Number of grid points in the Y direction",
                 My, flag);
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

    SSAFactory ssafactory = SSAFDFactory;

    SSATestCaseCFBC testcase(com, config);
    testcase.init(Mx,My,ssafactory);
    testcase.run();
    testcase.report("V");
    ierr = testcase.write(output_file);
    testcase.write_nuH(output_file);
  }
  catch (...) {
    handle_fatal_errors(com);
  }

  return 0;
}
