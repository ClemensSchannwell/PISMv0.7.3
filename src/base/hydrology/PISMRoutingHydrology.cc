// Copyright (C) 2012-2013 PISM Authors
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

#include "PISMVars.hh"
#include "pism_options.hh"
#include "Mask.hh"
#include "PISMHydrology.hh"
#include "hydrology_diagnostics.hh"


PISMRoutingHydrology::PISMRoutingHydrology(IceGrid &g, const NCConfigVariable &conf)
    : PISMHydrology(g, conf)
{
  stripwidth = config.get("hydrology_null_strip_width");
  if (allocate() != 0) {
    PetscPrintf(grid.com, "PISM ERROR: memory allocation failed in PISMRoutingHydrology constructor.\n");
    PISMEnd();
  }
}


PetscErrorCode PISMRoutingHydrology::allocate() {
  PetscErrorCode ierr;

  // model state variables; need ghosts
  ierr = W.create(grid, "bwat", true, 1); CHKERRQ(ierr);
  ierr = W.set_attrs("model_state",
                     "thickness of subglacial water layer",
                     "m", ""); CHKERRQ(ierr);
  ierr = W.set_attr("valid_min", 0.0); CHKERRQ(ierr);

  // auxiliary variables which NEED ghosts
  ierr = Wstag.create(grid, "W_staggered", true, 1); CHKERRQ(ierr);
  ierr = Wstag.set_attrs("internal",
                     "cell face-centered (staggered) values of water layer thickness",
                     "m", ""); CHKERRQ(ierr);
  ierr = Wstag.set_attr("valid_min", 0.0); CHKERRQ(ierr);
  ierr = Kstag.create(grid, "K_staggered", true, 1); CHKERRQ(ierr);
  ierr = Kstag.set_attrs("internal",
                     "cell face-centered (staggered) values of nonlinear conductivity",
                     "", ""); CHKERRQ(ierr);
  ierr = Kstag.set_attr("valid_min", 0.0); CHKERRQ(ierr);
  ierr = Qstag.create(grid, "advection_flux", true, 1); CHKERRQ(ierr);
  ierr = Qstag.set_attrs("internal",
                     "cell face-centered (staggered) components of advective subglacial water flux",
                     "m2 s-1", ""); CHKERRQ(ierr);
  ierr = R.create(grid, "potential_workspace", true, 1); CHKERRQ(ierr); // box stencil used
  ierr = R.set_attrs("internal",
                      "work space for modeled subglacial water hydraulic potential",
                      "Pa", ""); CHKERRQ(ierr);

  // auxiliary variables which do not need ghosts
  ierr = Pover.create(grid, "overburden_pressure_internal", false); CHKERRQ(ierr);
  ierr = Pover.set_attrs("internal",
                     "overburden pressure",
                     "Pa", ""); CHKERRQ(ierr);
  ierr = Pover.set_attr("valid_min", 0.0); CHKERRQ(ierr);
  ierr = V.create(grid, "water_velocity", false); CHKERRQ(ierr);
  ierr = V.set_attrs("internal",
                     "cell face-centered (staggered) components of water velocity in subglacial water layer",
                     "m s-1", ""); CHKERRQ(ierr);

  // temporaries during update; do not need ghosts
  ierr = Wnew.create(grid, "Wnew_internal", false); CHKERRQ(ierr);
  ierr = Wnew.set_attrs("internal",
                     "new thickness of subglacial water layer during update",
                     "m", ""); CHKERRQ(ierr);
  ierr = Wnew.set_attr("valid_min", 0.0); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode PISMRoutingHydrology::init(PISMVars &vars) {
  PetscErrorCode ierr;
  ierr = verbPrintf(2, grid.com,
    "* Initializing the routing subglacial hydrology model ...\n"); CHKERRQ(ierr);
  // initialize water layer thickness from the context if present,
  //   otherwise from -i or -boot_file, otherwise with constant value
  bool i, bootstrap, stripset;
  ierr = PetscOptionsBegin(grid.com, "",
            "Options controlling the 'routing' subglacial hydrology model", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsIsSet("-i", "PISM input file", i); CHKERRQ(ierr);
    ierr = PISMOptionsIsSet("-boot_file", "PISM bootstrapping file",
                            bootstrap); CHKERRQ(ierr);
    ierr = PISMOptionsIsSet("-report_mass_accounting",
      "Report to stdout on mass accounting in hydrology models", report_mass_accounting); CHKERRQ(ierr);
    ierr = PISMOptionsReal("-hydrology_null_strip",
                           "set the width, in km, of the strip around the edge of the computational domain in which hydrology is inactivated",
                           stripwidth,stripset); CHKERRQ(ierr);
    if (stripset) stripwidth *= 1.0e3;
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  ierr = PISMHydrology::init(vars); CHKERRQ(ierr);

  ierr = init_bwat(vars,i,bootstrap); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PISMRoutingHydrology::init_bwat(PISMVars &vars, bool i_set, bool bootstrap_set) {
  PetscErrorCode ierr;

  IceModelVec2S *W_input = dynamic_cast<IceModelVec2S*>(vars.get("bwat"));
  if (W_input != NULL) { // a variable called "bwat" is already in context
    ierr = W.copy_from(*W_input); CHKERRQ(ierr);
  } else if (i_set || bootstrap_set) {
    string filename;
    int start;
    ierr = find_pism_input(filename, bootstrap_set, start); CHKERRQ(ierr);
    if (i_set) {
      ierr = W.read(filename, start); CHKERRQ(ierr);
    } else {
      ierr = W.regrid(filename,
                      config.get("bootstrapping_bwat_value_no_var")); CHKERRQ(ierr);
    }
  } else {
    ierr = W.set(config.get("bootstrapping_bwat_value_no_var")); CHKERRQ(ierr);
  }

  // however we initialized it, we could be asked to regrid from file
  ierr = regrid(W); CHKERRQ(ierr);

FIXME remove:
  // add bwat to the variables in the context if it is not already there
  if (vars.get("bwat") == NULL) {
    ierr = vars.add(W); CHKERRQ(ierr);
  }
  return 0;
}


void PISMRoutingHydrology::add_vars_to_output(string /*keyword*/, set<string> &result) {
FIXME: call base
  result.insert("bwat");
}


PetscErrorCode PISMRoutingHydrology::define_variables(set<string> vars, const PIO &nc,
                                                 PISM_IO_Type nctype) {
FIXME: call base
  PetscErrorCode ierr;
  if (set_contains(vars, "bwat")) {
    ierr = W.define(nc, nctype); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode PISMRoutingHydrology::write_variables(set<string> vars, const PIO &nc) {
FIXME: call base
  PetscErrorCode ierr;
  if (set_contains(vars, "bwat")) {
    ierr = W.write(nc); CHKERRQ(ierr);
  }
  return 0;
}


void PISMRoutingHydrology::get_diagnostics(map<string, PISMDiagnostic*> &dict) {
  dict["bwp"] = new PISMHydrology_bwp(this, grid, *variables);
  dict["bwprel"] = new PISMHydrology_bwprel(this, grid, *variables);
  dict["effbwp"] = new PISMHydrology_effbwp(this, grid, *variables);
  dict["tillwp"] = new PISMHydrology_tillwp(this, grid, *variables);
  dict["enwat"] = new PISMHydrology_enwat(this, grid, *variables);
  dict["hydroinput"] = new PISMHydrology_hydroinput(this, grid, *variables);
  dict["wallmelt"] = new PISMHydrology_wallmelt(this, grid, *variables);
  dict["bwatvel"] = new PISMRoutingHydrology_bwatvel(this, grid, *variables);
}


//! Check W >= 0 and fails with message if not satisfied.
PetscErrorCode PISMRoutingHydrology::check_W_nonnegative() {
  PetscErrorCode ierr;
  ierr = W.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (W(i,j) < 0.0) {
        PetscPrintf(grid.com,
           "PISMRoutingHydrology ERROR: disallowed negative subglacial water layer thickness (bwat)\n"
           "    W(i,j) = %.6f m at (i,j)=(%d,%d)\n"
           "ENDING ... \n\n", W(i,j),i,j);
        PISMEnd();
      }
    }
  }
  ierr = W.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Correct the new water thickness based on boundary requirements.  Do mass accounting.
/*!
At ice free locations and ocean locations we require that the water thickness
is zero at the end of each time step.  Also we require that any negative water
thicknesses be set to zero (i.e. projection to enforce \f$W\ge 0\f$).

This method takes care of these requirements by altering Wnew appropriately.

We report accounts for these mass changes.
 */
PetscErrorCode PISMRoutingHydrology::boundary_mass_changes(
            IceModelVec2S &myWnew,
            PetscReal &icefreelost, PetscReal &oceanlost,
            PetscReal &negativegain, PetscReal &nullstriplost) {
  PetscErrorCode ierr;
  PetscReal fresh_water_density = config.get("fresh_water_density");
  PetscReal my_icefreelost = 0.0, my_oceanlost = 0.0, my_negativegain = 0.0;
  MaskQuery M(*mask);
  ierr = Wnew.begin_access(); CHKERRQ(ierr);
  ierr = mask->begin_access(); CHKERRQ(ierr);
  ierr = cellarea->begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscReal dmassdz = (*cellarea)(i,j) * fresh_water_density; // kg m-1
      if (Wnew(i,j) < 0.0) {
        my_negativegain += -Wnew(i,j) * dmassdz;
        Wnew(i,j) = 0.0;
      }
      if (M.ice_free_land(i,j) && (Wnew(i,j) > 0.0)) {
        my_icefreelost += Wnew(i,j) * dmassdz;
        Wnew(i,j) = 0.0;
      }
      if (M.ocean(i,j) && (Wnew(i,j) > 0.0)) {
        my_oceanlost += Wnew(i,j) * dmassdz;
        Wnew(i,j) = 0.0;
      }
    }
  }
  ierr = Wnew.end_access(); CHKERRQ(ierr);
  ierr = mask->end_access(); CHKERRQ(ierr);
  ierr = cellarea->end_access(); CHKERRQ(ierr);

  // make global over all proc domains (i.e. whole glacier/ice sheet)
  ierr = PISMGlobalSum(&my_icefreelost, &icefreelost, grid.com); CHKERRQ(ierr);
  ierr = PISMGlobalSum(&my_oceanlost, &oceanlost, grid.com); CHKERRQ(ierr);
  ierr = PISMGlobalSum(&my_negativegain, &negativegain, grid.com); CHKERRQ(ierr);

  // this reporting is redundant for the simpler models but shows short time step
  // reporting for nontrivially-distributed (possibly adaptive) hydrology models
  ierr = verbPrintf(4, grid.com,
    "  mass losses in hydrology time step:\n"
    "     land margin loss = %.3e kg, ocean margin loss = %.3e kg, (W<0) gain = %.3e kg\n",
    icefreelost, oceanlost, negativegain); CHKERRQ(ierr);

  if (stripwidth <= 0.0) {
    nullstriplost = 0.0;
    return 0;
  }

  PetscReal my_nullstriplost = 0.0;

  ierr = myWnew.begin_access(); CHKERRQ(ierr);
  ierr = cellarea->begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const PetscReal dmassdz = (*cellarea)(i,j) * fresh_water_density; // kg m-1
      if (in_null_strip(i,j)) {
        my_nullstriplost += Wnew(i,j) * dmassdz;
        Wnew(i,j) = 0.0;
      }
    }
  }
  ierr = myWnew.end_access(); CHKERRQ(ierr);
  ierr = cellarea->end_access(); CHKERRQ(ierr);

  ierr = PISMGlobalSum(&my_nullstriplost, &nullstriplost, grid.com); CHKERRQ(ierr);
  ierr = verbPrintf(4, grid.com,
    "     null strip loss = %.3e kg\n",nullstriplost); CHKERRQ(ierr);
  return 0;
}


