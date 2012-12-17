// Copyright (C) 2012 PISM Authors
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

#include "PISMHydrology.hh"
#include "PISMVars.hh"
#include "pism_options.hh"
#include "Mask.hh"


PetscErrorCode PISMHydrology::init(PISMVars &vars) {
  PetscErrorCode ierr;
  ierr = verbPrintf(4, grid.com,
    "entering initializer for base class PISMHydrology ...\n"); CHKERRQ(ierr);

  variables = &vars;

  thk = dynamic_cast<IceModelVec2S*>(vars.get("thk"));
  if (thk == NULL) SETERRQ(grid.com, 1, "thk is not available to PISMHydrology");

  bed = dynamic_cast<IceModelVec2S*>(vars.get("topg"));
  if (bed == NULL) SETERRQ(grid.com, 1, "topg is not available to PISMHydrology");

  bmelt = dynamic_cast<IceModelVec2S*>(vars.get("bmelt"));
  if (bmelt == NULL) SETERRQ(grid.com, 1, "bmelt is not available to PISMHydrology");

  cellarea = dynamic_cast<IceModelVec2S*>(vars.get("cell_area"));
  if (cellarea == NULL) SETERRQ(grid.com, 1, "cell_area is not available to PISMHydrology");

  mask = dynamic_cast<IceModelVec2Int*>(vars.get("mask"));
  if (mask == NULL) SETERRQ(grid.com, 1, "mask is not available to PISMHydrology");

  return 0;
}


void PISMHydrology::get_diagnostics(map<string, PISMDiagnostic*> &dict) {
  dict["bwp"] = new PISMHydrology_bwp(this, grid, *variables);
}


PetscErrorCode PISMHydrology::regrid(IceModelVec2S &myvar) {
  PetscErrorCode ierr;
  bool regrid_file_set, regrid_vars_set;
  string regrid_file;
  vector<string> regrid_vars;

  ierr = PetscOptionsBegin(grid.com, "", "PISMHydrology regridding options", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsString("-regrid_file", "regridding file name",
                             regrid_file, regrid_file_set); CHKERRQ(ierr);
    ierr = PISMOptionsStringArray("-regrid_vars", "comma-separated list of regridding variables",
                                  "", regrid_vars, regrid_vars_set); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  // stop if the user did not ask to regrid at all
  if (!regrid_file_set)
    return 0;
  // stop if the user did not ask to regrid myvar
  set<string> vars;
  for (unsigned int i = 0; i < regrid_vars.size(); ++i)
    vars.insert(regrid_vars[i]);
  if (!set_contains(vars, myvar.string_attr("short_name")))
    return 0;
  // otherwise, actually regrid
  ierr = verbPrintf(2, grid.com, "  regridding '%s' from file '%s' ...\n",
                    myvar.string_attr("short_name").c_str(), regrid_file.c_str()); CHKERRQ(ierr);
  ierr = myvar.regrid(regrid_file, true); CHKERRQ(ierr);
  return 0;
}


//! Update the overburden pressure from ice thickness.
/*!
Uses the standard hydrostatic (shallow) approximation of overburden pressure,
  \f[ P_0 = \rho_i g H \f]
Accesses H=thk from PISMVars, which points into IceModel.
 */
PetscErrorCode PISMHydrology::overburden_pressure(IceModelVec2S &result) {
  PetscErrorCode ierr;
  // FIXME issue #15
  ierr = result.copy_from(*thk); CHKERRQ(ierr);
  ierr = result.scale(config.get("ice_density") * config.get("standard_gravity")); CHKERRQ(ierr);
  return 0;
}


//! Compute the water input rate into the basal hydrology layer according to configuration and mask.
/*!
This method crops the (energy-conservation and sub-shelf-melt-coupler
computed) basal melt rate to the ice-covered region.  It also reads the
\c -hydrology_use_const_bmelt option.

Note that the input rate is (for now) assumed to be constant in time \e during the
PISMHydrology::update() actions.

(This method could, potentially, add separate en- and supra-glacial drainage
contributions to the basal melt rates computed at the lower surface of the ice.)
 */
PetscErrorCode PISMHydrology::get_input_rate(IceModelVec2S &result) {
  PetscErrorCode ierr;
  bool      use_const   = config.get_flag("hydrology_use_const_bmelt");
  PetscReal const_bmelt = config.get("hydrology_const_bmelt");
  ierr = bmelt->begin_access(); CHKERRQ(ierr);
  ierr = mask->begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  MaskQuery m(*mask);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (m.icy(i, j))
        result(i,j) = (use_const) ? const_bmelt : (*bmelt)(i,j);
      else
        result(i,j) = 0.0;
    }
  }
  ierr = bmelt->end_access(); CHKERRQ(ierr);
  ierr = mask->end_access(); CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Update the water thickness based on boundary requirements.  Do mass accounting.
