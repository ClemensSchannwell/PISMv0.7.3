// Copyright (C) 2008-2011 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
// Gudfinna Adalgeirsdottir and Andy Aschwanden
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

#ifndef __PISMSurfaceModel_hh
#define __PISMSurfaceModel_hh

#include "PISMComponent.hh"
#include "iceModelVec.hh"
#include "PISMAtmosphere.hh"
#include "localMassBalance.hh"

class PISMSurfaceModel : virtual public PISMComponent_TS {
public:
  PISMSurfaceModel(IceGrid &g, const NCConfigVariable &conf)
    : PISMComponent_TS(g, conf)
  { atmosphere = NULL; };

  virtual ~PISMSurfaceModel()
  { delete atmosphere; };

  virtual void get_diagnostics(map<string, PISMDiagnostic*> &dict)
  { if (atmosphere)  atmosphere->get_diagnostics(dict); }

  virtual PetscErrorCode init(PISMVars &vars);
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input);
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result) = 0;
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result) = 0;
  virtual PetscErrorCode ice_surface_liquid_water_fraction(IceModelVec2S &result);
  virtual PetscErrorCode mass_held_in_surface_layer(IceModelVec2S &result);
  virtual PetscErrorCode surface_layer_thickness(IceModelVec2S &result);

  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
protected:
  PISMAtmosphereModel *atmosphere;
};


//! A do-nothing (dummy) surface model. <b> Please avoid using it for real modeling! </b>
/*!
This dummy class is used, for example, when an internal formula generates the
surface mass balance.  A specific case is the manufactured solutions used in
verification.
*/
class PSDummy : public PISMSurfaceModel {
public:
  PSDummy(IceGrid &g, const NCConfigVariable &conf)
    : PISMComponent_TS(g, conf), PISMSurfaceModel(g, conf)
  {};

  virtual void attach_atmosphere_model(PISMAtmosphereModel *input)
  { delete input; }

  virtual PetscErrorCode init(PISMVars &) { return 0; };
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  { t = t_years; dt = dt_years; return 0; } // do nothing
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S&)
  { return 0; }

  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &)
  { return 0; }
  virtual void add_vars_to_output(string /*keyword*/, set<string> &/*result*/) {}
  virtual PetscErrorCode define_variables(set<string> /*vars*/, const NCTool &/*nc*/, nc_type /*nctype*/)
  { return 0; }
  virtual PetscErrorCode write_variables(set<string>, string)
  { return 0; }

  // Does not have an atmosphere model.
  virtual void get_diagnostics(map<string, PISMDiagnostic*> &/*dict*/) {}
};


//! \brief A class implementing a primitive surface model.
/*! 
This is an "invisible" surface processes model which "passes through"
information from the atmosphere above directly to the ice below the surface
layers.  It implements two modeling choices:
  \li accumulation which is obtained from an atmosphere model is interpreted
      as surface mass flux;
  \li mean-annual near-surface air temperature is interpreted as instantaneous
      temperature of the ice at the ice surface.

The second choice means that the upper boundary condition of the conservation of
energy scheme for the ice fluid is exactly the 2m air temperature.
*/
class PSSimple : public PISMSurfaceModel {
public:
  PSSimple(IceGrid &g, const NCConfigVariable &conf)
    : PISMComponent_TS(g, conf), PISMSurfaceModel(g, conf) {};
  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  {
    t = t_years; dt = dt_years;
    if (atmosphere) {
      PetscErrorCode ierr = atmosphere->update(t_years, dt_years); CHKERRQ(ierr);
    }
    return 0;
  }
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual void add_vars_to_output(string keyword, set<string> &result);
};