//! Copies the W variable, the modeled water layer thickness.
PetscErrorCode PISMRoutingHydrology::subglacial_water_thickness(IceModelVec2S &result) {
  PetscErrorCode ierr = W.copy_to(result); CHKERRQ(ierr);
  return 0;
}


//! Computes pressure of transportable subglacial water diagnostically as fixed fraction of overburden.
/*!
Here
  \f[ P = \lambda P_o = \lambda (\rho_i g H) \f]
where \f$\lambda\f$=hydrology_pressure_fraction and \f$P_o\f$ is the overburden pressure.
 */
PetscErrorCode PISMRoutingHydrology::subglacial_water_pressure(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = overburden_pressure(result); CHKERRQ(ierr);
  ierr = result.scale(config.get("hydrology_pressure_fraction")); CHKERRQ(ierr);
  return 0;
}


//! Computes water pressure in till by the same rule as in PISMNullTransportHydrology.
/*!
This rule uses only the till water amount, so the pressure of till is mostly
decoupled from the transportable water pressure.
 */
FIXME: PUT IN BASE CLASS?  SHOULD THERE BE MAX ON THIS COMPUTED PRESSURE AND SEPARATE SCALED OVERBURDEN?
PetscErrorCode PISMRoutingHydrology::till_water_pressure(IceModelVec2S &result) {
  PetscErrorCode ierr;

#if (PISM_DEBUG==1)
  ierr = check_Wtil_bounds(); CHKERRQ(ierr);
#endif

  ierr = overburden_pressure(result); CHKERRQ(ierr);

  const PetscReal Wtilmax  = config.get("hydrology_tillwat_max"),
                  lam      = config.get("hydrology_pressure_fraction_till");

  ierr = Wtil.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      result(i,j) = lam * (Wtil(i,j) / Wtilmax) * result(i,j);
    }
  }
  ierr = result.end_access(); CHKERRQ(ierr);
  ierr = Wtil.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Get the hydraulic potential from bedrock topography and current state variables.
