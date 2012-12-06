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

#ifndef _PISMHYDROLOGY_H_
#define _PISMHYDROLOGY_H_

#include "iceModelVec.hh"
#include "PISMComponent.hh"
#include "PISMStressBalance.hh"

//! \brief The PISM subglacial hydrology model interface.
/*!
This is a virtual base class.  The PISM default model is a derived class:
PISMTillCanHydrology.  Greatly-improved but computationally expensive mass-
conserving models are in PISMLakesHydrology and PISMDistributedHydrology.

PISMHydrology is a timestepping component (PISMComponent_TS).  Because of the
short physical timescales associated to liquid water moving under a glacier,
PISMHydrology derived classes will generally not use PISM's main ice dynamics
time steps.  Instead, when PISMHydrology::update() is called it advances its
internal time to the new goal t+dt using its own internal time steps.

Generally these subglacial hydrology models will use the ice geometry and/or
the basal sliding velocity.  These ice fields are normally treated as
time-independent during the update() call for the interval [t,t+dt].  Said
another way, the coupling is one-way during the update() call.  The frequency
with which the coupling becomes two-way is determined by the agent that calls
the update() method, which is generally IceModel.
 */
class PISMHydrology : public PISMComponent_TS {
public:
  PISMHydrology(IceGrid &g, const NCConfigVariable &conf) : PISMComponent_TS(g, conf) {}
  virtual ~PISMHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars) = 0;

  virtual void add_vars_to_output(string keyword, map<string,NCSpatialVariable> &result) = 0;
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype) = 0;
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc) = 0;

  using PISMComponent_TS::update;
  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt) = 0;

  virtual PetscErrorCode water_layer_thickness(IceModelVec2S &result) = 0;
  virtual PetscErrorCode water_pressure(IceModelVec2S &result) = 0;
};


//! \brief The subglacial hydrology model from Bueler & Brown (2009) but without contrived water diffusion.
/*!
The name "till-can" comes from the following mental image:  Each map-plane cell
under the glacier or ice sheet does not communicate with the next cell; i.e.
there are "can walls" separating the cells.  The cans are "open-topped" in the
sense that they fill up to level bwat_max.  Any water exceeding bwat_max "spills
over the sides" and disappears.  Thus this model is not mass conserving, but it
is useful for computing a till yield stress based on a time-integrated basal
melt rate.

The paper [\ref BBssasliding] used a model with contrived diffusion
in the basal layer.  It is implemented in the derived class PISMDiffusebwat.

See [\ref BBssasliding] and [\ref Tulaczyketal2000b].  See this URL for a talk
where the "till-can" metaphor is illustrated:  http://www2.gi.alaska.edu/snowice/glaciers/iceflow/bueler-igs-fairbanks-june2012.pdf
 */
class PISMTillCanHydrology : public PISMHydrology {
public:
  PISMTillCanHydrology(IceGrid &g, const NCConfigVariable &conf, bool Whasghosts);
  virtual ~PISMTillCanHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual void add_vars_to_output(string keyword, map<string,NCSpatialVariable> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

  virtual PetscErrorCode water_layer_thickness(IceModelVec2S &result);
  virtual PetscErrorCode water_pressure(IceModelVec2S &result);

protected:
  // this model's state
  IceModelVec2S W;      // water layer thickness

  // pointers into IceModel; these describe the ice sheet and the source
  IceModelVec2S *thk,   // ice thickness
                *bmelt; // ice sheet basal melt rate
  IceModelVec2Int *mask;// floating, grounded, etc. mask

  virtual PetscErrorCode allocate(bool Whasghosts);

  virtual PetscErrorCode check_W_bounds();
};


//! \brief The subglacial hydrology model from Bueler & Brown (2009) WITH the contrived water diffusion.
/*!
Implements the full model in [\ref BBssasliding], including the diffusion which
is equation (11).
 */
class PISMDiffusebwatHydrology : public PISMTillCanHydrology {
public:
  PISMDiffusebwatHydrology(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PISMDiffusebwatHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

protected:
  IceModelVec2S Wnew;      // water layer thickness, temporary during update
  virtual PetscErrorCode allocateWnew();
};



//! \brief The PISM subglacial hydrology model for a distributed linked-cavity system.
/*!
This implements the new van Pelt & Bueler model documented at the repo (currently
private):
  https://github.com/bueler/hydrolakes
 */
class PISMDistributedHydrology : public PISMHydrology {
public:
  PISMDistributedHydrology(IceGrid &g, const NCConfigVariable &conf, PISMStressBalance *sb);
  virtual ~PISMDistributedHydrology() {}

  virtual PetscErrorCode init(PISMVars &vars);

  virtual void add_vars_to_output(string keyword, map<string,NCSpatialVariable> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const PIO &nc,PISM_IO_Type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, const PIO &nc);

  virtual PetscErrorCode update(PetscReal icet, PetscReal icedt);

  virtual PetscErrorCode water_layer_thickness(IceModelVec2S &result);
  virtual PetscErrorCode water_pressure(IceModelVec2S &result);

protected:
  // this model's state
  IceModelVec2S W,      // water layer thickness
                P;      // water pressure
  // this model's auxiliary variables
  IceModelVec2S Po,     // overburden pressure
                cbase,  // sliding speed of overlying ice
                psi;    // hydraulic potential
  IceModelVec2Int known;// mask for (boundary) locations where subglacial hydrology state is known
  IceModelVec2Stag V,   // components are
                        //   V(i,j,0) = alpha(i,j) = east-edge centered  x-component of water velocity
                        //   V(i,j,1) = beta(i,j)  = north-edge centered y-component of water velocity
                   Wstag,// edge-centered (staggered) W values (averaged from regular)
                   Qstag;// edge-centered (staggered) advection fluxes
  // this model's workspace variables
  IceModelVec2S Wnew, Pnew;
  // pointers into IceModel; these describe the ice sheet and the source
  IceModelVec2S *bed,   // bedrock elevation
                *thk,   // ice thickness
                *usurf, // ice surface elevation
                *bmelt; // ice sheet basal melt rate

  PISMStressBalance* stressbalance;

  PetscReal standard_gravity, ice_density, fresh_water_density, sea_water_density;
  PetscReal c1, c2, K, Aglen, nglen, Wr, c0, E0, Y0;

  virtual PetscErrorCode allocate();

  virtual PetscErrorCode check_bounds();
  virtual PetscErrorCode P_from_W_steady(IceModelVec2S &result);
  virtual PetscErrorCode velocity_staggered(IceModelVec2Stag &result);
  virtual PetscErrorCode water_thickness_staggered(IceModelVec2Stag &result);
  virtual PetscErrorCode advective_fluxes(IceModelVec2Stag &result);
  virtual PetscErrorCode hydraulic_potential(IceModelVec2S &result);
  virtual PetscErrorCode known_state_mask(IceModelVec2Int &result);

  virtual PetscErrorCode update_ice_functions(IceModelVec2S &result_Po, IceModelVec2S &result_cbase);

  virtual PetscErrorCode adaptive_time_step(PetscReal t_current, PetscReal t_end, 
                                            PetscReal &dt_result);
};

#endif /* _PISMHYDROLOGY_H_ */

