// Copyright (C) 2004-2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cmath>
#include <cstring>
#include <assert.h>

#include <vector>     // STL vector container; sortable; used in test L
#include <algorithm>  // required by sort(...) in test L

#include "tests/exactTestsABCDE.h"
#include "tests/exactTestsFG.h"
#include "tests/exactTestH.h"
#include "tests/exactTestL.h"

#include "iceCompModel.hh"
#include "SIA_Sliding.hh"
#include "SIAFD.hh"
#include "flowlaw_factory.hh"
#include "PISMStressBalance.hh"
#include "enthalpyConverter.hh"
#include "PIO.hh"
#include "pism_options.hh"
#include "POConstant.hh"
#include "PSVerification.hh"
#include "Mask.hh"
#include "error_handling.hh"
#include "PISMBedDef.hh"

namespace pism {

const double IceCompModel::secpera = 3.15569259747e7;

IceCompModel::IceCompModel(IceGrid &g, Config &conf, Config &conf_overrides, int mytest)
  : IceModel(g, conf, conf_overrides) {

  // note lots of defaults are set by the IceModel constructor

  // defaults for IceCompModel:
  testname = mytest;
  exactOnly = false;
  bedrock_is_ice_forK = false;

  // Override some defaults from parent class
  config.set_double("sia_enhancement_factor", 1.0);
  // none use bed smoothing & bed roughness parameterization
  config.set_double("bed_smoother_range", 0.0);

  // set values of flags in run()
  config.set_flag("do_mass_conserve", true);
  config.set_flag("include_bmr_in_continuity", false);

  if (testname == 'V') {
    config.set_string("ssa_flow_law", "isothermal_glen");
    config.set_double("ice_softness", pow(1.9e8, -config.get("sia_Glen_exponent")));
  } else {
    // Set the default for IceCompModel:
    config.set_string("sia_flow_law", "arr");
  }
}

void IceCompModel::createVecs() {

  IceModel::createVecs();

  vHexactL.create(grid, "HexactL", WITH_GHOSTS, 2);

  strain_heating3_comp.create(grid,"strain_heating_comp", WITHOUT_GHOSTS);
  strain_heating3_comp.set_attrs("internal","rate of compensatory strain heating in ice",
                                 "W m-3", "");
}

void IceCompModel::set_grid_defaults() {

  // This sets the defaults for each test; command-line options can override this.

  // use the non-periodic grid:
  Periodicity periodicity = NOT_PERIODIC;
  // equal spacing is the default for all the tests except K
  SpacingType spacing = EQUAL;

  double Lx = 0.0, Ly = 0.0, Lz = 0.0;

  unsigned int
    Mx = grid.Mx(),
    My = grid.My(),
    Mz = grid.Mz();

  switch (testname) {
  case 'A':
  case 'E':
    // use 1600km by 1600km by 4000m rectangular domain
    Lx = 800e3;
    Ly = Lx;
    Lz = 4000;
    break;
  case 'B':
  case 'H':
    // use 2400km by 2400km by 4000m rectangular domain
    Lx = 1200e3;
    Ly = Lx;
    Lz = 4000;
    break;
  case 'C':
  case 'D':
    // use 2000km by 2000km by 4000m rectangular domain
    Lx = 1000e3;
    Ly = Lx;
    Lz = 4000;
    break;
  case 'F':
  case 'G':
  case 'L':
    // use 1800km by 1800km by 4000m rectangular domain
    Lx = 900e3;
    Ly = Lx;
    Lz = 4000;
    break;
  case 'K':
  case 'O':
    // use 2000km by 2000km by 4000m rectangular domain, but make truely periodic
    config.set_double("grid_Mbz", 2);
    config.set_double("grid_Lbz", 1000);
    Lx = 1000e3;
    Ly = Lx;
    Lz = 4000;
    periodicity = XY_PERIODIC;
    spacing = QUADRATIC;
    break;
  case 'V':
    My = 3;             // it's a flow-line setup
    Lx = 500e3;            // 500 km long
    Ly = grid.Ly();
    Lz = grid.Lz();
    periodicity = Y_PERIODIC;
    break;
  default:
    throw RuntimeError("desired test not implemented\n");
  }

  grid.set_size_and_extent(0.0, 0.0, Lx, Ly, Mx, My, periodicity);
  grid.set_vertical_levels(Lz, Mz, spacing);

  grid.time->init();
}

void IceCompModel::setFromOptions() {

  verbPrintf(2, grid.com, "starting Test %c ...\n", testname);

  /* This switch turns off actual numerical evolution and simply reports the
     exact solution. */
  bool flag = options::Bool("-eo", "exact only");
  if (flag) {
    exactOnly = true;
    verbPrintf(1,grid.com, "!!EXACT SOLUTION ONLY, NO NUMERICAL SOLUTION!!\n");
  }

  // These ifs are here (and not in the constructor or later) because
  // testname actually comes from a command-line *and* because command-line
  // options should be able to override parameter values set here.

  if (testname == 'H') {
    config.set_string("bed_deformation_model", "iso");
  } else
    config.set_string("bed_deformation_model", "none");

  if ((testname == 'F') || (testname == 'G') || (testname == 'K') || (testname == 'O')) {
    config.set_flag("do_energy", true);
    // essentially turn off run-time reporting of extremely low computed
    // temperatures; *they will be reported as errors* anyway
    config.set_double("global_min_allowed_temp", 0.0);
    config.set_double("max_low_temp_count", 1000000);
  } else
    config.set_flag("do_energy", false);

  config.set_flag("is_dry_simulation", true);

  // special considerations for K and O wrt thermal bedrock and pressure-melting
  if ((testname == 'K') || (testname == 'O')) {
    config.set_flag("temperature_allow_above_melting", false);
  } else {
    // note temps are generally allowed to go above pressure melting in verify
    config.set_flag("temperature_allow_above_melting", true);
  }

  if (testname == 'V') {
    // no sub-shelf melting
    config.set_flag("include_bmr_in_continuity", false);

    // this test is isothermal
    config.set_flag("do_energy", false);

    // do not use the SIA stress balance
    config.set_flag("do_sia", false);

    // do use the SSA solver
    config.set_string("stress_balance_model", "ssa");

    // this certainly is not a "dry simulation"
    config.set_flag("is_dry_simulation", false);

    config.set_flag("ssa_dirichlet_bc", true);
  }

  config.set_flag("do_cold_ice_methods", true);

  IceModel::setFromOptions();
}

void IceCompModel::allocate_enthalpy_converter() {

  if (EC != NULL) {
    return;
  }

  // allocate the "special" enthalpy converter;
  EC = new ICMEnthalpyConverter(config);
}

void IceCompModel::allocate_bedrock_thermal_unit() {

  if (btu != NULL) {
    return;
  }

  // this switch changes Test K to make material properties for bedrock the same as for ice
  bool biiSet = options::Bool("-bedrock_is_ice", "set bedrock properties to those of ice");
  if (biiSet == true) {
    if (testname == 'K') {
      verbPrintf(1,grid.com,
                 "setting material properties of bedrock to those of ice in Test K\n");
      config.set_double("bedrock_thermal_density", config.get("ice_density"));
      config.set_double("bedrock_thermal_conductivity", config.get("ice_thermal_conductivity"));
      config.set_double("bedrock_thermal_specific_heat_capacity", config.get("ice_specific_heat_capacity"));
      bedrock_is_ice_forK = true;
    } else {
      verbPrintf(1,grid.com,
                 "IceCompModel WARNING: option -bedrock_is_ice ignored; only applies to Test K\n");
    }
  }

  if (testname != 'K') {
    // now make bedrock have same material properties as ice
    // (note Mbz=1 also, by default, but want ice/rock interface to see
    // pure ice from the point of view of applying geothermal boundary
    // condition, especially in tests F and G)
    config.set_double("bedrock_thermal_density", config.get("ice_density"));
    config.set_double("bedrock_thermal_conductivity", config.get("ice_thermal_conductivity"));
    config.set_double("bedrock_thermal_specific_heat_capacity", config.get("ice_specific_heat_capacity"));
  }

  btu = new BTU_Verification(grid, testname, bedrock_is_ice_forK);
}

void IceCompModel::allocate_stressbalance() {

  if (stress_balance != NULL) {
    return;
  }

  if (testname == 'E') {
    config.set_flag("sia_sliding_verification_mode", true);
    ShallowStressBalance *ssb = new SIA_Sliding(grid, *EC);
    SIAFD *sia = new SIAFD(grid, *EC);

    stress_balance = new StressBalance(grid, ssb, sia);
  } else {
    IceModel::allocate_stressbalance();
  }

  if (testname != 'V') {
    // check on whether the options (already checked) chose the right
    // IceFlowLaw for verification (we need to have the right flow law for
    // errors to make sense)

    IceFlowLaw *ice = stress_balance->get_ssb_modifier()->flow_law();

    if (IceFlowLawIsPatersonBuddCold(ice, config, EC) == false) {
      verbPrintf(1, grid.com,
                 "WARNING: SIA flow law should be '-sia_flow_law arr' for the selected pismv test.\n");
    }
  }
}

void IceCompModel::allocate_bed_deformation() {

  IceModel::allocate_bed_deformation();

  f = config.get("ice_density") / config.get("lithosphere_density");  // for simple isostasy

  std::string bed_def_model = config.get_string("bed_deformation_model");

  if ((testname == 'H') && bed_def_model != "iso") {
    verbPrintf(1,grid.com,
               "IceCompModel WARNING: Test H should be run with option\n"
               "  '-bed_def iso'  for the reported errors to be correct.\n");
  }
}

void IceCompModel::allocate_couplers() {
  // Climate will always come from verification test formulas.
  surface = new surface::Verification(grid, EC, testname);
  ocean   = new ocean::Constant(grid);
}

void IceCompModel::set_vars_from_options() {

  // -boot_file command-line option is not allowed here.
  options::forbidden("-boot_file");

  strain_heating3_comp.set(0.0);

  verbPrintf(3,grid.com,
             "initializing Test %c from formulas ...\n",testname);

  // all have no uplift
  IceModelVec2S bed_uplift;
  bed_uplift.create(grid, "uplift", WITHOUT_GHOSTS);
  bed_uplift.set(0);
  beddef->set_uplift(bed_uplift);

  // this is the correct initialization for Test O (and every other
  // test; they all generate zero basal melt rate)
  basal_melt_rate.set(0.0);

  // Test-specific initialization:
  switch (testname) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'H':
    initTestABCDEH();
    break;
  case 'F':
  case 'G':
    initTestFG();  // see iCMthermo.cc
    break;
  case 'K':
  case 'O':
    initTestsKO();  // see iCMthermo.cc
    break;
  case 'L':
    initTestL();
    break;
  case 'V':
    test_V_init();
    break;
  default:
    throw RuntimeError("Desired test not implemented by IceCompModel.");
  }