/*!
Computes \f$\psi = P + \rho_w g (b + W)\f$ except where floating, where \f$\psi = P_o\f$.
Calls subglacial_water_pressure() method to get water pressure.
 */
PetscErrorCode PISMRoutingHydrology::subglacial_hydraulic_potential(IceModelVec2S &result) {
  PetscErrorCode ierr;

  const PetscReal
    rg = config.get("fresh_water_density") * config.get("standard_gravity");

  ierr = subglacial_water_pressure(result); CHKERRQ(ierr);
  ierr = result.add(rg, (*bed)); CHKERRQ(ierr); // result  <-- P + rhow g b
  ierr = result.add(rg, W); CHKERRQ(ierr);      // result  <-- result + rhow g (b + W)

  // now mask: psi = P_o if ocean
  MaskQuery M(*mask);
  ierr = overburden_pressure(Pover); CHKERRQ(ierr);
  ierr = Pover.begin_access(); CHKERRQ(ierr);
  ierr = mask->begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      if (M.ocean(i,j))
        result(i,j) = Pover(i,j);
    }
  }
  ierr = Pover.end_access(); CHKERRQ(ierr);
  ierr = mask->end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Average the regular grid water thickness to values at the center of cell edges.
PetscErrorCode PISMRoutingHydrology::water_thickness_staggered(IceModelVec2Stag &result) {
  PetscErrorCode ierr;

  ierr = W.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      result(i,j,0) = 0.5 * (W(i,j) + W(i+1,j  ));
      result(i,j,1) = 0.5 * (W(i,j) + W(i  ,j+1));
    }
  }
  ierr = W.end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Compute the nonlinear conductivity at the center of cell edges.
