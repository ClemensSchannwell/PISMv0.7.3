// Copyright (C) 2004-2013 Jed Brown, Ed Bueler and Constantine Khroulev
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

#ifndef __iceModel_hh
#define __iceModel_hh

//! \file iceModel.hh Definition of class IceModel.
/*! \file iceModel.hh
IceModel is a big class which is an ice flow model.  It contains all parts that
are not well-defined, separated components.  Such components are better places
to put sub-models that have a clear, general interface to the rest of an ice
sheet model.

IceModel has pointers to well-defined components, when they exist.

IceModel generally interprets user options, and initializes components based on
such options.  It manages the initialization sequences (%e.g. a restart from a
file containing a complete model state, versus bootstrapping).
 */

#include <signal.h>
#include <gsl/gsl_rng.h>
#include <petscsnes.h>
#include <petsctime.h>		// PetscGetTime()

#include "flowlaws.hh"


#include "pism_const.hh"
#include "iceModelVec.hh"
#include "NCVariable.hh"
#include "PISMVars.hh"

// forward declarations
class IceGrid;
class EnthalpyConverter;
class PISMHydrology;
class PISMYieldStress;
class IceBasalResistancePlasticLaw;
class PISMStressBalance;
class PISMSurfaceModel;
class PISMOceanModel;
class PISMBedDef;
class PISMBedThermalUnit;
class PISMDiagnostic;
class PISMTSDiagnostic;
class PISMIcebergRemover;
class PISMOceanKill;
class PISMFloatKill;
class PISMCalvingAtThickness;
class PISMEigenCalving;


//! The base class for PISM.  Contains all essential variables, parameters, and flags for modelling an ice sheet.
class IceModel {
  // The following classes implement various diagnostic computations.
  // 2D and 3D:
  friend class IceModel_hardav;
  friend class IceModel_bwp;
  friend class IceModel_cts;
  friend class IceModel_dhdt;
  friend class IceModel_temp;
  friend class IceModel_temp_pa;
  friend class IceModel_temppabase;
  friend class IceModel_enthalpybase;
  friend class IceModel_enthalpysurf;
  friend class IceModel_tempbase;
  friend class IceModel_tempsurf;
  friend class IceModel_liqfrac;
  friend class IceModel_tempicethk;
  friend class IceModel_tempicethk_basal;
  friend class IceModel_new_mask;
  friend class IceModel_climatic_mass_balance_cumulative;
  friend class IceModel_dHdt;
  friend class IceModel_flux_divergence;
  // scalar:
  friend class IceModel_ivol;
  friend class IceModel_slvol;
  friend class IceModel_divoldt;
  friend class IceModel_iarea;
  friend class IceModel_imass;
  friend class IceModel_dimassdt;
  friend class IceModel_ivoltemp;
  friend class IceModel_ivolcold;
  friend class IceModel_ivolg;
  friend class IceModel_ivolf;
  friend class IceModel_iareatemp;
  friend class IceModel_iareacold;
  friend class IceModel_ienthalpy;
  friend class IceModel_iareag;
  friend class IceModel_iareaf;
  friend class IceModel_dt;
  friend class IceModel_max_diffusivity;
  friend class IceModel_surface_flux;
  friend class IceModel_surface_flux_cumulative;
  friend class IceModel_grounded_basal_flux;
  friend class IceModel_grounded_basal_flux_cumulative;
  friend class IceModel_sub_shelf_flux;
  friend class IceModel_sub_shelf_flux_cumulative;
  friend class IceModel_nonneg_flux;
  friend class IceModel_nonneg_flux_cumulative;
  friend class IceModel_discharge_flux;
  friend class IceModel_discharge_flux_cumulative;
  friend class IceModel_nonneg_flux_2D_cumulative;
  friend class IceModel_grounded_basal_flux_2D_cumulative;
  friend class IceModel_floating_basal_flux_2D_cumulative;
  friend class IceModel_discharge_flux_2D_cumulative;
  friend class IceModel_max_hor_vel;
  friend class IceModel_sum_divQ_flux;
  friend class IceModel_H_to_Href_flux;
  friend class IceModel_Href_to_H_flux;
public:
  // see iceModel.cc for implementation of constructor and destructor:
  IceModel(IceGrid &g, NCConfigVariable &config, NCConfigVariable &overrides);
  virtual ~IceModel(); // must be virtual merely because some members are virtual

  // see iMinit.cc
  virtual PetscErrorCode grid_setup();