  compute_enthalpy_cold(T3, Enth3);
}

void IceCompModel::initTestABCDEH() {
  double     A0, T0, H, accum, dummy1, dummy2, dummy3;

  ThermoGlenArrIce tgaIce(grid.com, "sia_", config, EC);

  const double time = grid.time->current();

  // compute T so that A0 = A(T) = Acold exp(-Qcold/(R T))  (i.e. for ThermoGlenArrIce);
  // set all temps to this constant
  A0 = 1.0e-16/secpera;    // = 3.17e-24  1/(Pa^3 s);  (EISMINT value) flow law parameter
  T0 = tgaIce.tempFromSoftness(A0);

  T3.set(T0);
  geothermal_flux.set(Ggeo);
  vMask.set(MASK_GROUNDED);

  IceModelVec::AccessList list(ice_thickness);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double xx = grid.x(i), yy = grid.y(j),
      r = radius(grid, i, j);
    switch (testname) {
    case 'A':
      exactA(r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'B':
      exactB(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'C':
      exactC(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'D':
      exactD(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'E':
      exactE(xx, yy, &H, &accum, &dummy1, &dummy2, &dummy3);
      ice_thickness(i, j)   = H;
      break;
    case 'H':
      exactH(f, time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    default:
      throw RuntimeError("test must be A, B, C, D, E, or H");
    }
  }

  ice_thickness.update_ghosts();

  {
    IceModelVec2S bed_topography;
    bed_topography.create(grid, "topg", WITHOUT_GHOSTS);

    if (testname == 'H') {
      ice_thickness.copy_to(bed_topography);
      bed_topography.scale(-f);
    } else {  // flat bed case otherwise
      bed_topography.set(0.0);
    }
    beddef->set_elevation(bed_topography);
  }
}

//! Class used initTestL() in generating sorted list for ODE solver.
class rgrid {
public:
  double r;
  int    i,j;
};

//! Comparison used initTestL() in generating sorted list for ODE solver.
struct rgridReverseSort {
  bool operator()(rgrid a, rgrid b) {
    return (a.r > b.r);
  }
};

void IceCompModel::initTestL() {
  int ierr;
  double     A0, T0;

  assert(testname == 'L');

  ThermoGlenArrIce tgaIce(grid.com, "sia_", config, EC);

  // compute T so that A0 = A(T) = Acold exp(-Qcold/(R T))  (i.e. for ThermoGlenArrIce);
  // set all temps to this constant
  A0 = 1.0e-16/secpera;    // = 3.17e-24  1/(Pa^3 s);  (EISMINT value) flow law parameter
  T0 = tgaIce.tempFromSoftness(A0);

  T3.set(T0); 
  geothermal_flux.set(Ggeo); 

  // setup to evaluate test L; requires solving an ODE numerically
  //   using sorted list of radii, sorted in decreasing radius order
  const int MM = grid.xm() * grid.ym();

  std::vector<rgrid> rrv(MM);  // destructor at end of scope
  int k = 0;
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    rrv[k].i = i;
    rrv[k].j = j;
    rrv[k].r = radius(grid, i,j);

    k += 1;
  }

  std::sort(rrv.begin(), rrv.end(), rgridReverseSort()); // so rrv[k].r > rrv[k+1].r

  // get soln to test L at these radii; solves ODE only once (on each processor)
  std::vector<double> rr(MM), HH(MM), bb(MM), aa(MM);

  for (k = 0; k < MM; k++) {
    rr[k] = rrv[k].r;
  }

  ierr = exactL_list(&rr[0], MM, &HH[0], &bb[0], &aa[0]);
  switch (ierr) {
     case TESTL_NOT_DONE:
       verbPrintf(1,grid.com,
          "\n\nTest L ERROR: exactL_list() returns 'NOT_DONE' ...\n\n\n",ierr);
       break;
     case TESTL_NOT_DECREASING:
       verbPrintf(1,grid.com,
          "\n\nTest L ERROR: exactL_list() returns 'NOT_DECREASING' ...\n\n\n",ierr);
       break;
     case TESTL_INVALID_METHOD:
       verbPrintf(1,grid.com,
          "\n\nTest L ERROR: exactL_list() returns 'INVALID_METHOD' ...\n\n\n",ierr);
       break;
     case TESTL_NO_LIST:
       verbPrintf(1,grid.com,
          "\n\nTest L ERROR: exactL_list() returns 'NO_LIST' ...\n\n\n",ierr);
       break;
     default:
       break;
  }
  if (ierr != 0) {
    throw RuntimeError("test L: exactL_list(..) failed");
  }

  {
    IceModelVec2S bed_topography;
    bed_topography.create(grid, "topg", WITHOUT_GHOSTS);

    IceModelVec::AccessList list;
    list.add(ice_thickness);
    list.add(bed_topography);

    for (k = 0; k < MM; k++) {
      ice_thickness(rrv[k].i, rrv[k].j)  = HH[k];
      bed_topography(rrv[k].i, rrv[k].j) = bb[k];
    }

    ice_thickness.update_ghosts(); 
    beddef->set_elevation(bed_topography);
  }

  // store copy of ice_thickness for "-eo" runs and for evaluating geometry errors
  ice_thickness.copy_to(vHexactL);
}

//! \brief Tests A and E have a thickness B.C. (ice_thickness == 0 outside a circle of radius 750km).
void IceCompModel::reset_thickness_tests_AE() {
  const double LforAE = 750e3; // m

  IceModelVec::AccessList list(ice_thickness);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (radius(grid, i, j) > LforAE) {
      ice_thickness(i, j) = 0;
    }
  }

  ice_thickness.update_ghosts();
}



void IceCompModel::fillSolnTestABCDH() {
  double     H, accum;

  const double time = grid.time->current();

  IceModelVec::AccessList list(ice_thickness);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);
    switch (testname) {
    case 'A':
      exactA(r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'B':
      exactB(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'C':
      exactC(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'D':
      exactD(time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    case 'H':
      exactH(f, time, r, &H, &accum);
      ice_thickness(i, j)   = H;
      break;
    default:
      throw RuntimeError("test must be A, B, C, D, or H");
    }
  }

  ice_thickness.update_ghosts();

  {
    IceModelVec2S bed_topography;
    bed_topography.create(grid, "topg", WITHOUT_GHOSTS);

    if (testname == 'H') {
      ice_thickness.copy_to(bed_topography);
      bed_topography.scale(-f);
    } else {
      bed_topography.set(0.0);
    }
    beddef->set_elevation(bed_topography);
  }
}


void IceCompModel::fillSolnTestE() {
  double     H, accum, dummy;
  Vector2     bvel;

  // FIXME: This code messes with a field owned by the stress balance
  // object. This is BAD.
  IceModelVec2V &vel_adv = const_cast<IceModelVec2V&>(stress_balance->advective_velocity());

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(vel_adv);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double xx = grid.x(i), yy = grid.y(j);
    exactE(xx, yy, &H, &accum, &dummy, &bvel.u, &bvel.v);
    ice_thickness(i,j) = H;
    vel_adv(i,j)    = bvel;
  }

  ice_thickness.update_ghosts(); 
}


void IceCompModel::fillSolnTestL() {

  vHexactL.update_ghosts();
  ice_thickness.copy_from(vHexactL);

  // note bed was filled at initialization and hasn't changed
}


void IceCompModel::computeGeometryErrors(double &gvolexact, double &gareaexact,
                                                   double &gdomeHexact, double &volerr,
                                                   double &areaerr, double &gmaxHerr,
                                                   double &gavHerr, double &gmaxetaerr,
                                                   double &centerHerr) {
  // compute errors in thickness, eta=thickness^{(2n+2)/n}, volume, area

  const double time = grid.time->current();
  double
    Hexact     = 0.0,
    vol        = 0.0,
    area       = 0.0,
    domeH      = 0.0,
    volexact   = 0.0,
    areaexact  = 0.0,
    domeHexact = 0.0;
  double
    Herr   = 0.0,
    avHerr = 0.0,
    etaerr = 0.0;

  double     dummy, z, dummy1, dummy2, dummy3, dummy4, dummy5;

  IceModelVec::AccessList list(ice_thickness);
  if (testname == 'L') {
    list.add(vHexactL);
  }

  double
    seawater_density = config.get("sea_water_density"),
    ice_density      = config.get("ice_density"),
    Glen_n           = config.get("sia_Glen_exponent"),
    standard_gravity = config.get("standard_gravity");

  // area of grid square in square km:
  const double   a = grid.dx() * grid.dy() * 1e-3 * 1e-3;
  const double   m = (2.0 * Glen_n + 2.0) / Glen_n;
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (ice_thickness(i,j) > 0) {
      area += a;
      vol += a * ice_thickness(i,j) * 1e-3;
    }
    double xx = grid.x(i), yy = grid.y(j),
      r = radius(grid, i,j);
    switch (testname) {
    case 'A':
      exactA(r,&Hexact,&dummy);
      break;
    case 'B':
      exactB(time,r,&Hexact,&dummy);
      break;
    case 'C':
      exactC(time,r,&Hexact,&dummy);
      break;
    case 'D':
      exactD(time,r,&Hexact,&dummy);
      break;
    case 'E':
      exactE(xx,yy,&Hexact,&dummy,&dummy1,&dummy2,&dummy3);
      break;
    case 'F':
      if (r > LforFG - 1.0) {  // outside of sheet
        Hexact=0.0;
      } else {
        r=std::max(r,1.0);
        z=0.0;
        bothexact(0.0,r,&z,1,0.0,
                  &Hexact,&dummy,&dummy5,&dummy1,&dummy2,&dummy3,&dummy4);
      }
      break;
    case 'G':
      if (r > LforFG -1.0) {  // outside of sheet
        Hexact=0.0;
      } else {
        r=std::max(r,1.0);
        z=0.0;
        bothexact(time,r,&z,1,ApforG,
                  &Hexact,&dummy,&dummy5,&dummy1,&dummy2,&dummy3,&dummy4);
      }
      break;
    case 'H':
      exactH(f,time,r,&Hexact,&dummy);
      break;
    case 'K':
    case 'O':
      Hexact = 3000.0;
      break;
    case 'L':
      Hexact = vHexactL(i,j);
      break;
    case 'V':
      {
        double
          H0 = 600.0,
          v0 = grid.convert(300.0, "m/year", "m/second"),
          Q0 = H0 * v0,
          B0 = stress_balance->get_stressbalance()->flow_law()->hardness_parameter(0, 0),
          C  = pow(ice_density * standard_gravity * (1.0 - ice_density/seawater_density) / (4 * B0), 3);

        Hexact = pow(4 * C / Q0 * xx + 1/pow(H0, 4), -0.25);
      }
      break;
    default:
      throw RuntimeError("test must be A, B, C, D, E, F, G, H, K, L, or O");
    }

    if (Hexact > 0) {
      areaexact += a;
      volexact += a * Hexact * 1e-3;
    }
    if (i == ((int)grid.Mx() - 1)/2 and
        j == ((int)grid.My() - 1)/2) {
      domeH = ice_thickness(i,j);
      domeHexact = Hexact;
    }
    // compute maximum errors
    Herr = std::max(Herr,fabs(ice_thickness(i,j) - Hexact));
    etaerr = std::max(etaerr,fabs(pow(ice_thickness(i,j),m) - pow(Hexact,m)));
    // add to sums for average errors
    avHerr += fabs(ice_thickness(i,j) - Hexact);
  }

  // globalize (find errors over all processors)
  double gvol, garea, gdomeH;
  gvolexact = GlobalSum(grid.com, volexact);
  gdomeHexact = GlobalMax(grid.com, domeHexact);
  gareaexact = GlobalSum(grid.com, areaexact);

  gvol = GlobalSum(grid.com, vol);
  garea = GlobalSum(grid.com, area);
  volerr = fabs(gvol - gvolexact);
  areaerr = fabs(garea - gareaexact);

  gmaxHerr = GlobalMax(grid.com, Herr);
  gavHerr = GlobalSum(grid.com, avHerr);
  gavHerr = gavHerr/(grid.Mx()*grid.My());
  gmaxetaerr = GlobalMax(grid.com, etaerr);

  gdomeH = GlobalMax(grid.com, domeH);
  centerHerr = fabs(gdomeH - gdomeHexact);
}


void IceCompModel::computeBasalVelocityErrors(double &exactmaxspeed, double &gmaxvecerr,
                                                        double &gavvecerr, double &gmaxuberr,
                                                        double &gmaxvberr) {
  double    maxvecerr, avvecerr, maxuberr, maxvberr;
  double    ubexact,vbexact, dummy1,dummy2,dummy3;
  Vector2    bvel;

  if (testname != 'E') {
    throw RuntimeError("basal velocity errors only computable for test E");
  }

  const IceModelVec2V &vel_adv = stress_balance->advective_velocity();

  IceModelVec::AccessList list;
  list.add(vel_adv);
  list.add(ice_thickness);

  maxvecerr = 0.0; avvecerr = 0.0; maxuberr = 0.0; maxvberr = 0.0;
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (ice_thickness(i,j) > 0.0) {
      double xx = grid.x(i), yy = grid.y(j);
      exactE(xx,yy,&dummy1,&dummy2,&dummy3,&ubexact,&vbexact);
      // compute maximum errors
      const double uberr = fabs(vel_adv(i,j).u - ubexact);
      const double vberr = fabs(vel_adv(i,j).v - vbexact);
      maxuberr = std::max(maxuberr,uberr);
      maxvberr = std::max(maxvberr,vberr);
      const double vecerr = sqrt(uberr*uberr + vberr*vberr);
      maxvecerr = std::max(maxvecerr,vecerr);
      avvecerr += vecerr;
    }
  }

  gmaxuberr = GlobalMax(grid.com, maxuberr);
  gmaxvberr = GlobalMax(grid.com, maxvberr);

  gmaxvecerr = GlobalMax(grid.com, maxvecerr);
  gavvecerr = GlobalSum(grid.com, avvecerr);
  gavvecerr = gavvecerr/(grid.Mx()*grid.My());

  const double xpeak = 450e3 * cos(25.0*(M_PI/180.0)),
                    ypeak = 450e3 * sin(25.0*(M_PI/180.0));
  exactE(xpeak,ypeak,&dummy1,&dummy2,&dummy3,&ubexact,&vbexact);
  exactmaxspeed = sqrt(ubexact*ubexact + vbexact*vbexact);
}


void IceCompModel::additionalAtStartTimestep() {

  if (exactOnly == true && testname != 'K') {
    dt_force = config.get("maximum_time_step_years", "years", "seconds");
  }

  if (testname == 'F' || testname == 'G') {
    getCompSourcesTestFG();
  }
}


void IceCompModel::additionalAtEndTimestep() {

  if (testname == 'A' || testname == 'E') {
    reset_thickness_tests_AE();
  }

  // do nothing at the end of the time step unless the user has asked for the
  // exact solution to overwrite the numerical solution
  if (exactOnly == false) {
    return;
  }

  // because user wants exact solution, fill gridded values from exact formulas;
  // important notes:
  //     (1) the numerical computation *has* already occurred, in run(),
  //           and we just overwrite it with the exact solution here
  //     (2) certain diagnostic quantities like dHdt are computed numerically,
  //           and not overwritten here; while velbar_mag,velsurf_mag,flux_mag,wsurf are diagnostic
  //           quantities recomputed at the end of the run for writing into
  //           NetCDF, in particular dHdt is not recomputed before being written
  //           into the output file, so it is actually numerical
  switch (testname) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'H':
    fillSolnTestABCDH();
    break;
  case 'E':
    fillSolnTestE();
    break;
  case 'F':
  case 'G':
    fillSolnTestFG(); // see iCMthermo.cc
    break;
  case 'K':
    fillTemperatureSolnTestsKO(); // see iCMthermo.cc
    break;
  case 'O':
    fillTemperatureSolnTestsKO(); // see iCMthermo.cc
    fillBasalMeltRateSolnTestO(); // see iCMthermo.cc
    break;
  case 'L':
    fillSolnTestL();
    break;
  default:
    throw RuntimeError::formatted("unknown testname %c in IceCompModel", testname);
  }
}