//! \brief A class implementing a constant-in-time surface model.  Reads data
//! from a PISM input file.
/*! 
This is model is just as simple as PSSimple, but it assumes results from a
surface processes model are already known.  But they are treated as constant in
time and they are read from the input file at the beginning of the PISM run.

Specifically, these two fields are read from the \c -i or \c -boot_file file:
  \li \c acab = ice-equivalent surface mass balance
  \li \c artm = ice fluid upper surface temperature.

This surface model does not use an atmosphere model at all, so the
\c attach_atmosphere_model() method is null.  Any choice of PISMAtmosphereModel
made using option \c -atmosphere is ignored.  This may be an advantage in coupler
code simplicity.

Note that a very minimal coupling of an existing atmosphere and surface processes
model to the ice dynamics core in PISM could be accomplished by using this
PSConstant class for relatively short ice dynamics runs, each of which starts by
reading the latest \c acab and \c artm fields supplied by the atmosphere and
surface processes model.
*/
class PSConstant : public PISMSurfaceModel {
public:
  PSConstant(IceGrid &g, const NCConfigVariable &conf)
    : PISMComponent_TS(g, conf), PISMSurfaceModel(g, conf)
  {};

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  { t = t_years; dt = dt_years; return 0; } // do nothing
  //! This surface model does not use an atmosphere model.
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input)
  { delete input; }

  // Does not have an atmosphere model.
  virtual void get_diagnostics(map<string, PISMDiagnostic*> &/*dict*/) {}

  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
  virtual void add_vars_to_output(string keyword, set<string> &result);
protected:
  string input_file;
  IceModelVec2S acab, artm;
};


//! \brief A class implementing a temperature-index (positive degree-day) scheme
//! to compute melt and runoff, and thus surface mass balance, from
//! precipitation and air temperature.
/*! 
Temperature-index schemes are far from perfect as a way of modeling surface mass
balance on ice sheets which experience surface melt, but they are known to have
reasonable data requirements and to do a good job when tuned appropriately
[\ref Hock05].

This base class already accesses a fair amount of functionality.  It holds a
pointer to an instance of the LocalMassBalance class.  This class has method
LocalMassBalance::getMassFluxFromTemperatureTimeSeries() which uses the
precipitation during the ice sheet model time step, plus a variable temperature
over that time step, to compute melt, refreeze, and surface balance.

This base class reads options <tt>-pdd_factor_snow</tt>, <tt>-pdd_factor_ice</tt>,
and <tt>-pdd_refreeze</tt> and sets these factors accordingly, in the case where
the factors are independent of location.  If option <tt>-pdd_fausto</tt> is used
then an object is called which updates these values based on the location.
*/
class PSTemperatureIndex : public PISMSurfaceModel {
public:
  PSTemperatureIndex(IceGrid &g, const NCConfigVariable &conf);
  virtual ~PSTemperatureIndex();
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years);
  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode max_timestep(PetscReal t_years, PetscReal &dt_years);
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);  
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
protected:
  virtual PetscErrorCode update_internal(PetscReal t_years, PetscReal dt_years);
  LocalMassBalance *mbscheme;	      //!< mass balance scheme to use

  FaustoGrevePDDObject *faustogreve;  //!< if not NULL then user wanted fausto PDD stuff

  DegreeDayFactors base_ddf;          //!< holds degree-day factors in location-independent case
  PetscScalar  base_pddStdDev,        //!< K; daily amount of randomness
               base_pddThresholdTemp; //!< K; temps are positive above this
  IceModelVec2S
    acab,		//!< cached surface mass balance rate
    accumulation_rate,  //!< diagnostic output accumulation rate (snow - rain)
    melt_rate,          //!< diagnostic output melt rate (rate at which snow
                        //! and ice is melted, but some snow melt refreezes)
    runoff_rate;        //!< diagnostic output meltwater runoff rate

  IceModelVec2S *lat, *lon, *usurf;  //!< PSTemperatureIndex must hold these
                                     //!pointers in order to use object which
                                     //!needs 3D location to determine degree
                                     //!day factors.
  bool pdd_annualize;
  PetscReal next_pdd_update_year;
};