/*!
Computes
    \f[ K = K(W,\nabla P, \nabla b) = k W^{\alpha-1} |\nabla(P+\rho_w g b)|^{\beta-2} \f]
on the staggered grid.  We denote \f$R = P+\rho_w g b\f$ internally.  The quantity
    \f[ \Pi = |\nabla(P+\rho_w g b)|^2 = |\nabla R|^2 \f]
is computed on a staggered grid by a [\ref Mahaffy] -like scheme.  This requires
\f$R\f$ to be defined on a box stencil of width 1.

Also computes the maximum over all staggered points of \f$ K W \f$.
 */
PetscErrorCode PISMRoutingHydrology::conductivity_staggered(
                       IceModelVec2Stag &result, PetscReal &maxKW) {
  PetscErrorCode ierr;
  const PetscReal
    k     = config.get("hydrology_hydraulic_conductivity"),
    alpha = config.get("hydrology_thickness_power_in_flux"),
    beta  = config.get("hydrology_potential_gradient_power_in_flux"),
    rg    = config.get("standard_gravity") * config.get("fresh_water_density");
  if (alpha < 1.0) {
    PetscPrintf(grid.com,
           "PISM ERROR: alpha = %f < 1 which is not allowed\n"
           "ENDING ... \n\n", alpha);
    PISMEnd();
  }

  if (beta == 2.0) {
    ierr = verbPrintf(4, grid.com,
      "    in PISMRoutingHydrology::conductivity_staggered(): "
      "beta == 2.0 exactly; simplifying calculation\n"); CHKERRQ(ierr);
  } else {
    // general case where beta is used; put norm of square gradient temporarily
    //   in result
    ierr = subglacial_water_pressure(R); CHKERRQ(ierr);  // yes, it updates ghosts
    ierr = R.add(rg, (*bed)); CHKERRQ(ierr); // R  <-- P + rhow g b
    ierr = R.update_ghosts(); CHKERRQ(ierr);

    PetscReal dRdx, dRdy;
    ierr = R.begin_access(); CHKERRQ(ierr);
    ierr = result.begin_access(); CHKERRQ(ierr);
    for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
      for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
        dRdx = ( R(i+1,j) - R(i,j) ) / grid.dx;
        dRdy = ( R(i+1,j+1) + R(i,j+1) - R(i+1,j-1) - R(i,j-1) ) / (4.0 * grid.dy);
        result(i,j,0) = dRdx * dRdx + dRdy * dRdy;
        dRdx = ( R(i+1,j+1) + R(i+1,j) - R(i-1,j+1) - R(i-1,j) ) / (4.0 * grid.dx);
        dRdy = ( R(i,j+1) - R(i,j) ) / grid.dy;
        result(i,j,1) = dRdx * dRdx + dRdy * dRdy;
      }
    }
    ierr = R.end_access(); CHKERRQ(ierr);
    ierr = result.end_access(); CHKERRQ(ierr);
  }

  PetscReal mymaxKW = 0.0;
  ierr = R.begin_access(); CHKERRQ(ierr);
  ierr = Wstag.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      for (PetscInt o = 0; o < 2; ++o) {
        if (beta == 2.0)
          result(i,j,o) = k * pow(Wstag(i,j,o),alpha-1.0);
        else {
          if ((result(i,j,o) <= 0.0) && (beta < 2.0)) {
          FIXME:  instead *regularize* |\grad psi|^{beta - 2} ??
            result(i,j,o) = 1000.0 * k;  // FIXME: ad hoc
          } else {
            result(i,j,o) = k * pow(Wstag(i,j,o),alpha-1.0)
                                * pow(result(i,j,o),(beta-2.0)/2.0);
          }
        }
        mymaxKW = PetscMax( mymaxKW, result(i,j,o) * Wstag(i,j,o) );
      }
    }
  }
  ierr = R.end_access(); CHKERRQ(ierr);
  ierr = Wstag.end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);

  ierr = PISMGlobalMax(&mymaxKW, &maxKW, grid.com); CHKERRQ(ierr);

  return 0;
}