/*!
At ice free locations and ocean locations we require that the water thickness
is zero at the end of each time step.  Also we require that any negative water
thicknesses be set to zero (i.e. projection to enforce \f$W\ge 0\f$).

This method takes care of these requirements by altering Wnew appropriately.
And we account for the mass changes that these alterations represent.
 */
PetscErrorCode PISMHydrology::boundary_mass_changes(IceModelVec2S &Wnew,
            PetscReal &icefreelost, PetscReal &oceanlost, PetscReal &negativegain) {
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
      if  (Wnew(i,j) > 0.0) {
        if (M.ice_free_land(i,j)) {
          my_icefreelost += Wnew(i,j) * dmassdz;
          Wnew(i,j) = 0.0;
        } else if (M.ocean(i,j)) {
          my_oceanlost += Wnew(i,j) * dmassdz;
          Wnew(i,j) = 0.0;
        }
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
    "mass losses in hydrology time step:\n"
    "   land margin loss = %.3e kg, ocean margin loss = %.3e kg, (W<0) gain = %.3e kg\n",
    icefreelost, oceanlost, negativegain); CHKERRQ(ierr);
  return 0;
}


PISMTillCanHydrology::PISMTillCanHydrology(IceGrid &g, const NCConfigVariable &conf,
                                           bool Whasghosts)
    : PISMHydrology(g, conf)
{
  if (allocate(Whasghosts) != 0) {
    PetscPrintf(grid.com,
      "PISM ERROR: allocation failed in PISMTillCanHydrology constructor.\n");
    PISMEnd();
  }
}