  virtual PetscErrorCode allocate_submodels();
  virtual PetscErrorCode set_default_flowlaw();
  virtual PetscErrorCode allocate_enthalpy_converter();
  virtual PetscErrorCode allocate_basal_resistance_law();
  virtual PetscErrorCode allocate_stressbalance();
  virtual PetscErrorCode allocate_bed_deformation();
  virtual PetscErrorCode allocate_bedrock_thermal_unit();
  virtual PetscErrorCode allocate_subglacial_hydrology();
  virtual PetscErrorCode allocate_basal_yield_stress();
  virtual PetscErrorCode allocate_couplers();
  virtual PetscErrorCode allocate_iceberg_remover();

  virtual PetscErrorCode init_couplers();
  virtual PetscErrorCode set_grid_from_options();
  virtual PetscErrorCode set_grid_defaults();
  virtual PetscErrorCode model_state_setup();
  virtual PetscErrorCode set_vars_from_options();
  virtual PetscErrorCode allocate_internal_objects();
  virtual PetscErrorCode misc_setup();
  virtual PetscErrorCode init_diagnostics();
  virtual PetscErrorCode init_calving();

  virtual PetscErrorCode list_diagnostics();

  // see iceModel.cc
  PetscErrorCode init();
  virtual PetscErrorCode run();
  virtual PetscErrorCode step(bool do_mass_continuity, bool do_energy, bool do_age, bool do_skip);
  virtual PetscErrorCode setExecName(std::string my_executable_short_name);
  virtual void reset_counters();

  // see iMbootstrap.cc 
  virtual PetscErrorCode bootstrapFromFile(std::string fname);
  virtual PetscErrorCode bootstrap_2d(std::string fname);
  virtual PetscErrorCode bootstrap_3d();
  virtual PetscErrorCode putTempAtDepth();

  // see iMoptions.cc
  virtual PetscErrorCode setFromOptions();
  virtual PetscErrorCode set_output_size(std::string option, std::string description,
					 std::string default_value, std::set<std::string> &result);
  virtual std::string         get_output_size(std::string option);

  // see iMutil.cc
  virtual PetscErrorCode additionalAtStartTimestep();
  virtual PetscErrorCode additionalAtEndTimestep();
  virtual PetscErrorCode compute_cell_areas(); // is an initialization step; should go there

  // see iMIO.cc
  virtual PetscErrorCode initFromFile(std::string);
  virtual PetscErrorCode writeFiles(std::string default_filename);
  virtual PetscErrorCode write_model_state(const PIO &nc);
  virtual PetscErrorCode write_metadata(const PIO &nc,
                                        bool write_mapping,
                                        bool write_run_stats);
  virtual PetscErrorCode write_variables(const PIO &nc, std::set<std::string> vars,
					 PISM_IO_Type nctype);
protected:

  IceGrid               &grid;

  NCConfigVariable      mapping, //!< grid projection (mapping) parameters
    &config,			 //!< configuration flags and parameters
    &overrides,			 //!< flags and parameters overriding config, see -config_override
    run_stats;                   //!< run statistics

  NCGlobalAttributes    global_attributes;

  PISMHydrology   *subglacial_hydrology;
  PISMYieldStress *basal_yield_stress;
  IceBasalResistancePlasticLaw *basal;

  EnthalpyConverter *EC;
  PISMBedThermalUnit *btu;

  PISMIcebergRemover     *iceberg_remover;
  PISMOceanKill          *ocean_kill_calving;
  PISMFloatKill          *float_kill_calving;
  PISMCalvingAtThickness *thickness_threshold_calving;
  PISMEigenCalving       *eigen_calving;

  PISMSurfaceModel *surface;
  PISMOceanModel   *ocean;
  PISMBedDef       *beddef;

  //! \brief A dictionary with pointers to IceModelVecs below, for passing them
  //! from the IceModel core to other components (such as surface and ocean models)
  PISMVars variables;