//! Compute the wall melt rate which comes from (turbulent) dissipation of flow energy.
/*!
This code fills `result` with
    \f[ \frac{m_{wall}}{\rho_w} = - \frac{1}{L \rho_w} \mathbf{q} \cdot \nabla \psi = \left(\frac{k}{L \rho_w}\right) W^\alpha |\nabla R|^\beta \f]
where \f$R = P+\rho_w g b\f$.

Note that conductivity_staggered() computes the related quantity
\f$K = k W^{\alpha-1} |\nabla R|^{\beta-2}\f$ on the staggered grid, but
contriving to reuse that code would be inefficient because of the
staggered-versus-regular change.

At the current state of the code, this is a diagnostic calculation only.
 */
PetscErrorCode PISMRoutingHydrology::wall_melt(IceModelVec2S &result) {
  PetscErrorCode ierr;

  const PetscReal
    k     = config.get("hydrology_hydraulic_conductivity"),
    L     = config.get("water_latent_heat_fusion"),
    alpha = config.get("hydrology_thickness_power_in_flux"),
    beta  = config.get("hydrology_potential_gradient_power_in_flux"),
    rhow  = config.get("standard_gravity"),
    g     = config.get("fresh_water_density"),
    rg    = rhow * g,
    CC    = k / (L * rhow);

  // FIXME:  could be scaled with overall factor hydrology_coefficient_wall_melt ?
  if (alpha < 1.0) {
    PetscPrintf(grid.com,
           "PISM ERROR: alpha = %f < 1 which is not allowed\n"
           "ENDING ... \n\n", alpha);
    PISMEnd();
  }

  ierr = subglacial_water_pressure(R); CHKERRQ(ierr);  // yes, it updates ghosts
  ierr = R.add(rg, (*bed)); CHKERRQ(ierr); // R  <-- P + rhow g b
  ierr = R.update_ghosts(); CHKERRQ(ierr);

  PetscReal dRdx, dRdy;
  ierr = R.begin_access(); CHKERRQ(ierr);
  ierr = W.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      if (W(i,j) > 0.0) {
        dRdx = 0.0;
        if (W(i+1,j) > 0.0) {
          dRdx =  ( R(i+1,j) - R(i,j) ) / (2.0 * grid.dx);
        }
        if (W(i-1,j) > 0.0) {
          dRdx += ( R(i,j) - R(i-1,j) ) / (2.0 * grid.dx);
        }
        dRdy = 0.0;
        if (W(i,j+1) > 0.0) {
          dRdy =  ( R(i,j+1) - R(i,j) ) / (2.0 * grid.dy);
        }
        if (W(i,j-1) > 0.0) {
          dRdy += ( R(i,j) - R(i,j-1) ) / (2.0 * grid.dy);
        }
        result(i,j) = CC * pow(W(i,j),alpha) * pow(dRdx * dRdx + dRdy * dRdy, beta/2.0);
      } else
        result(i,j) = 0.0;
    }
  }
  ierr = R.end_access(); CHKERRQ(ierr);
  ierr = W.end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);

  return 0;
}


//! Get the advection velocity V at the center of cell edges.
/*!
Computes the advection velocity \f$\mathbf{V}\f$ on the staggered
(edge-centered) grid.  If V = (u,v) in components then we have
<code> result(i,j,0) = u(i+1/2,j) </code> and
<code> result(i,j,1) = v(i,j+1/2) </code>

The advection velocity is given by the formula
  \f[ \mathbf{V} = - K \left(\nabla P + \rho_w g \nabla b\right) \f]
where \f$\mathbf{V}\f$ is the water velocity, \f$P\f$ is the water
pressure, and \f$b\f$ is the bedrock elevation.

If the corresponding staggered grid value of the water thickness is zero then
that component of V is set to zero.  This does not change the flux value (which
would be zero anyway) but it does provide the correct max velocity in the
CFL calculation.  We assume Wstag and Kstag are up-to-date.  We assume P and b
have valid ghosts.

Calls subglacial_water_pressure() method to get water pressure.
 */