PetscErrorCode PISMTillCanHydrology::allocate(bool Whasghosts) {
  PetscErrorCode ierr;
  // workspace
  ierr = input.create(grid, "input_hydro", false); CHKERRQ(ierr);
  ierr = input.set_attrs("internal",
                         "workspace for input into subglacial water layer",
                         "m s-1", ""); CHKERRQ(ierr);
  // model state variables
  if (Whasghosts) {
    ierr = W.create(grid, "bwat", true, 1); CHKERRQ(ierr);
  } else {
    ierr = W.create(grid, "bwat", false); CHKERRQ(ierr);
  }
  ierr = W.set_attrs("model_state",
                     "thickness of subglacial water layer",
                     "m", ""); CHKERRQ(ierr);
  ierr = W.set_attr("valid_min", 0.0); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PISMTillCanHydrology::init(PISMVars &vars) {
  PetscErrorCode ierr;
  ierr = verbPrintf(2, grid.com,
    "* Initializing the 'tillcan' subglacial hydrology model...\n"); CHKERRQ(ierr);
  ierr = PISMHydrology::init(vars); CHKERRQ(ierr);

  // initialize water layer thickness from the context if present,
  //   otherwise from -i or -boot_file, otherwise with constant value
  bool i_set, bootstrap;
  string filename;
  int start;
  ierr = PetscOptionsBegin(grid.com, "",
            "Options controlling the 'tillcan' subglacial hydrology model", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsIsSet("-i", "PISM input file", i_set); CHKERRQ(ierr);
    ierr = PISMOptionsIsSet("-boot_file", "PISM bootstrapping file",
                            bootstrap); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);
  IceModelVec2S *W_input = dynamic_cast<IceModelVec2S*>(vars.get("bwat"));
  if (W_input != NULL) { // a variable called "bwat" is already in context
    ierr = W.copy_from(*W_input); CHKERRQ(ierr);
  } else if (i_set || bootstrap) {
    ierr = find_pism_input(filename, bootstrap, start); CHKERRQ(ierr);
    if (i_set) {
      ierr = W.read(filename, start); CHKERRQ(ierr);
    } else {
      ierr = W.regrid(filename,
                      config.get("bootstrapping_bwat_value_no_var")); CHKERRQ(ierr);
    }
  } else {
    ierr = W.set(config.get("bootstrapping_bwat_value_no_var")); CHKERRQ(ierr);
  }

  // whether or not we could initialize from file, we could be asked to regrid from file
  ierr = regrid(W); CHKERRQ(ierr);

  // add bwat to the variables in the context if it is not already there
  if (vars.get("bwat") == NULL) {
    ierr = vars.add(W); CHKERRQ(ierr);
  }
  return 0;
}


void PISMTillCanHydrology::add_vars_to_output(string /*keyword*/, map<string,NCSpatialVariable> &result) {
  result["bwat"] = W.get_metadata();
}


PetscErrorCode PISMTillCanHydrology::define_variables(set<string> vars, const PIO &nc,
                                                 PISM_IO_Type nctype) {
  PetscErrorCode ierr;
  if (set_contains(vars, "bwat")) {
    ierr = W.define(nc, nctype); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode PISMTillCanHydrology::write_variables(set<string> vars, const PIO &nc) {
  PetscErrorCode ierr;
  if (set_contains(vars, "bwat")) {
    ierr = W.write(nc); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode PISMTillCanHydrology::water_layer_thickness(IceModelVec2S &result) {
  PetscErrorCode ierr = W.copy_to(result); CHKERRQ(ierr);
  return 0;
}


//! Computes pressure diagnostically.
/*!
  \f[ P = \lambda P_o \max\{1,W / W_{crit}\} \f]
where \f$\lambda\f$=till_pw_fraction, \f$P_o = \rho_i g H\f$, \f$W_{crit}\f$=hydrology_bwat_max.
 */
PetscErrorCode PISMTillCanHydrology::water_pressure(IceModelVec2S &result) {
  PetscErrorCode ierr;

#if (PISM_DEBUG==1)
  ierr = check_W_bounds(); CHKERRQ(ierr); // check:  W \le bwat_max = Wcrit
#endif

  ierr = overburden_pressure(result); CHKERRQ(ierr);

  double bwat_max = config.get("hydrology_bwat_max"),
    till_pw_fraction = config.get("till_pw_fraction");

  ierr = W.begin_access(); CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (PetscInt   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys+grid.ym; ++j) {
      // P = lambda (W/W_0) P_o
      result(i,j) = till_pw_fraction * (W(i,j) / bwat_max) * result(i,j);
    }
  }
  ierr = result.end_access(); CHKERRQ(ierr);
  ierr = W.end_access(); CHKERRQ(ierr);
  return 0;
}


/*!
Checks \f$0 \le W \le W_{crit} =\f$hydrology_bwat_max.
 */
PetscErrorCode PISMTillCanHydrology::check_W_bounds() {
  PetscErrorCode ierr;
  PetscReal bwat_max = config.get("hydrology_bwat_max");
  ierr = W.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (W(i,j) < 0.0) {
        PetscPrintf(grid.com,
           "PISMTillCanHydrology ERROR: disallowed negative subglacial water layer thickness W(i,j) = %.6f m\n"
           "            at (i,j)=(%d,%d)\n"
           "ENDING ... \n\n", W(i,j),i,j);
        PISMEnd();
      }
      if (W(i,j) > bwat_max) {
        PetscPrintf(grid.com,
           "PISMTillCanHydrology ERROR: subglacial water layer thickness W(i,j) = %.6f m exceeds\n"
           "            hydrology_bwat_max = %.6f at (i,j)=(%d,%d)\n"
           "ENDING ... \n\n", W(i,j),bwat_max,i,j);
        PISMEnd();
      }
    }
  }
  ierr = W.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Update the water thickness from input (melt and drainage from ice above), the upper bound on water amount, and the decay rate.
/*!
Solves on explicit (forward Euler) step of the integration
  \f[ \frac{dW}{dt} = \text{bmelt} - C \f]
but subject to the inequalities
  \f[ 0 \le W \le W_0 \f]
where \f$C=\f$hydrology_bwat_decay_rate and \f$W_0\f$=hydrology_bwat_max.
 */
PetscErrorCode PISMTillCanHydrology::update(PetscReal icet, PetscReal icedt) {
  // if asked for the identical time interval as last time, then do nothing
  if ((fabs(icet - t) < 1e-6) && (fabs(icedt - dt) < 1e-6))
    return 0;
  t = icet;
  dt = icedt;

  PetscErrorCode ierr;

  ierr = get_input_rate(input); CHKERRQ(ierr);

  PetscReal bwat_max        = config.get("hydrology_bwat_max"),
            bwat_decay_rate = config.get("hydrology_bwat_decay_rate");
  PetscReal icefreelost = 0, oceanlost = 0, negativegain = 0;

  ierr = W.begin_access(); CHKERRQ(ierr);
  ierr = input.begin_access(); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      W(i,j) = pointwise_update(W(i,j), input(i,j) * icedt, bwat_decay_rate * icedt, bwat_max);
    }
  }
  ierr = W.end_access(); CHKERRQ(ierr);
  ierr = input.end_access(); CHKERRQ(ierr);

  // following should *not* alter W, and it should report all zeros by design;
  // this hydrology is *not* distributed
  ierr = boundary_mass_changes(W,icefreelost,oceanlost,negativegain); CHKERRQ(ierr);
  ierr = verbPrintf(2, grid.com,
    " 'tillcan' hydrology mass losses:\n"
    "     ice free land lost = %.3e kg, ocean lost = %.3e kg, negative bmelt gain = %.3e kg\n",
    icefreelost, oceanlost, negativegain); CHKERRQ(ierr);
  return 0;
}


PISMDiffuseOnlyHydrology::PISMDiffuseOnlyHydrology(IceGrid &g, const NCConfigVariable &conf)
    : PISMTillCanHydrology(g, conf, true)
{
  if (allocateWnew() != 0) {
    PetscPrintf(grid.com,
      "PISM ERROR: allocation of Wnew failed in PISMDiffuseOnlyHydrology constructor.\n");
    PISMEnd();
  }
}


PetscErrorCode PISMDiffuseOnlyHydrology::allocateWnew() {
  PetscErrorCode ierr;
  // also need temporary space during update
  ierr = Wnew.create(grid, "Wnew-internal", false); CHKERRQ(ierr);
  ierr = Wnew.set_attrs("internal",
                     "new thickness of subglacial water layer during update",
                     "m", ""); CHKERRQ(ierr);
  ierr = Wnew.set_attr("valid_min", 0.0); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PISMDiffuseOnlyHydrology::init(PISMVars &vars) {
  PetscErrorCode ierr;
  ierr = PISMTillCanHydrology::init(vars); CHKERRQ(ierr);
  ierr = verbPrintf(2, grid.com,
    "  using the diffusive water layer variant ...\n"); CHKERRQ(ierr);
  return 0;
}


//! Explicit time step for diffusion of subglacial water layer bwat.
/*!
This model adds a contrived lateral diffusion to the PISMTillCanHydrology
model.  See equation (11) in \ref BBssasliding , namely
  \f[W_t = K \nabla^2 W.\f]
The diffusion constant \f$K\f$ is chosen so that the fundamental solution (Green's
function) of this equation has standard deviation \f$\sigma=L\f$ at time t=\c diffusion_time.
Note that \f$2 \sigma^2 = 4 K t\f$.

The time step restriction for the explicit method for this equation is believed
to be so rare that if it is triggered there is a stdout warning.
 */
PetscErrorCode PISMDiffuseOnlyHydrology::update(PetscReal icet, PetscReal icedt) {
  // if asked for the identical time interval as last time, then do nothing
  if ((fabs(icet - t) < 1e-6) && (fabs(icedt - dt) < 1e-6))
    return 0;
  t = icet;
  dt = icedt;

  PetscErrorCode ierr;
  const PetscReal L = config.get("hydrology_bwat_diffusion_distance");
  if (L <= 0.0)  {
    ierr = PISMTillCanHydrology::update(icet,icedt); CHKERRQ(ierr);
    return 0;
  }

  const PetscReal
    diffusion_time  = config.get("hydrology_bwat_diffusion_time", "years", "seconds"), // convert to seconds
    bwat_max        = config.get("hydrology_bwat_max"),
    bwat_decay_rate = config.get("hydrology_bwat_decay_rate"),
    K               = L * L / (2.0 * diffusion_time);

  PetscReal hdt;
  PetscInt NN;
  hdt = (1.0 / (grid.dx*grid.dx)) + (1.0 / (grid.dy*grid.dy));
  hdt = 1.0 / (2.0 * K * hdt);
  NN = int(ceil(dt / hdt));
  hdt = dt / NN;
  if (NN > 1) {
    verbPrintf(2,grid.com,
      "PISMDiffuseOnlyHydrology WARNING: more than one time step per ice dynamics time step\n"
      "   ... NN = %d > 1 ... THIS IS BELIEVED TO BE RARE\n",NN);
  }

  ierr = get_input_rate(input); CHKERRQ(ierr);

  PetscReal icefreelost = 0, oceanlost = 0, negativegain = 0;

  PetscReal  Rx = K * hdt / (grid.dx * grid.dx),
             Ry = K * hdt / (grid.dy * grid.dy),
             oneM4R = 1.0 - 2.0 * Rx - 2.0 * Ry;
  for (PetscInt n=0; n<NN; ++n) {
    // time-splitting: first, Euler step on source terms
    ierr = W.begin_access(); CHKERRQ(ierr);
    ierr = input.begin_access(); CHKERRQ(ierr);
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        W(i,j) = pointwise_update(W(i,j), input(i,j) * icedt, bwat_decay_rate * icedt, bwat_max);
      }
    }
    ierr = W.end_access(); CHKERRQ(ierr);
    ierr = input.end_access(); CHKERRQ(ierr);

    // valid ghosts for diffusion below
    ierr = W.beginGhostComm(); CHKERRQ(ierr);
    ierr = W.endGhostComm(); CHKERRQ(ierr);

    // time-splitting: second, diffusion by first-order explicit
    ierr = W.begin_access(); CHKERRQ(ierr);
    ierr = Wnew.begin_access(); CHKERRQ(ierr);
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        Wnew(i,j) = oneM4R * W(i,j) + Rx * (W(i+1,j  ) + W(i-1,j  ))
                                    + Ry * (W(i  ,j+1) + W(i  ,j-1));
        // no check of upper bound here because maximum principle applies to step
      }
    }
    ierr = W.end_access(); CHKERRQ(ierr);
    ierr = Wnew.end_access(); CHKERRQ(ierr);

    ierr = boundary_mass_changes(Wnew,icefreelost,oceanlost,negativegain); CHKERRQ(ierr);
    ierr = verbPrintf(2, grid.com,
      " 'diffuseonly' hydrology mass losses:\n"
      "     ice free land lost = %.3e kg, ocean lost = %.3e kg, negative bmelt gain = %.3e kg\n",
      icefreelost, oceanlost, negativegain); CHKERRQ(ierr);

    ierr = Wnew.beginGhostComm(W); CHKERRQ(ierr);
    ierr = Wnew.endGhostComm(W); CHKERRQ(ierr);
  }
  return 0;
}


PISMHydrology_bwp::PISMHydrology_bwp(PISMHydrology *m, IceGrid &g, PISMVars &my_vars)
  : PISMDiag<PISMHydrology>(m, g, my_vars) {

  // set metadata:
  vars[0].init_2d("bwp", grid);

  set_attrs("pressure of water in subglacial layer", "",
            "Pa", "Pa", 0);
}


PetscErrorCode PISMHydrology_bwp::compute(IceModelVec* &output) {
  PetscErrorCode ierr;

  IceModelVec2S *result = new IceModelVec2S;
  ierr = result->create(grid, "bwp", false); CHKERRQ(ierr);
  ierr = result->set_metadata(vars[0], 0); CHKERRQ(ierr);
  result->write_in_glaciological_units = true;

  ierr = model->water_pressure(*result); CHKERRQ(ierr);

  output = result;
  return 0;
}