  // state variables and some diagnostics/internals
  IceModelVec2S vh,		//!< ice surface elevation; ghosted
    vH,		//!< ice thickness; ghosted
    vtauc,		//!< yield stress for basal till (plastic or pseudo-plastic model); ghosted
    basal_melt_rate,           //!< rate of production of basal meltwater (ice-equivalent); no ghosts
    vLongitude,	//!< Longitude; ghosted to compute cell areas
    vLatitude,	//!< Latitude; ghosted to compute cell areas
    vbed,		//!< bed topography; ghosted
    vuplift,	//!< bed uplift rate; no ghosts
    vGhf,   //!< geothermal flux; no ghosts
    vFD,    //!< fracture density
    vFG,    //!< fracture growth rate
    vFH,    //!< fracture healing rate
    vFE,    //!< fracture flow enhancement
    vFA,    //!< fracture age
    vFT,    //!< fracture toughness
    bedtoptemp,     //!< temperature seen by bedrock thermal layer, if present; no ghosts
    vHref,          //!< accumulated mass advected to a partially filled grid cell
    vHresidual,     //!< residual ice mass of a not any longer partially (fully) filled grid cell
    acab,		//!< accumulation/ablation rate; no ghosts
    climatic_mass_balance_cumulative,    //!< cumulative acab
    grounded_basal_flux_2D_cumulative, //!< grounded basal (melt/freeze-on) cumulative flux
    floating_basal_flux_2D_cumulative, //!< floating (sub-shelf) basal (melt/freeze-on) cumulative flux
    nonneg_flux_2D_cumulative,         //!< cumulative nonnegative-rule flux
    discharge_flux_2D_cumulative,      //!< cumulative discharge (calving) flux (2D field)
    ice_surface_temp,		//!< ice temperature at the ice surface but below firn; no ghosts
    liqfrac_surface,    //!< ice liquid water fraction at the top surface of the ice
    shelfbtemp,		//!< ice temperature at the shelf base; no ghosts
    shelfbmassflux,	//!< ice mass flux into the ocean at the shelf base; no ghosts
    cell_area,		//!< cell areas (computed using the WGS84 datum)
    flux_divergence;    //!< flux divergence

  IceModelVec2 strain_rates; //!< major and minor principal components of horizontal strain-rate tensor
  
  IceModelVec2 deviatoric_stresses; //!< components of horizontal stress tensor along axes and shear stress
  IceModelVec2 principal_stresses; //!< major and minor principal components of horizontal stress tensor

  IceModelVec2Int vMask, //!< \brief mask for flow type with values ice_free_bedrock,
                         //!< grounded_ice, floating_ice, ice_free_ocean
    vBCMask; //!< mask to determine Dirichlet boundary locations
 
  IceModelVec2V vBCvel; //!< Dirichlet boundary velocities
  
  IceModelVec2S gl_mask; //!< mask to determine grounding line position

  IceModelVec3
        T3,		//!< absolute temperature of ice; K (ghosted)
        Enth3,          //!< enthalpy; J / kg (ghosted)
        tau3;		//!< age of ice; s (ghosted because it is averaged onto the staggered-grid)

  // parameters
  PetscReal   dt,     //!< mass continuity time step, s
              t_TempAge,  //!< time of last update for enthalpy/temperature
              dt_TempAge,  //!< enthalpy/temperature and age time-steps
              maxdt_temporary, dt_force,
              CFLviolcount,    //!< really is just a count, but PISMGlobalSum requires this type
              dt_from_cfl, CFLmaxdt, CFLmaxdt2D,
              gDmax,		// global max of the diffusivity
              gmaxu, gmaxv, gmaxw,  // global maximums on 3D grid of abs value of vel components
    grounded_basal_ice_flux_cumulative,
    nonneg_rule_flux_cumulative,
    sub_shelf_ice_flux_cumulative,
    surface_ice_flux_cumulative,
    sum_divQ_SIA_cumulative,
    sum_divQ_SSA_cumulative,
    Href_to_H_flux_cumulative,
    H_to_Href_flux_cumulative,
    discharge_flux_cumulative;      //!< cumulative discharge (calving) flux

  PetscInt    skipCountDown;

  // flags
  PetscBool  allowAboveMelting;
  PetscBool  repeatRedist, putOnTop;
  char        adaptReasonFlag;

  std::string      stdout_flags, stdout_ssa;

  std::string executable_short_name;
  
protected:
  // see iceModel.cc
  virtual PetscErrorCode createVecs();
  virtual PetscErrorCode deallocate_internal_objects();

  // see iMadaptive.cc
  virtual PetscErrorCode computeMax3DVelocities();
  virtual PetscErrorCode computeMax2DSlidingSpeed();
  virtual PetscErrorCode adaptTimeStepDiffusivity();
  virtual PetscErrorCode determineTimeStep(const bool doTemperatureCFL);
  virtual PetscErrorCode countCFLViolations(PetscScalar* CFLviol);

  // see iMage.cc
  virtual PetscErrorCode ageStep();

