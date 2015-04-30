// Copyright (C) 2009--2015 Ed Bueler and Constantine Khroulev
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

//This file contains various initialization routines. See the IceModel::init()
//documentation comment in iceModel.cc for the order in which they are called.

#include <petscdmda.h>
#include <cassert>
#include <algorithm>

#include "iceModel.hh"
#include "base/basalstrength/PISMConstantYieldStress.hh"
#include "base/basalstrength/PISMMohrCoulombYieldStress.hh"
#include "base/basalstrength/basal_resistance.hh"
#include "base/calving/PISMCalvingAtThickness.hh"
#include "base/calving/PISMEigenCalving.hh"
#include "base/calving/PISMFloatKill.hh"
#include "base/calving/PISMIcebergRemover.hh"
#include "base/calving/PISMOceanKill.hh"
#include "base/energy/bedrockThermalUnit.hh"
#include "base/hydrology/PISMHydrology.hh"
#include "base/rheology/flowlaw_factory.hh"
#include "base/stressbalance/PISMStressBalance.hh"
#include "base/stressbalance/sia/SIAFD.hh"
#include "base/stressbalance/ssa/SSAFD.hh"
#include "base/stressbalance/ssa/SSAFEM.hh"
#include "base/util/Mask.hh"
#include "base/util/PISMConfigInterface.hh"
#include "base/util/PISMTime.hh"
#include "base/util/error_handling.hh"
#include "base/util/io/PIO.hh"
#include "base/util/pism_options.hh"
#include "coupler/PISMOcean.hh"
#include "coupler/PISMSurface.hh"
#include "coupler/atmosphere/PAFactory.hh"
#include "coupler/ocean/POFactory.hh"
#include "coupler/surface/PSFactory.hh"
#include "earth/PBLingleClark.hh"
#include "earth/PISMBedDef.hh"
#include "enthalpyConverter.hh"
#include "varcEnthalpyConverter.hh"
#include "base/util/PISMVars.hh"
#include "base/util/io/io_helpers.hh"