void IceCompModel::summary(bool /* tempAndAge */) {
  //   we always show a summary at every step
  IceModel::summary(true);
}


void IceCompModel::reportErrors() {
  // geometry errors to report (for all tests except K and O):
  //    -- max thickness error
  //    -- average (at each grid point on whole grid) thickness error
  //    -- max (thickness)^(2n+2)/n error
  //    -- volume error
  //    -- area error
  // and temperature errors (for tests F & G & K & O):
  //    -- max T error over 3D domain of ice
  //    -- av T error over 3D domain of ice
  // and basal temperature errors (for tests F & G):
  //    -- max basal temp error
  //    -- average (at each grid point on whole grid) basal temp error
  // and bedrock temperature errors (for tests K & O):
  //    -- max Tb error over 3D domain of bedrock
  //    -- av Tb error over 3D domain of bedrock
  // and strain-heating (Sigma) errors (for tests F & G):
  //    -- max Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  //    -- av Sigma error over 3D domain of ice (in 10^-3 K a^-1)
  // and basal melt rate error (for test O):
  //    -- max bmelt error over base of ice
  // and surface velocity errors (for tests F & G):
  //    -- max |<us,vs> - <usex,vsex>| error
  //    -- av |<us,vs> - <usex,vsex>| error
  //    -- max ws error
  //    -- av ws error
  // and basal sliding errors (for test E):
  //    -- max ub error
  //    -- max vb error
  //    -- max |<ub,vb> - <ubexact,vbexact>| error
  //    -- av |<ub,vb> - <ubexact,vbexact>| error

  bool dont_report = options::Bool("-no_report", "Don't report numerical errors");

  if (dont_report) {
    return;
  }

  IceFlowLaw* flow_law = stress_balance->get_ssb_modifier()->flow_law();
  if ((testname == 'F' or testname == 'G') and
      testname != 'V' and
      not IceFlowLawIsPatersonBuddCold(flow_law, config, EC)) {
    verbPrintf(1, grid.com,
               "pismv WARNING: flow law must be cold part of Paterson-Budd ('-siafd_flow_law arr')\n"
               "   for reported errors in test %c to be meaningful!\n",
               testname);
  }

  verbPrintf(1,grid.com,
             "NUMERICAL ERRORS evaluated at final time (relative to exact solution):\n");

  unsigned int start;
  NCTimeseries err("N", "N", grid.config.get_unit_system());

  err.set_units("1");

  PIO nc(grid.com, "netcdf3", grid.config.get_unit_system()); // OK to use netcdf3

  options::String report_file("-report_file", "NetCDF error report file");
  bool append = options::Bool("-append", "Append the NetCDF error report");

  IO_Mode mode = PISM_READWRITE;
  if (append == false) {
    mode = PISM_READWRITE_MOVE;
  }

  if (report_file.is_set()) {
    verbPrintf(2,grid.com, "Also writing errors to '%s'...\n", report_file->c_str());

    // Find the number of records in this file:
    nc.open(report_file, mode);
    start = nc.inq_dimlen("N");

    nc.write_global_attributes(global_attributes);

    // Write the dimension variable:
    nc.write_timeseries(err, (size_t)start, (double)(start + 1), PISM_INT);

    // Always write grid parameters:
    err.set_name("dx");
    err.set_units("meters");
    nc.write_timeseries(err, (size_t)start, grid.dx());
    err.set_name("dy");
    nc.write_timeseries(err, (size_t)start, grid.dy());
    err.set_name("dz");
    nc.write_timeseries(err, (size_t)start, grid.dz_max());

    // Always write the test name:
    err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
    err.set_name("test");
    nc.write_timeseries(err, (size_t)start, (double)testname, PISM_BYTE);
  }

  // geometry (thickness, vol) errors if appropriate; reported in m except for relmaxETA
  if ((testname != 'K') && (testname != 'O')) {
    double volexact, areaexact, domeHexact, volerr, areaerr, maxHerr, avHerr,
                maxetaerr, centerHerr;
    computeGeometryErrors(volexact,areaexact,domeHexact,
                          volerr,areaerr,maxHerr,avHerr,maxetaerr,centerHerr);
    verbPrintf(1,grid.com,
               "geometry  :    prcntVOL        maxH         avH   relmaxETA\n");  // no longer reporting centerHerr
    const double   m = (2.0 * flow_law->exponent() + 2.0) / flow_law->exponent();
    verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n",
               100*volerr/volexact, maxHerr, avHerr,
               maxetaerr/pow(domeHexact,m));

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("relative_volume");
      err.set_units("percent");
      err.set_string("long_name", "relative ice volume error");
      nc.write_timeseries(err, (size_t)start, 100*volerr/volexact);

      err.set_name("relative_max_eta");
      err.set_units("1");
      err.set_string("long_name", "relative $\\eta$ error");
      nc.write_timeseries(err, (size_t)start, maxetaerr/pow(domeHexact,m));

      err.set_name("maximum_thickness");
      err.set_units("meters");
      err.set_string("long_name", "maximum ice thickness error");
      nc.write_timeseries(err, (size_t)start, maxHerr);

      err.set_name("average_thickness");
      err.set_units("meters");
      err.set_string("long_name", "average ice thickness error");
      nc.write_timeseries(err, (size_t)start, avHerr);
    }
  }

  // temperature errors for F and G
  if ((testname == 'F') || (testname == 'G')) {
    double maxTerr, avTerr, basemaxTerr, baseavTerr, basecenterTerr;
    computeTemperatureErrors(maxTerr, avTerr);
    computeBasalTemperatureErrors(basemaxTerr, baseavTerr, basecenterTerr);
    verbPrintf(1,grid.com,
               "temp      :        maxT         avT    basemaxT     baseavT\n");  // no longer reporting   basecenterT
    verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n",
               maxTerr, avTerr, basemaxTerr, baseavTerr);

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_temperature");
      err.set_units("Kelvin");
      err.set_string("long_name", "maximum ice temperature error");
      nc.write_timeseries(err, (size_t)start, maxTerr);

      err.set_name("average_temperature");
      err.set_string("long_name", "average ice temperature error");
      nc.write_timeseries(err, (size_t)start, avTerr);

      err.set_name("maximum_basal_temperature");
      err.set_string("long_name", "maximum basal temperature error");
      nc.write_timeseries(err, (size_t)start, basemaxTerr);
      err.set_name("average_basal_temperature");
      err.set_string("long_name", "average basal temperature error");
      nc.write_timeseries(err, (size_t)start, baseavTerr);
    }

  } else if ((testname == 'K') || (testname == 'O')) {
    double maxTerr, avTerr, maxTberr, avTberr;
    computeIceBedrockTemperatureErrors(maxTerr, avTerr, maxTberr, avTberr);
    verbPrintf(1,grid.com,
               "temp      :        maxT         avT       maxTb        avTb\n");
    verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n",
               maxTerr, avTerr, maxTberr, avTberr);

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_temperature");
      err.set_units("Kelvin");
      err.set_string("long_name", "maximum ice temperature error");
      nc.write_timeseries(err, (size_t)start, maxTerr);

      err.set_name("average_temperature");
      err.set_string("long_name", "average ice temperature error");
      nc.write_timeseries(err, (size_t)start, avTerr);

      err.set_name("maximum_bedrock_temperature");
      err.set_string("long_name", "maximum bedrock temperature error");
      nc.write_timeseries(err, (size_t)start, maxTberr);

      err.set_name("average_bedrock_temperature");
      err.set_string("long_name", "average bedrock temperature error");
      nc.write_timeseries(err, (size_t)start, avTberr);
    }
  }

  // strain_heating errors if appropriate; reported in 10^6 J/(s m^3)
  if ((testname == 'F') || (testname == 'G')) {
    double max_strain_heating_error, av_strain_heating_error;
    compute_strain_heating_errors(max_strain_heating_error, av_strain_heating_error);
    verbPrintf(1,grid.com,
               "Sigma     :      maxSig       avSig\n");
    verbPrintf(1,grid.com, "           %12.6f%12.6f\n",
               max_strain_heating_error*1.0e6, av_strain_heating_error*1.0e6);

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_sigma");
      err.set_units("J s-1 m-3");
      err.set_glaciological_units("1e6 J s-1 m-3");
      err.set_string("long_name", "maximum strain heating error");
      nc.write_timeseries(err, (size_t)start, max_strain_heating_error);

      err.set_name("average_sigma");
      err.set_string("long_name", "average strain heating error");
      nc.write_timeseries(err, (size_t)start, av_strain_heating_error);
    }
  }

  // surface velocity errors if exact values are available; reported in m/year
  if ((testname == 'F') || (testname == 'G')) {
    double maxUerr, avUerr, maxWerr, avWerr;
    computeSurfaceVelocityErrors(maxUerr, avUerr, maxWerr, avWerr);
    verbPrintf(1,grid.com,
               "surf vels :     maxUvec      avUvec        maxW         avW\n");
    verbPrintf(1,grid.com, "           %12.6f%12.6f%12.6f%12.6f\n",
               grid.convert(maxUerr, "m/second", "m/year"), grid.convert(avUerr, "m/second", "m/year"), grid.convert(maxWerr, "m/second", "m/year"), grid.convert(avWerr, "m/second", "m/year"));

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_surface_velocity");
      err.set_string("long_name", "maximum ice surface horizontal velocity error");
      err.set_units("m/s");
      err.set_glaciological_units("meters/year");
      nc.write_timeseries(err, (size_t)start, maxUerr);

      err.set_name("average_surface_velocity");
      err.set_string("long_name", "average ice surface horizontal velocity error");
      nc.write_timeseries(err, (size_t)start, avUerr);

      err.set_name("maximum_surface_w");
      err.set_string("long_name", "maximum ice surface vertical velocity error");
      nc.write_timeseries(err, (size_t)start, maxWerr);

      err.set_name("average_surface_w");
      err.set_string("long_name", "average ice surface vertical velocity error");
      nc.write_timeseries(err, (size_t)start, avWerr);
    }
  }

  // basal velocity errors if appropriate; reported in m/year except prcntavvec
  if (testname == 'E') {
    double exactmaxspeed, maxvecerr, avvecerr, maxuberr, maxvberr;
    computeBasalVelocityErrors(exactmaxspeed,
                               maxvecerr,avvecerr,maxuberr,maxvberr);
    verbPrintf(1,grid.com,
               "base vels :  maxvector   avvector  prcntavvec     maxub     maxvb\n");
    verbPrintf(1,grid.com, "           %11.4f%11.5f%12.5f%10.4f%10.4f\n",
               grid.convert(maxvecerr, "m/second", "m/year"), grid.convert(avvecerr, "m/second", "m/year"),
               (avvecerr/exactmaxspeed)*100.0,
               grid.convert(maxuberr, "m/second", "m/year"), grid.convert(maxvberr, "m/second", "m/year"));

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_basal_velocity");
      err.set_units("m/s");
      err.set_glaciological_units("meters/year");
      nc.write_timeseries(err, (size_t)start, maxvecerr);

      err.set_name("average_basal_velocity");
      nc.write_timeseries(err, (size_t)start, avvecerr);
      err.set_name("maximum_basal_u");
      nc.write_timeseries(err, (size_t)start, maxuberr);
      err.set_name("maximum_basal_v");
      nc.write_timeseries(err, (size_t)start, maxvberr);

      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("relative_basal_velocity");
      err.set_units("percent");
      nc.write_timeseries(err, (size_t)start, (avvecerr/exactmaxspeed)*100);
    }
  }

  // basal melt rate errors if appropriate; reported in m/year
  if (testname == 'O') {
    double maxbmelterr, minbmelterr;
    computeBasalMeltRateErrors(maxbmelterr, minbmelterr);
    if (maxbmelterr != minbmelterr) {
      verbPrintf(1,grid.com,
                 "IceCompModel WARNING: unexpected Test O situation: max and min of bmelt error\n"
                 "  are different: maxbmelterr = %f, minbmelterr = %f\n",
                 grid.convert(maxbmelterr, "m/second", "m/year"),
                 grid.convert(minbmelterr, "m/second", "m/year"));
    }
    verbPrintf(1,grid.com,
               "basal melt:  max\n");
    verbPrintf(1,grid.com, "           %11.5f\n",
               grid.convert(maxbmelterr, "m/second", "m/year"));

    if (report_file.is_set()) {
      err.clear_all_strings(); err.clear_all_doubles(); err.set_units("1");
      err.set_name("maximum_basal_melt_rate");
      err.set_units("m/s");
      err.set_glaciological_units("meters/year");
      nc.write_timeseries(err, (size_t)start, maxbmelterr);
    }
  }

  if (report_file.is_set()) {
    nc.close();
  }

  verbPrintf(1,grid.com, "NUM ERRORS DONE\n");
}