  // see iMenergy.cc
  virtual PetscErrorCode energyStep();
  virtual PetscErrorCode get_bed_top_temp(IceModelVec2S &result);
  virtual bool checkThinNeigh(
       PetscScalar E, PetscScalar NE, PetscScalar N, PetscScalar NW, 
       PetscScalar W, PetscScalar SW, PetscScalar S, PetscScalar SE);

  // see iMenthalpy.cc
  virtual PetscErrorCode compute_enthalpy_cold(IceModelVec3 &temperature, IceModelVec3 &result);
  virtual PetscErrorCode compute_enthalpy(IceModelVec3 &temperature, IceModelVec3 &liquid_water_fraction,
                                          IceModelVec3 &result);
  virtual PetscErrorCode compute_liquid_water_fraction(IceModelVec3 &enthalpy, IceModelVec3 &result);

  virtual PetscErrorCode setCTSFromEnthalpy(IceModelVec3 &result);

  virtual PetscErrorCode enthalpyAndDrainageStep(PetscScalar* vertSacrCount,
                                                 PetscScalar* liquifiedVol, PetscScalar* bulgeCount);

  // see iMgeometry.cc
  virtual PetscErrorCode updateSurfaceElevationAndMask();
  virtual PetscErrorCode update_mask(IceModelVec2S &bed, IceModelVec2S &ice_thickness, IceModelVec2Int &mask);
  virtual PetscErrorCode update_surface_elevation(IceModelVec2S &bed, IceModelVec2S &ice_thickness, IceModelVec2S &result);
  virtual void cell_interface_fluxes(bool dirichlet_bc,
                                     int i, int j,
                                     planeStar<PISMVector2> input_velocity,
                                     planeStar<PetscScalar> input_flux,
                                     planeStar<PetscScalar> &output_velocity,
                                     planeStar<PetscScalar> &output_flux);
  virtual void adjust_flow(planeStar<int> mask,
                           planeStar<PetscScalar> &SSA_velocity,
                           planeStar<PetscScalar> &SIA_flux);
  virtual PetscErrorCode massContExplicitStep();
  virtual PetscErrorCode sub_gl_position();
  virtual PetscErrorCode do_calving();
  virtual PetscErrorCode Href_cleanup();
  virtual PetscErrorCode update_cumulative_discharge(IceModelVec2S &thickness,
                                                     IceModelVec2S &thickness_old,
                                                     IceModelVec2S &Href,
                                                     IceModelVec2S &Href_old);


  // see iMIO.cc
  virtual PetscErrorCode dumpToFile(std::string filename);
  virtual PetscErrorCode regrid(int dimensions);
  virtual PetscErrorCode regrid_variables(std::string filename, std::set<std::string> regrid_vars, int ndims);
  virtual PetscErrorCode init_enthalpy(std::string filename, bool regrid, int last_record);

  // see iMfractures.cc
  virtual PetscErrorCode calculateFractureDensity();

  // see iMpartgrid.cc
  PetscReal get_threshold_thickness(bool do_redist,
                                    planeStar<int> Mask,
                                    planeStar<PetscScalar> thickness,
                                    planeStar<PetscScalar> surface_elevation,
                                    PetscScalar bed_elevation);
  virtual PetscErrorCode redistResiduals();
  virtual PetscErrorCode calculateRedistResiduals();

  // see iMreport.cc
  virtual PetscErrorCode volumeArea(
                       PetscScalar& gvolume,PetscScalar& garea);
  virtual PetscErrorCode energyStats(
                       PetscScalar iarea,PetscScalar &gmeltfrac);
  virtual PetscErrorCode ageStats(PetscScalar ivol, PetscScalar &gorigfrac);
  virtual PetscErrorCode summary(bool tempAndAge);
  virtual PetscErrorCode summaryPrintLine(PetscBool printPrototype, bool tempAndAge,
                                          PetscScalar delta_t,
                                          PetscScalar volume, PetscScalar area,
                                          PetscScalar meltfrac, PetscScalar max_diffusivity);

  // see iMreport.cc;  methods for computing diagnostic quantities:
  // scalar:
  virtual PetscErrorCode compute_ice_volume(PetscScalar &result);
  virtual PetscErrorCode compute_sealevel_volume(PetscScalar &result);
  virtual PetscErrorCode compute_ice_volume_temperate(PetscScalar &result);
  virtual PetscErrorCode compute_ice_volume_cold(PetscScalar &result);
  virtual PetscErrorCode compute_ice_area(PetscScalar &result);
  virtual PetscErrorCode compute_ice_area_temperate(PetscScalar &result);
  virtual PetscErrorCode compute_ice_area_cold(PetscScalar &result);
  virtual PetscErrorCode compute_ice_area_grounded(PetscScalar &result);
  virtual PetscErrorCode compute_ice_area_floating(PetscScalar &result);
  virtual PetscErrorCode compute_ice_enthalpy(PetscScalar &result);