PetscErrorCode PISMRoutingHydrology::velocity_staggered(IceModelVec2Stag &result) {
  PetscErrorCode ierr;
  const PetscReal  rg = config.get("standard_gravity") * config.get("fresh_water_density");
  PetscReal dbdx, dbdy, dPdx, dPdy;

  ierr = subglacial_water_pressure(R); CHKERRQ(ierr);  // R=P; yes, it updates ghosts

  ierr = R.begin_access(); CHKERRQ(ierr);
  ierr = Wstag.begin_access(); CHKERRQ(ierr);
  ierr = Kstag.begin_access(); CHKERRQ(ierr);
  ierr = bed->begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      if (Wstag(i,j,0) > 0.0) {
        dPdx = (R(i+1,j) - R(i,j)) / grid.dx;
        dbdx = ((*bed)(i+1,j) - (*bed)(i,j)) / grid.dx;
        result(i,j,0) = - Kstag(i,j,0) * (dPdx + rg * dbdx);
      } else
        result(i,j,0) = 0.0;
      if (Wstag(i,j,1) > 0.0) {
        dPdy = (R(i,j+1) - R(i,j)) / grid.dy;
        dbdy = ((*bed)(i,j+1) - (*bed)(i,j)) / grid.dy;
        result(i,j,1) = - Kstag(i,j,1) * (dPdy + rg * dbdy);
      } else
        result(i,j,1) = 0.0;
      if (in_null_strip(i,j) || in_null_strip(i+1,j))
        result(i,j,0) = 0.0;
      if (in_null_strip(i,j) || in_null_strip(i,j+1))
        result(i,j,1) = 0.0;
    }
  }
  ierr = R.end_access(); CHKERRQ(ierr);
  ierr = Wstag.end_access(); CHKERRQ(ierr);
  ierr = Kstag.end_access(); CHKERRQ(ierr);
  ierr = bed->end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Compute Q = V W at edge-centers (staggered grid) by first-order upwinding.
/*!
The field W must have valid ghost values, but V does not need them.

FIXME:  This could be re-implemented using the Koren (1993) flux-limiter.
 */
PetscErrorCode PISMRoutingHydrology::advective_fluxes(IceModelVec2Stag &result) {
  PetscErrorCode ierr;
  ierr = W.begin_access(); CHKERRQ(ierr);
  ierr = V.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      result(i,j,0) = (V(i,j,0) >= 0.0) ? V(i,j,0) * W(i,j) :  V(i,j,0) * W(i+1,j  );
      result(i,j,1) = (V(i,j,1) >= 0.0) ? V(i,j,1) * W(i,j) :  V(i,j,1) * W(i,  j+1);
    }
  }
  ierr = W.end_access(); CHKERRQ(ierr);
  ierr = V.end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Compute the adaptive time step for evolution of W.
PetscErrorCode PISMRoutingHydrology::adaptive_for_W_evolution(
                  PetscReal t_current, PetscReal t_end, PetscReal maxKW,
                  PetscReal &dt_result,
                  PetscReal &maxV_result, PetscReal &maxD_result,
                  PetscReal &dtCFL_result, PetscReal &dtDIFFW_result) {
  PetscErrorCode ierr;
  const PetscReal
    dtmax = config.get("hydrology_maximum_time_step_years",
                       "years", "seconds"),
    rg    = config.get("standard_gravity") * config.get("fresh_water_density");
  PetscReal tmp[2];
  ierr = V.absmaxcomponents(tmp); CHKERRQ(ierr); // V could be zero if P is constant and bed is flat
  maxV_result = sqrt(tmp[0]*tmp[0] + tmp[1]*tmp[1]);
  maxD_result = rg * maxKW;
  dtCFL_result = 0.5 / (tmp[0]/grid.dx + tmp[1]/grid.dy); // is regularization needed?
  dtDIFFW_result = 1.0/(grid.dx*grid.dx) + 1.0/(grid.dy*grid.dy);
  dtDIFFW_result = 0.25 / (maxD_result * dtDIFFW_result);
  // dt = min { te-t, dtmax, dtCFL, dtDIFFW }
  dt_result = PetscMin(t_end - t_current, dtmax);
  dt_result = PetscMin(dt_result, dtCFL_result);
  dt_result = PetscMin(dt_result, dtDIFFW_result);
  return 0;
}