//! \brief Initialize test V.
/*
 Try

 pismv -test V -y 1000 -part_grid -ssa_method fd -cfbc -o fig4-blue.nc
 pismv -test V -y 1000 -part_grid -ssa_method fd -o fig4-green.nc

 to try to reproduce Figure 4.

 Try

 pismv -test V -y 3000 -ssa_method fd -cfbc -o fig5.nc -thickness_calving_threshold 250 -part_grid

 with -Mx 51, -Mx 101, -Mx 201 for figure 5,

 pismv -test V -y 300 -ssa_method fd -o fig6-ab.nc

 for 6a and 6b,

 pismv -test V -y 300 -ssa_method fd -cfbc -part_grid -o fig6-cd.nc

 for 6c and 6d,

 pismv -test V -y 300 -ssa_method fd -cfbc -part_grid -part_redist -o fig6-ef.nc

 for 6e and 6f.

 */
void IceCompModel::test_V_init() {

  {
    // initialize the bed topography
    IceModelVec2S bed_topography;
    bed_topography.create(grid, "topg", WITHOUT_GHOSTS);
    bed_topography.set(-1000);
    beddef->set_elevation(bed_topography);
  }

  // set SSA boundary conditions:
  double upstream_velocity = grid.convert(300.0, "m/year", "m/second"),
    upstream_thk = 600.0;

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(vBCMask);
  list.add(vBCvel);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (i <= 2) {
      vBCMask(i,j) = 1;
      vBCvel(i,j)  = Vector2(upstream_velocity, 0.0);
      ice_thickness(i, j) = upstream_thk;
    } else {
      vBCMask(i,j) = 0;
      vBCvel(i,j)  = Vector2(0.0, 0.0);
      ice_thickness(i, j) = 0;
    }
  }

  vBCMask.update_ghosts();

  vBCvel.update_ghosts();

  ice_thickness.update_ghosts();
}

} // end of namespace pism