  // see iMtemp.cc
  virtual PetscErrorCode excessToFromBasalMeltLayer(
                      const PetscScalar rho, const PetscScalar c, const PetscScalar L,
                      const PetscScalar z, const PetscScalar dz,
                      PetscScalar *Texcess, PetscScalar *bwat);
  virtual PetscErrorCode temperatureStep(PetscScalar* vertSacrCount, PetscScalar* bulgeCount);

  // see iMutil.cc
  virtual int            endOfTimeStepHook();
  virtual PetscErrorCode stampHistoryCommand();
  virtual PetscErrorCode stampHistoryEnd();
  virtual PetscErrorCode stampHistory(std::string);
  virtual PetscErrorCode update_run_stats();
  virtual PetscErrorCode check_maximum_thickness();
  virtual PetscErrorCode check_maximum_thickness_hook(const int old_Mz);
  virtual bool           issounding(const PetscInt i, const PetscInt j);

protected:
  // working space (a convenience)
  static const PetscInt nWork2d=2;
  IceModelVec2S vWork2d[nWork2d];
  IceModelVec2V vWork2dV;

  // 3D working space
  IceModelVec3 vWork3d;

  PISMStressBalance *stress_balance;

  std::map<std::string,PISMDiagnostic*> diagnostics;
  std::map<std::string,PISMTSDiagnostic*> ts_diagnostics;

  // Set of variables to put in the output file:
  std::set<std::string> output_vars;

  // This is related to the snapshot saving feature
  std::string snapshots_filename;
  bool save_snapshots, snapshots_file_is_ready, split_snapshots;
  std::vector<double> snapshot_times;
  std::set<std::string> snapshot_vars;
  unsigned int current_snapshot;
  PetscErrorCode init_snapshots();
  PetscErrorCode write_snapshot();

  // scalar time-series
  bool save_ts;			//! true if the user requested time-series output
  std::string ts_filename;		//! file to write time-series to
  std::vector<double> ts_times;	//! times requested
  unsigned int current_ts;	//! index of the current time
  std::set<std::string> ts_vars;		//! variables requested
  PetscErrorCode init_timeseries();
  PetscErrorCode flush_timeseries();
  PetscErrorCode write_timeseries();
  PetscErrorCode ts_max_timestep(double my_t, double& my_dt, bool &restrict);

  // spatially-varying time-series
  bool save_extra, extra_file_is_ready, split_extra;
  std::string extra_filename;
  std::vector<double> extra_times;
  unsigned int next_extra;
  double last_extra;
  std::set<std::string> extra_vars;
  NCTimeBounds extra_bounds;
  NCTimeseries timestamp;
  PetscErrorCode init_extras();
  PetscErrorCode write_extras();
  PetscErrorCode extras_max_timestep(double my_t, double& my_dt, bool &restrict);

  // automatic backups
  double backup_interval;
  std::string backup_filename;
  PetscReal last_backup_time;
  std::set<std::string> backup_vars;
  PetscErrorCode init_backups();
  PetscErrorCode write_backup();

  // diagnostic viewers; see iMviewers.cc
  virtual PetscErrorCode init_viewers();
  virtual PetscErrorCode update_viewers();
  std::set<std::string> map_viewers, slice_viewers, sounding_viewers;
  PetscInt     id, jd;	     // sounding indices
  std::map<std::string,PetscViewer> viewers;

  // time step decision helper; see step()
  inline void revise_maxdt(PetscReal new_dt, PetscReal &my_maxdt) {
    if (my_maxdt > 0)
      my_maxdt = PetscMin(new_dt, my_maxdt);
    else
      my_maxdt = new_dt;
  }

private:
  PetscLogDouble start_time;    // this is used in the wall-clock-time backup code

  int event_step,		//!< total time spent doing time-stepping
    event_velocity,		//!< total velocity computation
    event_energy,		//!< energy balance computation
    event_hydrology,		//!< subglacial hydrology computation
    event_mass,			//!< mass continuity computation
    event_age,			//!< age computation
    event_beddef,		//!< bed deformation step
    event_output,		//!< time spent writing the output file
    event_output_define,        //!< time spent defining variables
    event_snapshots,            //!< time spent writing snapshots
    event_backups;              //!< time spent writing backups files
};

#endif /* __iceModel_hh */