//! The computation of Wnew, called by update().
PetscErrorCode PISMRoutingHydrology::raw_update_W(PetscReal hdt) {
    PetscErrorCode ierr;
    const PetscReal
      wux  = 1.0 / (grid.dx * grid.dx),
      wuy  = 1.0 / (grid.dy * grid.dy),
      rg   = config.get("standard_gravity") * config.get("fresh_water_density");
    PetscReal divadflux, diffW;

    ierr = W.begin_access(); CHKERRQ(ierr);
    ierr = Wstag.begin_access(); CHKERRQ(ierr);
    ierr = Kstag.begin_access(); CHKERRQ(ierr);
    ierr = Qstag.begin_access(); CHKERRQ(ierr);
    ierr = total_input.begin_access(); CHKERRQ(ierr);
    ierr = Wnew.begin_access(); CHKERRQ(ierr);
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        divadflux =   (Qstag(i,j,0) - Qstag(i-1,j  ,0)) / grid.dx
                    + (Qstag(i,j,1) - Qstag(i,  j-1,1)) / grid.dy;
        const PetscReal  De = rg * Kstag(i,  j,0) * Wstag(i,  j,0),
                         Dw = rg * Kstag(i-1,j,0) * Wstag(i-1,j,0),
                         Dn = rg * Kstag(i,j  ,1) * Wstag(i,j  ,1),
                         Ds = rg * Kstag(i,j-1,1) * Wstag(i,j-1,1);
        diffW =   wux * (  De * (W(i+1,j) - W(i,j)) - Dw * (W(i,j) - W(i-1,j)) )
                + wuy * (  Dn * (W(i,j+1) - W(i,j)) - Ds * (W(i,j) - W(i,j-1)) );
        Wnew(i,j) = W(i,j) + hdt * (- divadflux + diffW + total_input(i,j));
      }
    }
    ierr = W.end_access(); CHKERRQ(ierr);
    ierr = Wstag.end_access(); CHKERRQ(ierr);
    ierr = Kstag.end_access(); CHKERRQ(ierr);
    ierr = Qstag.end_access(); CHKERRQ(ierr);
    ierr = total_input.end_access(); CHKERRQ(ierr);
    ierr = Wnew.end_access(); CHKERRQ(ierr);

    return 0;
}


//! Update the model state variables W and Wtil by applying the subglacial hydrology model equations.
/*!
Runs the hydrology model from time icet to time icet + icedt.  Here [icet,icedt]
is generally on the order of months to years.  This hydrology model will take its
own shorter time steps, perhaps hours to weeks.

For updating W = `bwat`, see raw_update_W().

For updating Wtil, does an implicit (backward Euler) step of the integration
  \f[ \frac{\partial W_{til}}{\partial t} = \mu \left(\min\{\tau W,W_{til}^{max} - W_{til}\right)\f]
where \f$\mu=\f$`hydrology_tillwat_rate`, \f$\tau=\f$`hydrology_tillwat_transfer_proportion`,
and \f$W_{til}^{max}\f$=`hydrology_tillwat_max`.

The solution satisfies the inequalities
  \f[ 0 \le W_{til} \le W_{til}^{max}.\f]

 */