//! \brief A base class for mechanisms which modify the results of a surface
//! processes model (an instance of PISMSurfaceModel) before they reach the ice.
/*! 
Frequently ice sheet models are driven by a "basic" surface model plus "forcings".
This modifier class allows the implementations of forcings which alter the 
results of the surface processes model.  That is, if the atmospheric inputs 
are already dealt-with, and a basic surface processes model is in use which 
generates surface mass balance and ice upper surface temperature, then instances
of this PSModifier class can be used to modify the surface mass balance and ice
upper surface temperature "just before" it gets to the ice itself.
*/
class PSModifier : public PISMSurfaceModel {
public:
  PSModifier(IceGrid &g, const NCConfigVariable &conf, PISMSurfaceModel *input)
    : PISMComponent_TS(g, conf), PISMSurfaceModel(g, conf), input_surface_model(input)
  {}

  virtual ~PSModifier()
  { delete input_surface_model; }

  virtual void attach_atmosphere_model(PISMAtmosphereModel *input) {
    input_surface_model->attach_atmosphere_model(input);
  }

  virtual void get_diagnostics(map<string, PISMDiagnostic*> &dict)
  { input_surface_model->get_diagnostics(dict); }

  virtual void add_vars_to_output(string key, set<string> &result) {
    if (input_surface_model != NULL)
      input_surface_model->add_vars_to_output(key, result);
  }
protected:
  PISMSurfaceModel *input_surface_model;
};


//! A class implementing a modified surface mass balance which forces
//! ice thickness to a given target by the end of the run.
class PSForceThickness : public PSModifier {
public:
  PSForceThickness(IceGrid &g, const NCConfigVariable &conf, PISMSurfaceModel *input)
    : PISMComponent_TS(g, conf), PSModifier(g, conf, input)
  {
    ice_thickness = NULL;
    alpha = config.get("force_to_thickness_alpha");
    write_ftt_mask = false;
  }

  virtual ~PSForceThickness() {}
  PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  {
    t = t_years; dt = dt_years;
    PetscErrorCode ierr = input_surface_model->update(t_years, dt_years); CHKERRQ(ierr);
    return 0;
  }
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input);
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual PetscErrorCode max_timestep(PetscReal t_years, PetscReal &dt_years);
  virtual void add_vars_to_output(string keyword, set<string> &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
protected:
  string input_file;
  PetscReal alpha;
  bool write_ftt_mask;
  IceModelVec2S *ice_thickness;	//!< current ice thickness produced by IceModel.
  IceModelVec2S target_thickness, ftt_mask, ftt_modified_acab;
};

//! \brief A class implementing a constant-in-time surface model for the surface mass balance. Reads data
//! Reads data from a PISM input file.
//! Ice surface temperature is parameterized as in PISM-PIK, dependent on latitude and surface elevation.

class PSConstantPIK : public PISMSurfaceModel {
public:
  PSConstantPIK(IceGrid &g, const NCConfigVariable &conf)
    : PISMComponent_TS(g, conf), PISMSurfaceModel(g, conf)
  {};

  virtual PetscErrorCode init(PISMVars &vars);
  //! This surface model does not use an atmosphere model.
  virtual void attach_atmosphere_model(PISMAtmosphereModel *input)
  { delete input; }

  // Does not have an atmosphere model.
  virtual void get_diagnostics(map<string, PISMDiagnostic*> &/*dict*/) {}

  virtual PetscErrorCode update(PetscReal t_years, PetscReal dt_years)
  { t = t_years; dt = dt_years; return 0; } // do nothing
  virtual PetscErrorCode ice_surface_mass_flux(IceModelVec2S &result);
  virtual PetscErrorCode ice_surface_temperature(IceModelVec2S &result);
  virtual PetscErrorCode define_variables(set<string> vars, const NCTool &nc, nc_type nctype);
  virtual PetscErrorCode write_variables(set<string> vars, string filename);
  virtual void add_vars_to_output(string keyword, set<string> &result);
protected:
  string input_file;
  IceModelVec2S acab, artm;
  IceModelVec2S *lat, *usurf;

};


#endif	// __PISMSurfaceModel_hh