namespace pism {

//! Set default values of grid parameters.
/*!
  Derived classes (IceCompModel, for example) reimplement this to change the
  grid initialization when no -i option is set.
 */
void IceModel::set_grid_defaults() {
  // Logical (as opposed to physical) grid dimensions should not be
  // deduced from a bootstrapping file, so we check if these options
  // are set and stop if they are not.
  options::Integer
    Mx("-Mx", "grid size in X direction", m_grid->Mx()),
    My("-My", "grid size in Y direction", m_grid->My()),
    Mz("-Mz", "grid size in vertical direction", m_grid->Mz());
  options::Real Lz("-Lz", "height of the computational domain", m_grid->Lz());

  if (not (Mx.is_set() and My.is_set() and Mz.is_set() and Lz.is_set())) {
    throw RuntimeError("All of -bootstrap, -Mx, -My, -Mz, -Lz are required for bootstrapping.");
  }

  // Get the bootstrapping file name:

  options::String input_file("-i", "Specifies the input file");
  std::string filename = input_file;

  if (not input_file.is_set()) {
    throw RuntimeError("Please specify an input file using -i.");
  }

  // Use a bootstrapping file to set some grid parameters (they can be
  // overridden later, in IceModel::set_grid_from_options()).

  PIO nc(m_grid->com, "netcdf3"); // OK to use netcdf3, we read very little data here.

  // Try to deduce grid information from present spatial fields. This is bad,
  // because theoretically these fields may use different grids. We need a
  // better way of specifying PISM's computational grid at bootstrapping.
  grid_info input;
  {
    std::vector<std::string> names;
    names.push_back("land_ice_thickness");
    names.push_back("bedrock_altitude");
    names.push_back("thk");
    names.push_back("topg");
    bool grid_info_found = false;
    nc.open(filename, PISM_READONLY);
    for (unsigned int i = 0; i < names.size(); ++i) {

      grid_info_found = nc.inq_var(names[i]);
      if (not grid_info_found) {
        // Failed to find using a short name. Try using names[i] as a
        // standard name...
        std::string dummy1;
        bool dummy2;
        nc.inq_var("dummy", names[i], grid_info_found, dummy1, dummy2);
      }

      if (grid_info_found) {
        input = grid_info(nc, names[i], m_sys, m_grid->periodicity());
        break;
      }
    }

    if (grid_info_found == false) {
      throw RuntimeError::formatted("no geometry information found in '%s'",
                                    filename.c_str());
    }
    nc.close();
  }

  // proj.4 and mapping
  {
    nc.open(filename, PISM_READONLY);
    std::string proj4_string = nc.get_att_text("PISM_GLOBAL", "proj4");
    if (proj4_string.empty() == false) {
      global_attributes.set_string("proj4", proj4_string);
    }

    bool mapping_exists = nc.inq_var("mapping");
    if (mapping_exists) {
      io::read_attributes(nc, mapping.get_name(), mapping);
      mapping.report_to_stdout(*m_log, 4);
    }
    nc.close();
  }

  // Set the grid center and horizontal extent:
  m_grid->set_extent(input.x0, input.y0, input.Lx, input.Ly);

  // read current time if no option overrides it (avoids unnecessary reporting)
  bool ys = options::Bool("-ys", "starting time");
  if (not ys) {
    if (input.t_len > 0) {
      m_time->set_start(input.time);
      m_log->message(2,
                 "  time t = %s found; setting current time\n",
                 m_time->date().c_str());
    }
  }

  m_time->init(*m_log);
}

//! Initalizes the grid from options.
/*! Reads all of -Mx, -My, -Mz, -Mbz, -Lx, -Ly, -Lz, -Lbz, -z_spacing and
    -zb_spacing. Sets corresponding grid parameters.
 */
void IceModel::set_grid_from_options() {

  double
    x0 = m_grid->x0(),
    y0 = m_grid->y0(),
    Lx = m_grid->Lx(),
    Ly = m_grid->Ly(),
    Lz = m_grid->Lz();
  int
    Mx = m_grid->Mx(),
    My = m_grid->My(),
    Mz = m_grid->Mz();
  SpacingType spacing = QUADRATIC; // irrelevant (it is reset below)

  // Process options:
  {
    // Domain size
    {
      Lx = 1000.0 * options::Real("-Lx", "Half of the grid extent in the Y direction, in km",
                                  Lx / 1000.0);
      Ly = 1000.0 * options::Real("-Ly", "Half of the grid extent in the X direction, in km",
                                  Ly / 1000.0);
      Lz = options::Real("-Lz", "Grid extent in the Z (vertical) direction in the ice, in meters",
                         Lz);
    }

    // Alternatively: domain size and extent
    {
      options::RealList x_range("-x_range", "min,max x coordinate values");
      options::RealList y_range("-y_range", "min,max y coordinate values");

      if (x_range.is_set() && y_range.is_set()) {
        if (x_range->size() != 2 || y_range->size() != 2) {
          throw RuntimeError("-x_range and/or -y_range argument is invalid.");
        }
        x0 = (x_range[0] + x_range[1]) / 2.0;
        y0 = (y_range[0] + y_range[1]) / 2.0;
        Lx = (x_range[1] - x_range[0]) / 2.0;
        Ly = (y_range[1] - y_range[0]) / 2.0;
      }
    }

    // Number of grid points
    options::Integer mx("-Mx", "Number of grid points in the X direction",
                        Mx);
    options::Integer my("-My", "Number of grid points in the Y direction",
                        My);
    options::Integer mz("-Mz", "Number of grid points in the Z (vertical) direction in the ice",
                        Mz);
    Mx = mx;
    My = my;
    Mz = mz;

    // validate inputs
    {
      if (Mx < 3 || My < 3 || Mz < 2) {
        throw RuntimeError::formatted("-Mx %d -My %d -Mz %d is invalid\n"
                                      "(have to have Mx >= 3, My >= 3, Mz >= 2).",
                                      Mx, My, Mz);
      }

      if (Lx <= 0.0 || Ly <= 0.0 || Lz <= 0.0) {
        throw RuntimeError::formatted("-Lx %f -Ly %f -Lz %f is invalid\n"
                                      "(Lx, Ly, Lz have to be positive).",
                                      Lx / 1000.0, Ly / 1000.0, Lz);
      }
    }

    // Vertical spacing (respects -z_spacing)
    {
      spacing = string_to_spacing(config->get_string("grid_ice_vertical_spacing"));
    }
  }

  // Use the information obtained above:
  //
  // Note that grid.periodicity() includes the result of processing
  // the -periodicity option.
  m_grid->set_size_and_extent(x0, y0, Lx, Ly, Mx, My, m_grid->periodicity());
  m_grid->set_vertical_levels(Lz, Mz, spacing);

  // At this point all the fields except for da2, xs, xm, ys, ym should be
  // filled. We're ready to call grid.allocate().
}

//! Sets up the computational grid.
/*!
  There are two cases here:

  1) Initializing from a PISM ouput file, in which case all the options
  influencing the grid (currently: -Mx, -My, -Mz, -Mbz, -Lx, -Ly, -Lz, -z_spacing,
  -zb_spacing) are ignored.

  2) Initializing using defaults, command-line options and (possibly) a
  bootstrapping file. Derived classes requiring special grid setup should
  reimplement IceGrid::set_grid_from_options().

  No memory allocation should happen here.
 */
void IceModel::grid_setup() {

  m_log->message(3,
             "Setting up the computational grid...\n");

  // Check if we are initializing from a PISM output file:
  options::String input_file("-i", "Specifies a PISM input file");
  bool bootstrap = options::Bool("-bootstrap", "enable bootstrapping heuristics");

  if (input_file.is_set() and not bootstrap) {
    PIO nc(m_grid->com, "guess_mode");

    // Get the 'source' global attribute to check if we are given a PISM output
    // file:
    nc.open(input_file, PISM_READONLY);
    std::string source = nc.get_att_text("PISM_GLOBAL", "source");

    std::string proj4_string = nc.get_att_text("PISM_GLOBAL", "proj4");
    if (proj4_string.empty() == false) {
      global_attributes.set_string("proj4", proj4_string);
    }

    bool mapping_exists = nc.inq_var("mapping");
    if (mapping_exists) {
      io::read_attributes(nc, mapping.get_name(), mapping);
      mapping.report_to_stdout(*m_log, 4);
    }

    nc.close();

    // If it's missing, print a warning
    if (source.empty()) {
      m_log->message(1,
                 "PISM WARNING: file '%s' does not have the 'source' global attribute.\n"
                 "     If '%s' is a PISM output file, please run the following to get rid of this warning:\n"
                 "     ncatted -a source,global,c,c,PISM %s\n",
                 input_file->c_str(), input_file->c_str(), input_file->c_str());
    } else if (source.find("PISM") == std::string::npos) {
      // If the 'source' attribute does not contain the string "PISM", then print
      // a message and stop:
      m_log->message(1,
                 "PISM WARNING: '%s' does not seem to be a PISM output file.\n"
                 "     If it is, please make sure that the 'source' global attribute contains the string \"PISM\".\n",
                 input_file->c_str());
    }

    std::vector<std::string> names;
    names.push_back("enthalpy");
    names.push_back("temp");

    nc.open(input_file, PISM_READONLY);

    bool var_exists = false;
    for (unsigned int i = 0; i < names.size(); ++i) {
      var_exists = nc.inq_var(names[i]);

      if (var_exists == true) {
        IceGrid::FromFile(nc, names[i], m_grid->periodicity(), *m_grid);
        break;
      }
    }

    if (var_exists == false) {
      nc.close();
      throw RuntimeError::formatted("file %s has neither enthalpy nor temperature in it",
                                    input_file->c_str());
    }

    nc.close();

    // These options are ignored because we're getting *all* the grid
    // parameters from a file.
    options::ignored(*m_log, "-Mx");
    options::ignored(*m_log, "-My");
    options::ignored(*m_log, "-Mz");
    options::ignored(*m_log, "-Mbz");
    options::ignored(*m_log, "-Lx");
    options::ignored(*m_log, "-Ly");
    options::ignored(*m_log, "-Lz");
    options::ignored(*m_log, "-z_spacing");
  } else {
    set_grid_defaults();
    set_grid_from_options();
  }

  m_grid->allocate();
}

//! Initialize time from an input file or command-line options.
void IceModel::time_setup() {
  Time::Ptr time = m_time;

  // Check if we are initializing from a PISM output file:
  options::String input_file("-i", "Specifies a PISM input file");

  if (input_file.is_set()) {
    PIO nc(m_grid->com, "guess_mode");

    std::string time_name = config->get_string("time_dimension_name");

    nc.open(input_file, PISM_READONLY);
    unsigned int time_length = nc.inq_dimlen(time_name);
    if (time_length > 0) {
      double T = 0.0;
      // Set the default starting time to be equal to the last time saved in the input file
      nc.inq_dim_limits(time_name, NULL, &T);
      time->set_start(T);
    }
    nc.close();
  }

  time->init(*m_log);

  m_log->message(2,
             "* Setting time: [%s, %s]  (%s years, using the '%s' calendar)\n",
             time->start_date().c_str(),
             time->end_date().c_str(),
             time->run_length().c_str(),
             time->calendar().c_str());
}

//! Sets the starting values of model state variables.
/*!
  There are two cases:

  1) Initializing from a PISM output file.

  2) Setting the values using command-line options only (verification and
  simplified geometry runs, for example) or from a bootstrapping file, using
  heuristics to fill in missing and 3D fields.

  Calls IceModel::regrid().

  This function is called after all the memory allocation is done and all the
  physical parameters are set.

  Calling this method should be all one needs to set model state variables.
  Please avoid modifying them in other parts of the initialization sequence.

  Also, please avoid operations that would make it unsafe to call this more
  than once (memory allocation is one example).
 */
void IceModel::model_state_setup() {

  reset_counters();

  // Initialize (or re-initialize) boundary models.
  init_couplers();

  // Check if we are initializing from a PISM output file:
  options::String input_file("-i", "Specifies the PISM input file");
  bool bootstrap = options::Bool("-bootstrap", "enable bootstrapping heuristics");

  if (input_file.is_set() and not bootstrap) {
    initFromFile(input_file);

    regrid(0);
    // Check consistency of geometry after initialization:
    updateSurfaceElevationAndMask();
  } else {
    set_vars_from_options();
  }

  // Initialize a bed deformation model (if needed); this should go
  // after the regrid(0) call but before other init() calls that need
  // bed elevation and uplift.
  if (beddef) {
    beddef->init();
    m_grid->variables().add(beddef->bed_elevation());
    m_grid->variables().add(beddef->uplift());
  }

  if (stress_balance) {
    stress_balance->init();

    if (config->get_boolean("include_bmr_in_continuity")) {
      stress_balance->set_basal_melt_rate(basal_melt_rate);
    }
  }

  if (btu) {
    bool bootstrapping_needed = false;
    btu->init(bootstrapping_needed);

    if (bootstrapping_needed == true) {
      // update surface and ocean models so that we can get the
      // temperature at the top of the bedrock
      m_log->message(2,
                 "getting surface B.C. from couplers...\n");
      init_step_couplers();

      get_bed_top_temp(bedtoptemp);

      btu->bootstrap();
    }
  }

  if (subglacial_hydrology) {
    subglacial_hydrology->init();
  }

  // basal_yield_stress_model->init() needs bwat so this must happen
  // after subglacial_hydrology->init()
  if (basal_yield_stress_model) {
    basal_yield_stress_model->init();
  }

  if (climatic_mass_balance_cumulative.was_created()) {
    if (input_file.is_set()) {
      m_log->message(2,
                 "* Trying to read cumulative climatic mass balance from '%s'...\n",
                 input_file->c_str());
      climatic_mass_balance_cumulative.regrid(input_file, OPTIONAL, 0.0);
    } else {
      climatic_mass_balance_cumulative.set(0.0);
    }
  }

  if (grounded_basal_flux_2D_cumulative.was_created()) {
    if (input_file.is_set()) {
      m_log->message(2,
                 "* Trying to read cumulative grounded basal flux from '%s'...\n",
                 input_file->c_str());
      grounded_basal_flux_2D_cumulative.regrid(input_file, OPTIONAL, 0.0);
    } else {
      grounded_basal_flux_2D_cumulative.set(0.0);
    }
  }

  if (floating_basal_flux_2D_cumulative.was_created()) {
    if (input_file.is_set()) {
      m_log->message(2,
                 "* Trying to read cumulative floating basal flux from '%s'...\n",
                 input_file->c_str());
      floating_basal_flux_2D_cumulative.regrid(input_file, OPTIONAL, 0.0);
    } else {
      floating_basal_flux_2D_cumulative.set(0.0);
    }
  }

  if (nonneg_flux_2D_cumulative.was_created()) {
    if (input_file.is_set()) {
      m_log->message(2,
                 "* Trying to read cumulative nonneg flux from '%s'...\n",
                 input_file->c_str());
      nonneg_flux_2D_cumulative.regrid(input_file, OPTIONAL, 0.0);
    } else {
      nonneg_flux_2D_cumulative.set(0.0);
    }
  }

  if (input_file.is_set()) {
    PIO nc(m_grid->com, "netcdf3");

    nc.open(input_file, PISM_READONLY);
    bool run_stats_exists = nc.inq_var("run_stats");
    if (run_stats_exists) {
      io::read_attributes(nc, run_stats.get_name(), run_stats);
    }
    nc.close();

    if (run_stats.has_attribute("grounded_basal_ice_flux_cumulative")) {
      grounded_basal_ice_flux_cumulative = run_stats.get_double("grounded_basal_ice_flux_cumulative");
    }

    if (run_stats.has_attribute("nonneg_rule_flux_cumulative")) {
      nonneg_rule_flux_cumulative = run_stats.get_double("nonneg_rule_flux_cumulative");
    }

    if (run_stats.has_attribute("sub_shelf_ice_flux_cumulative")) {
      sub_shelf_ice_flux_cumulative = run_stats.get_double("sub_shelf_ice_flux_cumulative");
    }

    if (run_stats.has_attribute("surface_ice_flux_cumulative")) {
      surface_ice_flux_cumulative = run_stats.get_double("surface_ice_flux_cumulative");
    }

    if (run_stats.has_attribute("sum_divQ_SIA_cumulative")) {
      sum_divQ_SIA_cumulative = run_stats.get_double("sum_divQ_SIA_cumulative");
    }

    if (run_stats.has_attribute("sum_divQ_SSA_cumulative")) {
      sum_divQ_SSA_cumulative = run_stats.get_double("sum_divQ_SSA_cumulative");
    }

    if (run_stats.has_attribute("Href_to_H_flux_cumulative")) {
      Href_to_H_flux_cumulative = run_stats.get_double("Href_to_H_flux_cumulative");
    }

    if (run_stats.has_attribute("H_to_Href_flux_cumulative")) {
      H_to_Href_flux_cumulative = run_stats.get_double("H_to_Href_flux_cumulative");
    }

    if (run_stats.has_attribute("discharge_flux_cumulative")) {
      discharge_flux_cumulative = run_stats.get_double("discharge_flux_cumulative");
    }
  }

  compute_cell_areas();

  // a report on whether PISM-PIK modifications of IceModel are in use
  const bool pg   = config->get_boolean("part_grid"),
    pr   = config->get_boolean("part_redist"),
    ki   = config->get_boolean("kill_icebergs");
  if (pg || pr || ki) {
    m_log->message(2,
               "* PISM-PIK mass/geometry methods are in use:  ");

    if (pg)   {
      m_log->message(2, "part_grid,");
    }
    if (pr)   {
      m_log->message(2, "part_redist,");
    }
    if (ki)   {
      m_log->message(2, "kill_icebergs");
    }

    m_log->message(2, "\n");
  }

  stampHistoryCommand();
}

//! Sets starting values of model state variables using command-line options.
/*!
  Sets starting values of model state variables using command-line options and
  (possibly) a bootstrapping file.

  In the base class there is only one case: bootstrapping.
 */
void IceModel::set_vars_from_options() {

  m_log->message(3,
             "Setting initial values of model state variables...\n");

  options::String input_file("-i", "Specifies the input file");
  bool bootstrap = options::Bool("-bootstrap", "enable bootstrapping heuristics");

  if (bootstrap and input_file.is_set()) {
    bootstrapFromFile(input_file);
  } else {
    throw RuntimeError("No input file specified.");
  }
}

//! \brief Decide which stress balance model to use.
void IceModel::allocate_stressbalance() {

  EnthalpyConverter::Ptr EC = m_ctx->enthalpy_converter();

  using namespace pism::stressbalance;

  if (stress_balance != NULL) {
    return;
  }

  m_log->message(2,
             "# Allocating a stress balance model...\n");

  std::string model = config->get_string("stress_balance_model");

  ShallowStressBalance *sliding = NULL;
  if (model == "none" || model == "sia") {
    sliding = new ZeroSliding(m_grid, EC);
  } else if (model == "prescribed_sliding" || model == "prescribed_sliding+sia") {
    sliding = new PrescribedSliding(m_grid, EC);
  } else if (model == "ssa" || model == "ssa+sia") {
    std::string method = config->get_string("ssa_method");

    if (method == "fem") {
      sliding = new SSAFEM(m_grid, EC);
    } else if (method == "fd") {
      sliding = new SSAFD(m_grid, EC);
    } else {
      throw RuntimeError::formatted("invalid ssa method: %s", method.c_str());
    }

  } else {
    throw RuntimeError::formatted("invalid stress balance model: %s", model.c_str());
  }

  SSB_Modifier *modifier = NULL;
  if (model == "none" || model == "ssa" || model == "prescribed_sliding") {
    modifier = new ConstantInColumn(m_grid, EC);
  } else if (model == "prescribed_sliding+sia" || "ssa+sia") {
    modifier = new SIAFD(m_grid, EC);
  } else {
    throw RuntimeError::formatted("invalid stress balance model: %s", model.c_str());
  }

  // ~StressBalance() will de-allocate sliding and modifier.
  stress_balance = new StressBalance(m_grid, sliding, modifier);
}

void IceModel::allocate_iceberg_remover() {

  if (iceberg_remover != NULL) {
    return;
  }

  m_log->message(2,
             "# Allocating an iceberg remover (part of a calving model)...\n");

  if (config->get_boolean("kill_icebergs")) {

    // this will throw an exception on failure
    iceberg_remover = new calving::IcebergRemover(m_grid);

    // Iceberg Remover does not have a state, so it is OK to
    // initialize here.
    iceberg_remover->init();
  }
}

//! \brief Decide which bedrock thermal unit to use.
void IceModel::allocate_bedrock_thermal_unit() {

  if (btu != NULL) {
    return;
  }

  m_log->message(2,
             "# Allocating a bedrock thermal layer model...\n");

  btu = new energy::BedThermalUnit(m_grid);
}

//! \brief Decide which subglacial hydrology model to use.
void IceModel::allocate_subglacial_hydrology() {

  using namespace pism::hydrology;

  std::string hydrology_model = config->get_string("hydrology_model");

  if (subglacial_hydrology != NULL) { // indicates it has already been allocated
    return;
  }

  m_log->message(2,
             "# Allocating a subglacial hydrology model...\n");

  if (hydrology_model == "null") {
    subglacial_hydrology = new NullTransport(m_grid);
  } else if (hydrology_model == "routing") {
    subglacial_hydrology = new Routing(m_grid);
  } else if (hydrology_model == "distributed") {
    subglacial_hydrology = new Distributed(m_grid, stress_balance);
  } else {
    throw RuntimeError::formatted("unknown value for configuration string 'hydrology_model':\n"
                                  "has value '%s'", hydrology_model.c_str());
  }
}

//! \brief Decide which basal yield stress model to use.
void IceModel::allocate_basal_yield_stress() {

  if (basal_yield_stress_model != NULL) {
    return;
  }

  m_log->message(2,
             "# Allocating a basal yield stress model...\n");

  std::string model = config->get_string("stress_balance_model");

  // only these two use the yield stress (so far):
  if (model == "ssa" || model == "ssa+sia") {
    std::string yield_stress_model = config->get_string("yield_stress_model");

    if (yield_stress_model == "constant") {
      basal_yield_stress_model = new ConstantYieldStress(m_grid);
    } else if (yield_stress_model == "mohr_coulomb") {
      basal_yield_stress_model = new MohrCoulombYieldStress(m_grid, subglacial_hydrology);
    } else {
      throw RuntimeError::formatted("yield stress model '%s' is not supported.",
                                    yield_stress_model.c_str());
    }
  }
}

//! Allocate PISM's sub-models implementing some physical processes.
/*!
  This method is called after memory allocation but before filling any of
  IceModelVecs because all the physical parameters should be initialized before
  setting up the coupling or filling model-state variables.
 */
void IceModel::allocate_submodels() {

  // FIXME: someday we will have an "energy balance" sub-model...
  if (config->get_boolean("do_energy") == true) {
    if (config->get_boolean("do_cold_ice_methods") == false) {
      m_log->message(2,
                 "* Using the enthalpy-based energy balance model...\n");
    } else {
      m_log->message(2,
                 "* Using the temperature-based energy balance model...\n");
    }
  }

  allocate_iceberg_remover();

  allocate_stressbalance();

  // this has to happen *after* allocate_stressbalance()
  allocate_subglacial_hydrology();

  // this has to happen *after* allocate_subglacial_hydrology()
  allocate_basal_yield_stress();

  allocate_bedrock_thermal_unit();

  allocate_bed_deformation();

  allocate_couplers();
}


void IceModel::allocate_couplers() {
  // Initialize boundary models:
  atmosphere::Factory pa(m_grid);
  surface::Factory ps(m_grid);
  ocean::Factory po(m_grid);
  atmosphere::AtmosphereModel *atmosphere;

  if (surface == NULL) {

    m_log->message(2,
             "# Allocating a surface process model or coupler...\n");

    surface = ps.create();
    external_surface_model = false;

    atmosphere = pa.create();
    surface->attach_atmosphere_model(atmosphere);
  }

  if (ocean == NULL) {
    m_log->message(2,
             "# Allocating an ocean model or coupler...\n");

    ocean = po.create();
    external_ocean_model = false;
  }
}

//! Initializes atmosphere and ocean couplers.
void IceModel::init_couplers() {

  m_log->message(3,
             "Initializing boundary models...\n");

  assert(surface != NULL);
  surface->init();

  assert(ocean != NULL);
  ocean->init();
}


//! Some sub-models need fields provided by surface and ocean models
//! for initialization, so here we call update() to make sure that
//! surface and ocean models report a decent state
void IceModel::init_step_couplers() {

  const double
    now               = m_time->current(),
    one_year_from_now = m_time->increment_date(now, 1.0);

  // Take a one year long step if we can.
  MaxTimestep max_dt(one_year_from_now - now);

  assert(surface != NULL);
  max_dt = std::min(max_dt, surface->max_timestep(now));

  assert(ocean != NULL);
  max_dt = std::min(max_dt, ocean->max_timestep(now));

  // Do not take time-steps shorter than 1 second
  if (max_dt.value() < 1.0) {
    max_dt = MaxTimestep(1.0);
  }

  assert(max_dt.is_finite() == true);

  surface->update(now, max_dt.value());
  ocean->update(now, max_dt.value());
}


//! Allocates work vectors.
void IceModel::allocate_internal_objects() {
  const unsigned int WIDE_STENCIL = config->get_double("grid_max_stencil_width");

  // various internal quantities
  // 2d work vectors
  for (int j = 0; j < nWork2d; j++) {
    char namestr[30];
    snprintf(namestr, sizeof(namestr), "work_vector_%d", j);
    vWork2d[j].create(m_grid, namestr, WITH_GHOSTS, WIDE_STENCIL);
  }

  // 3d work vectors
  vWork3d.create(m_grid,"work_vector_3d",WITHOUT_GHOSTS);
  vWork3d.set_attrs("internal",
                    "e.g. new values of temperature or age or enthalpy during time step",
                    "", "");
}


//! Miscellaneous initialization tasks plus tasks that need the fields that can come from regridding.
void IceModel::misc_setup() {

  m_log->message(3, "Finishing initialization...\n");

  output_vars = output_size_from_option("-o_size", "Sets the 'size' of an output file.",
                                        "medium");

  // Quietly re-initialize couplers (they might have done one
  // time-step during initialization)
  {
    int user_verbosity = getVerbosityLevel();
    setVerbosityLevel(1);
    init_couplers();
    setVerbosityLevel(user_verbosity);
  }

  init_calving();
  init_diagnostics();
  init_snapshots();
  init_backups();
  init_timeseries();
  init_extras();
  init_viewers();

  // Make sure that we use the output_variable_order that works with NetCDF-4,
  // "quilt", and HDF5 parallel I/O. (For different reasons, but mainly because
  // it is faster.)
  std::string o_format = config->get_string("output_format");
  if ((o_format == "netcdf4_parallel" || o_format == "quilt" || o_format == "hdf5") &&
      config->get_string("output_variable_order") != "xyz") {
    throw RuntimeError("output formats netcdf4_parallel, quilt, and hdf5 require -o_order xyz.");
  }
}

//! \brief Initialize calving mechanisms.
void IceModel::init_calving() {

  std::istringstream arg(config->get_string("calving_methods"));
  std::string method_name;
  std::set<std::string> methods;

    while (getline(arg, method_name, ',')) {
      methods.insert(method_name);
    }

  if (methods.find("ocean_kill") != methods.end()) {

    if (ocean_kill_calving == NULL) {
      ocean_kill_calving = new calving::OceanKill(m_grid);
    }

    ocean_kill_calving->init();
    methods.erase("ocean_kill");
  }

  if (methods.find("thickness_calving") != methods.end()) {

    if (thickness_threshold_calving == NULL) {
      thickness_threshold_calving = new calving::CalvingAtThickness(m_grid);
    }

    thickness_threshold_calving->init();
    methods.erase("thickness_calving");
  }


  if (methods.find("eigen_calving") != methods.end()) {

    if (eigen_calving == NULL) {
      eigen_calving = new calving::EigenCalving(m_grid, stress_balance);
    }

    eigen_calving->init();
    methods.erase("eigen_calving");
  }

  if (methods.find("float_kill") != methods.end()) {
    if (float_kill_calving == NULL) {
      float_kill_calving = new calving::FloatKill(m_grid);
    }

    float_kill_calving->init();
    methods.erase("float_kill");
  }

  std::set<std::string>::iterator j = methods.begin();
  std::string unused;
  while (j != methods.end()) {
    unused += (*j + ",");
    ++j;
  }

  if (unused.empty() == false) {
    m_log->message(2,
               "PISM ERROR: calving method(s) [%s] are unknown and are ignored.\n",
               unused.c_str());
  }
}

void IceModel::allocate_bed_deformation() {
  std::string model = config->get_string("bed_deformation_model");

  if (beddef != NULL) {
    return;
  }

  m_log->message(2,
             "# Allocating a bed deformation model...\n");

  if (model == "none") {
    beddef = new bed::PBNull(m_grid);
    return;
  }

  if (model == "iso") {
    beddef = new bed::PBPointwiseIsostasy(m_grid);
    return;
  }

  if (model == "lc") {
    beddef = new bed::PBLingleClark(m_grid);
    return;
  }
}

} // end of namespace pism