PetscErrorCode PISMRoutingHydrology::update(PetscReal icet, PetscReal icedt) {
  PetscErrorCode ierr;

  // if asked for the identical time interval versus last time, then
  //   do nothing; otherwise assume that [my_t,my_t+my_dt] is the time
  //   interval on which we are solving
  if ((fabs(icet - t) < 1e-12) && (fabs(icedt - dt) < 1e-12))
    return 0;
  // update PISMComponent times: t = current time, t+dt = target time
  t = icet;
  dt = icedt;

  const PetscReal Wtilmax  = config.get("hydrology_tillwat_max"),
                  mu       = config.get("hydrology_tillwat_rate"),
                  tau      = config.get("hydrology_tillwat_transfer_proportion");
  if ((Wtilmax < 0.0) || (mu < 0.0) || (tau < 0.0)) {
    PetscPrintf(grid.com,
       "PISMRoutingHydrology ERROR: one of scalar config parameters for tillwat is negative\n"
       "            this is not allowed\n"
       "ENDING ... \n\n");
    PISMEnd();
  }

  // make sure W has valid ghosts before starting hydrology steps
  ierr = W.update_ghosts(); CHKERRQ(ierr);

  MaskQuery M(*mask);
  PetscReal ht = t, hdt, // hydrology model time and time step
            maxKW, maxV, maxD, dtCFL, dtDIFFW;
  PetscReal icefreelost = 0, oceanlost = 0, negativegain = 0, nullstriplost = 0,
            delta_icefree, delta_ocean, delta_neggain, delta_nullstrip;
  PetscInt hydrocount = 0; // count hydrology time steps

  while (ht < t + dt) {
    hydrocount++;

#if (PISM_DEBUG==1)
    ierr = check_W_nonnegative(); CHKERRQ(ierr);
    ierr = check_Wtil_bounds(); CHKERRQ(ierr);
#endif

    ierr = water_thickness_staggered(Wstag); CHKERRQ(ierr);
    ierr = Wstag.update_ghosts(); CHKERRQ(ierr);

    ierr = conductivity_staggered(Kstag,maxKW); CHKERRQ(ierr);
    ierr = Kstag.update_ghosts(); CHKERRQ(ierr);

    ierr = velocity_staggered(V); CHKERRQ(ierr);

    // to get Qstag, W needs valid ghosts
    ierr = advective_fluxes(Qstag); CHKERRQ(ierr);
    ierr = Qstag.update_ghosts(); CHKERRQ(ierr);

    ierr = adaptive_for_W_evolution(ht, t+dt, maxKW,
                                    hdt, maxV, maxD, dtCFL, dtDIFFW); CHKERRQ(ierr);

    if ((inputtobed != NULL) || (hydrocount==1)) {
      ierr = get_input_rate(ht,hdt,total_input); CHKERRQ(ierr);
    }

    // update Wnew (the actual step) from W, Wstag, Qstag, total_input
    ierr = raw_update_W(hdt); CHKERRQ(ierr);

    ierr = boundary_mass_changes(Wnew, delta_icefree, delta_ocean,
                                 delta_neggain, delta_nullstrip); CHKERRQ(ierr);
    icefreelost  += delta_icefree;
    oceanlost    += delta_ocean;
    negativegain += delta_neggain;
    nullstriplost+= delta_nullstrip;

    // transfer Wnew into W
    ierr = Wnew.update_ghosts(W); CHKERRQ(ierr);

    // update Wtil and W by (possibly) transfering water from bwat; implicit step
    //   with no time-step restriction
    ierr = Wtil.begin_access(); CHKERRQ(ierr);
    ierr = W.begin_access(); CHKERRQ(ierr);
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        const PetscReal
            change = mu * PetscMin(tau * W(i,j), Wtilmax),
            Wtilnew = (Wtil(i,j) + icedt * change) / (1.0 + mu * icedt);
        W(i,j) = W(i,j) - (Wtilnew - Wtil(i,j));
        Wtil(i,j) = Wtilnew;
      }
    }
    ierr = Wtil.end_access(); CHKERRQ(ierr);
    ierr = W.end_access(); CHKERRQ(ierr);

    ht += hdt;
  } // end of hydrology model time-stepping loop

  if (report_mass_accounting) {
    ierr = verbPrintf(2, grid.com,
                      " 'routing' hydrology summary:\n"
                      "     %d hydrology sub-steps with average dt = %.6f years = %.2f s\n"
                      "        (last max |V| = %.2e m s-1; last max D = %.2e m^2 s-1)\n"
                      "     ice free land lost = %.3e kg, ocean lost = %.3e kg\n"
                      "     negative bmelt gain = %.3e kg, null strip lost = %.3e kg\n",
                      hydrocount,
                      grid.convert(dt/hydrocount, "seconds", "years"),
                      dt/hydrocount, maxV, maxD,
                      icefreelost, oceanlost, negativegain, nullstriplost); CHKERRQ(ierr);
  }
  return 0;
}


PISMRoutingHydrology_bwatvel::PISMRoutingHydrology_bwatvel(PISMRoutingHydrology *m, IceGrid &g, PISMVars &my_vars)
    : PISMDiag<PISMRoutingHydrology>(m, g, my_vars) {

  // set metadata:
  dof = 2;
  vars.resize(dof, NCSpatialVariable(g.get_unit_system()));
  vars[0].init_2d("bwatvel[0]", grid);
  vars[1].init_2d("bwatvel[1]", grid);

  set_attrs("velocity of water in subglacial layer, i-offset", "",
            "m s-1", "m year-1", 0);
  set_attrs("velocity of water in subglacial layer, j-offset", "",
            "m s-1", "m year-1", 1);
}


PetscErrorCode PISMRoutingHydrology_bwatvel::compute(IceModelVec* &output) {
  PetscErrorCode ierr;

  IceModelVec2Stag *result = new IceModelVec2Stag;
  ierr = result->create(grid, "bwatvel", true); CHKERRQ(ierr);
  ierr = result->set_metadata(vars[0], 0); CHKERRQ(ierr);
  ierr = result->set_metadata(vars[1], 1); CHKERRQ(ierr);
  result->write_in_glaciological_units = true;

  ierr = model->velocity_staggered(*result); CHKERRQ(ierr);

  output = result;
  return 0;
}
