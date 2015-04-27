/* Copyright (C) 2015 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <cassert>
#include <mpi.h>

#include "base/util/io/PIO.hh"
#include "PISMConfigInterface.hh"
#include "PISMUnits.hh"
#include "pism_const.hh"
#include "pism_options.hh"
#include "error_handling.hh"

namespace pism {

struct Config::Impl {
  Impl(units::System::Ptr sys)
    : unit_system(sys) {
    // empty
  }
  //! Unit system. @fixme: this should be moved to the Context class.
  units::System::Ptr unit_system;

  std::string filename;

  //! @brief Set of parameters set by the user. Used to warn about parameters that were set but were
  //! not used.
  std::set<std::string> parameters_set_by_user;
  //! @brief Set of parameters used in a run. Used to warn about parameters that were set but were
  //! not used.
  std::set<std::string> parameters_used;
};

Config::Config(units::System::Ptr system)
  : m_impl(new Impl(system)) {
  // empty
}

Config::~Config() {
  delete m_impl;
}

units::System::Ptr Config::unit_system() const {
  return m_impl->unit_system;
}

void Config::read(MPI_Comm com, const std::string &file) {

  PIO nc(com, "netcdf3"); // OK to use netcdf3

  nc.open(file, PISM_READONLY);

  this->read(nc);

  nc.close();
}

void Config::read(const PIO &nc) {
  this->read_impl(nc);

  m_impl->filename = nc.inq_filename();
}

void Config::write(const PIO &nc) const {
  this->write_impl(nc);
}

void Config::write(MPI_Comm com, const std::string &file, bool append) const {

  PIO nc(com, "netcdf3"); // OK to use netcdf3

  IO_Mode mode = append ? PISM_READWRITE : PISM_READWRITE_MOVE;

  nc.open(file, mode);

  this->write(nc);

  nc.close();
}

//! \brief Returns the name of the file used to initialize the database.
std::string Config::filename() const {
  return m_impl->filename;
}

void Config::import_from(const Config &other) {
  Doubles doubles = other.all_doubles();
  Strings strings = other.all_strings();
  Booleans booleans = other.all_booleans();

  Doubles::const_iterator i;
  for (i = doubles.begin(); i != doubles.end(); ++i) {
    this->set_double(i->first, i->second, USER);
  }

  Strings::const_iterator j;
  for (j = strings.begin(); j != strings.end(); ++j) {
    this->set_string(j->first, j->second, USER);
  }

  Booleans::const_iterator k;
  for (k = booleans.begin(); k != booleans.end(); ++k) {
    this->set_boolean(k->first, k->second, USER);
  }
}

const std::set<std::string>& Config::parameters_set_by_user() const {
  return m_impl->parameters_set_by_user;
}

const std::set<std::string>& Config::parameters_used() const {
  return m_impl->parameters_used;
}

bool Config::is_set(const std::string &name) const {
  return this->is_set_impl(name);
}

Config::Doubles Config::all_doubles() const {
  return this->all_doubles_impl();
}

double Config::get_double(const std::string &name, UseFlag flag) const {
  if (flag == REMEMBER_THIS_USE) {
    m_impl->parameters_used.insert(name);
  }
  return this->get_double_impl(name);
}

double Config::get_double(const std::string &name,
                          const std::string &u1, const std::string &u2,
                          UseFlag flag) const {
  double value = this->get_double(name, flag);
  return units::convert(m_impl->unit_system, value, u1, u2);
}

void Config::set_double(const std::string &name, double value,
                        Config::SettingFlag flag) {
  std::set<std::string> &set_by_user = m_impl->parameters_set_by_user;

  if (flag == USER) {
    set_by_user.insert(name);
  }

  // stop if we're setting the default value and this parameter was set by user already
  if (flag == DEFAULT and
      set_by_user.find(name) != set_by_user.end()) {
    return;
  }

  this->set_double_impl(name, value);
}

Config::Strings Config::all_strings() const {
  return this->all_strings_impl();
}

std::string Config::get_string(const std::string &name, UseFlag flag) const {
  if (flag == REMEMBER_THIS_USE) {
    m_impl->parameters_used.insert(name);
  }
  return this->get_string_impl(name);
}

void Config::set_string(const std::string &name,
                        const std::string &value,
                        Config::SettingFlag flag) {
  std::set<std::string> &set_by_user = m_impl->parameters_set_by_user;

  if (flag == USER) {
    set_by_user.insert(name);
  }

  // stop if we're setting the default value and this parameter was set by user already
  if (flag == DEFAULT and
      set_by_user.find(name) != set_by_user.end()) {
    return;
  }

  this->set_string_impl(name, value);
}

Config::Booleans Config::all_booleans() const {
  return this->all_booleans_impl();
}

bool Config::get_boolean(const std::string& name, UseFlag flag) const {
  if (flag == REMEMBER_THIS_USE) {
    m_impl->parameters_used.insert(name);
  }
  return this->get_boolean_impl(name);
}

void Config::set_boolean(const std::string& name, bool value,
                         Config::SettingFlag flag) {
  std::set<std::string> &set_by_user = m_impl->parameters_set_by_user;

  if (flag == USER) {
    set_by_user.insert(name);
  }

  // stop if we're setting the default value and this parameter was set by user already
  if (flag == DEFAULT and
      set_by_user.find(name) != set_by_user.end()) {
    return;
  }

  this->set_boolean_impl(name, value);
}

void print_config(int verbosity_threshhold, MPI_Comm com, const Config &config) {
  const int v = verbosity_threshhold;

  verbPrintf(v, com,
             "### Strings:\n"
             "###\n");

  Config::Strings strings = config.all_strings();
  Config::Strings::const_iterator j;
  for (j = strings.begin(); j != strings.end(); ++j) {
    std::string name  = j->first;
    std::string value = j->second;

    if (value.empty() or ends_with(name, "_doc") or ends_with(name, "_units")) {
      continue;
    }

    verbPrintf(v, com, "  %s = \"%s\"\n", name.c_str(), value.c_str());
  }

  verbPrintf(v, com,
             "### Doubles:\n"
             "###\n");

  Config::Doubles doubles = config.all_doubles();
  Config::Doubles::const_iterator k;
  for (k = doubles.begin(); k != doubles.end(); ++k) {
    std::string name  = k->first;
    double value = k->second;
    std::string units = strings[name + "_units"]; // will be empty if not set

    if (fabs(value) >= 1.0e7 or fabs(value) <= 1.0e-4) {
      // use scientific notation if a number is big or small
      verbPrintf(v, com, "  %s = %12.3e (%s)\n", name.c_str(), value, units.c_str());
    } else {
      verbPrintf(v, com, "  %s = %12.5f (%s)\n", name.c_str(), value, units.c_str());
    }
  }

  verbPrintf(v, com,
             "### Booleans:\n"
             "###\n");

  Config::Booleans booleans = config.all_booleans();
  Config::Booleans::const_iterator p;
  for (p = booleans.begin(); p != booleans.end(); ++p) {
    std::string name  = p->first;
    std::string value = p->second ? "true" : "false";

    verbPrintf(v, com, "  %s = %s\n", name.c_str(), value.c_str());
  }

  verbPrintf(v, com,
             "### List of configuration parameters ends here.\n"
             "###\n");
}

void print_unused_parameters(int verbosity_threshhold, MPI_Comm com,
                             const Config &config) {
  std::set<std::string> parameters_set = config.parameters_set_by_user();
  std::set<std::string> parameters_used = config.parameters_used();

  if (options::Bool("-options_left", "report unused options")) {
    verbosity_threshhold = getVerbosityLevel();
  }

  std::set<std::string>::const_iterator k;
  for (k = parameters_set.begin(); k != parameters_set.end(); ++k) {

    if (ends_with(*k, "_doc")) {
      continue;
    }

    if (parameters_used.find(*k) == parameters_used.end()) {
      verbPrintf(verbosity_threshhold, com,
                 "PISM WARNING: flag or parameter \"%s\" was set but was not used!\n",
                 k->c_str());

    }
  }
}

// command-line options

//! Get a flag from a command-line option.
/*!
  If called as `boolean_from_option("foo", "foo")`, checks both `-foo` and `-no_foo`.

  - if `-foo` is set, calls `set_boolean("foo", true)`,

  - if `-no_foo` is set, calls `set_boolean("foo", false)`,

  - if *both* are set, prints an error message and stops,

  - if none, does nothing.

*/
void set_boolean_from_option(Config &config, const std::string &name, const std::string &flag) {

  bool foo    = options::Bool("-" + name,
                              config.get_string(flag + "_doc", Config::FORGET_THIS_USE));
  bool no_foo = options::Bool("-no_" + name,
                              config.get_string(flag + "_doc", Config::FORGET_THIS_USE));

  if (foo and no_foo) {
    throw RuntimeError::formatted("Inconsistent command-line options: both -%s and -no_%s are set.\n",
                                  name.c_str(), name.c_str());
  }

  if (foo) {
    config.set_boolean(flag, true, Config::USER);
  }

  if (no_foo) {
    config.set_boolean(flag, false, Config::USER);
  }
}

//! Sets a configuration parameter from a command-line option.
/*!
  If called as scalar_from_option("foo", "foo"), checks -foo and calls set("foo", value).

  Does nothing if -foo was not set.

  Note that no unit conversion is performed; parameters should be stored in
  input units and converted as needed. (This allows saving parameters without
  converting again.)
*/
void set_scalar_from_option(Config &config, const std::string &name, const std::string &parameter) {
  options::Real option("-" + name,
                       config.get_string(parameter + "_doc", Config::FORGET_THIS_USE),
                       config.get_double(parameter, Config::FORGET_THIS_USE));
  if (option.is_set()) {
    config.set_double(parameter, option, Config::USER);
  }
}

void set_string_from_option(Config &config, const std::string &name, const std::string &parameter) {

  options::String value("-" + name,
                        config.get_string(parameter + "_doc", Config::FORGET_THIS_USE),
                        config.get_string(parameter, Config::FORGET_THIS_USE));
  if (value.is_set()) {
    config.set_string(parameter, value, Config::USER);
  }
}

//! \brief Set a keyword parameter from a command-line option.
/*!
 * This sets the parameter "parameter" after checking the "-name" command-line
 * option. This option requires an argument, which has to match one of the
 * keyword given in a comma-separated list "choices_list".
 */
void set_keyword_from_option(Config &config, const std::string &name,
                             const std::string &parameter,
                             const std::string &choices) {

  options::Keyword keyword("-" + name,
                           config.get_string(parameter + "_doc", Config::FORGET_THIS_USE),
                           choices,
                           config.get_string(parameter, Config::FORGET_THIS_USE));

  if (keyword.is_set()) {
    config.set_string(parameter, keyword, Config::USER);
  }
}

void set_parameter_from_options(Config &config, const std::string &name) {

  if (not config.is_set(name + "_option")) {
    return;
  }

  std::string option = config.get_string(name + "_option");

  std::string type = "string";
  if (config.is_set(name + "_type")) {
    // will get marked as "used", but that's OK
    type = config.get_string(name + "_type");
  }

  if (type == "string") {
    set_string_from_option(config, option, name);
  } else if (type == "boolean") {
    set_boolean_from_option(config, option, name);
  } else if (type == "scalar") {
    set_scalar_from_option(config, option, name);
  } else if (type == "keyword") {
    // will be marked as "used" and will fail if not set
    std::string choices = config.get_string(name + "_choices");

    set_keyword_from_option(config, option, name, choices);
  } else {
    throw RuntimeError::formatted("parameter type \"%s\" is invalid", type.c_str());
  }
}

void set_config_from_options(Config &config) {

  set_keyword_from_option(config, "periodicity", "grid_periodicity", "none,x,y,xy");
  set_keyword_from_option(config, "z_spacing", "grid_ice_vertical_spacing", "quadratic,equal");

  // Energy modeling
  set_boolean_from_option(config, "use_Kirchhoff_law", "use_Kirchhoff_law");
  set_boolean_from_option(config, "varc", "use_linear_in_temperature_heat_capacity");
  set_boolean_from_option(config, "vark",
                            "use_temperature_dependent_thermal_conductivity");

  set_boolean_from_option(config, "bmr_in_cont", "include_bmr_in_continuity");

  {
    options::Keyword energy("-energy",
                            "choose the energy model (one of 'none', 'cold', 'enthalpy')",
                            "none,cold,enthalpy", "enthalpy");

    if (energy.is_set()) {
      if (energy == "none") {
        config.set_boolean("do_energy", false, Config::USER);
        // Allow selecting cold ice flow laws in isothermal mode.
        config.set_boolean("do_cold_ice_methods", true, Config::USER);
      } else if (energy == "cold") {
        config.set_boolean("do_energy", true, Config::USER);
        config.set_boolean("do_cold_ice_methods", true, Config::USER);
      } else if (energy == "enthalpy") {
        config.set_boolean("do_energy", true, Config::USER);
        config.set_boolean("do_cold_ice_methods", false, Config::USER);
      } else {
        throw RuntimeError("this can't happen: options::Keyword validates input");
      }
    }
  }

  // at bootstrapping, choose whether the method uses smb as upper boundary for
  // vertical velocity
  set_keyword_from_option(config, "boot_temperature_heuristic",
                            "bootstrapping_temperature_heuristic", "smb,quartic_guess");

  set_scalar_from_option(config, "low_temp", "global_min_allowed_temp");
  set_scalar_from_option(config, "max_low_temps", "max_low_temp_count");

  // Sub-models
  set_boolean_from_option(config, "age", "do_age");
  set_boolean_from_option(config, "mass", "do_mass_conserve");

  // hydrology
  set_keyword_from_option(config, "hydrology", "hydrology_model",
                            "null,routing,distributed");
  set_boolean_from_option(config, "hydrology_use_const_bmelt",
                            "hydrology_use_const_bmelt");
  set_scalar_from_option(config, "hydrology_const_bmelt",
                           "hydrology_const_bmelt");
  set_scalar_from_option(config, "hydrology_tillwat_max",
                           "hydrology_tillwat_max");
  set_scalar_from_option(config, "hydrology_tillwat_decay_rate",
                           "hydrology_tillwat_decay_rate");
  set_scalar_from_option(config, "hydrology_hydraulic_conductivity",
                           "hydrology_hydraulic_conductivity");
  set_scalar_from_option(config, "hydrology_thickness_power_in_flux",
                           "hydrology_thickness_power_in_flux");
  set_scalar_from_option(config, "hydrology_gradient_power_in_flux",
                           "hydrology_gradient_power_in_flux");
  // additional to hydrology::Routing, these apply to hydrology::Distributed:
  set_scalar_from_option(config, "hydrology_roughness_scale",
                           "hydrology_roughness_scale");
  set_scalar_from_option(config, "hydrology_cavitation_opening_coefficient",
                           "hydrology_cavitation_opening_coefficient");
  set_scalar_from_option(config, "hydrology_creep_closure_coefficient",
                           "hydrology_creep_closure_coefficient");
  set_scalar_from_option(config, "hydrology_regularizing_porosity",
                           "hydrology_regularizing_porosity");

  // Time-stepping
  set_keyword_from_option(config, "calendar", "calendar",
                            "standard,gregorian,proleptic_gregorian,noleap,365_day,360_day,julian,none");

  set_scalar_from_option(config, "adapt_ratio",
                           "adaptive_timestepping_ratio");

  set_scalar_from_option(config, "timestep_hit_multiples",
                           "timestep_hit_multiples");

  set_boolean_from_option(config, "count_steps", "count_time_steps");
  set_scalar_from_option(config, "max_dt", "maximum_time_step_years");


  // SIA-related
  set_scalar_from_option(config, "bed_smoother_range", "bed_smoother_range");

  set_keyword_from_option(config, "gradient", "surface_gradient_method",
                            "eta,haseloff,mahaffy");

  // rheology-related
  set_scalar_from_option(config, "sia_n", "sia_Glen_exponent");
  set_scalar_from_option(config, "ssa_n", "ssa_Glen_exponent");

  set_keyword_from_option(config, "sia_flow_law", "sia_flow_law",
                            "arr,arrwarm,gk,gpbld,hooke,isothermal_glen,pb");

  set_keyword_from_option(config, "ssa_flow_law", "ssa_flow_law",
                            "arr,arrwarm,gpbld,hooke,isothermal_glen,pb");

  set_scalar_from_option(config, "sia_e", "sia_enhancement_factor");
  set_scalar_from_option(config, "ssa_e", "ssa_enhancement_factor");

  set_boolean_from_option(config, "e_age_coupling", "e_age_coupling");

  // This parameter is used by the Goldsby-Kohlstedt flow law.
  set_scalar_from_option(config, "ice_grain_size", "ice_grain_size");

  set_boolean_from_option(config, "grain_size_age_coupling",
                            "compute_grain_size_using_age");

  // SSA
  // Decide on the algorithm for solving the SSA
  set_keyword_from_option(config, "ssa_method", "ssa_method", "fd,fem");

  set_scalar_from_option(config, "ssa_eps",  "epsilon_ssa");
  set_scalar_from_option(config, "ssa_maxi", "max_iterations_ssafd");
  set_scalar_from_option(config, "ssa_rtol", "ssafd_relative_convergence");

  set_scalar_from_option(config, "ssafd_nuH_iter_failure_underrelaxation", "ssafd_nuH_iter_failure_underrelaxation");

  set_boolean_from_option(config, "ssa_dirichlet_bc", "ssa_dirichlet_bc");
  set_boolean_from_option(config, "cfbc", "calving_front_stress_boundary_condition");

  // Basal sliding fiddles
  set_boolean_from_option(config, "brutal_sliding", "brutal_sliding");
  set_scalar_from_option(config, "brutal_sliding_scale","brutal_sliding_scale");

  set_scalar_from_option(config, "sliding_scale_factor_reduces_tauc",
                           "sliding_scale_factor_reduces_tauc");

  // SSA Inversion

  set_keyword_from_option(config, "inv_method","inv_ssa_method",
                            "sd,nlcg,ign,tikhonov_lmvm,tikhonov_cg,tikhonov_blmvm,tikhonov_lcl,tikhonov_gn");

  set_keyword_from_option(config, "inv_design_param",
                            "inv_design_param","ident,trunc,square,exp");

  set_scalar_from_option(config, "inv_target_misfit","inv_target_misfit");

  set_scalar_from_option(config, "tikhonov_penalty","tikhonov_penalty_weight");
  set_scalar_from_option(config, "tikhonov_atol","tikhonov_atol");
  set_scalar_from_option(config, "tikhonov_rtol","tikhonov_rtol");
  set_scalar_from_option(config, "tikhonov_ptol","tikhonov_ptol");

  set_keyword_from_option(config, "inv_state_func",
                            "inv_state_func",
                            "meansquare,log_ratio,log_relative");
  set_keyword_from_option(config, "inv_design_func",
                            "inv_design_func","sobolevH1,tv");

  set_scalar_from_option(config, "inv_design_cL2","inv_design_cL2");
  set_scalar_from_option(config, "inv_design_cH1","inv_design_cH1");
  set_scalar_from_option(config, "inv_ssa_tv_exponent","inv_ssa_tv_exponent");
  set_scalar_from_option(config, "inv_log_ratio_scale","inv_log_ratio_scale");

  // Basal strength
  set_scalar_from_option(config, "till_cohesion", "till_cohesion");
  set_scalar_from_option(config, "till_reference_void_ratio",
                           "till_reference_void_ratio");
  set_scalar_from_option(config, "till_compressibility_coefficient",
                           "till_compressibility_coefficient");
  set_scalar_from_option(config, "till_effective_fraction_overburden",
                           "till_effective_fraction_overburden");
  set_scalar_from_option(config, "till_log_factor_transportable_water",
                           "till_log_factor_transportable_water");

  // read the comma-separated list of four values
  options::RealList topg_to_phi("-topg_to_phi", "phi_min, phi_max, topg_min, topg_max");
  if (topg_to_phi.is_set()) {
    if (topg_to_phi->size() != 4) {
      throw RuntimeError::formatted("option -topg_to_phi requires a comma-separated list with 4 numbers; got %d",
                                    (int)topg_to_phi->size());
    }
    config.set_boolean("till_use_topg_to_phi", true);
    config.set_double("till_topg_to_phi_phi_min", topg_to_phi[0]);
    config.set_double("till_topg_to_phi_phi_max", topg_to_phi[1]);
    config.set_double("till_topg_to_phi_topg_min", topg_to_phi[2]);
    config.set_double("till_topg_to_phi_topg_max", topg_to_phi[3]);
  }

  set_boolean_from_option(config, "tauc_slippery_grounding_lines",
                            "tauc_slippery_grounding_lines");
  set_boolean_from_option(config, "tauc_add_transportable_water",
                            "tauc_add_transportable_water");

  set_keyword_from_option(config, "yield_stress", "yield_stress_model",
                            "constant,mohr_coulomb");

  // all basal strength models use this in ice-free areas
  set_scalar_from_option(config, "high_tauc", "high_tauc");

  // controls regularization of plastic basal sliding law
  set_scalar_from_option(config, "plastic_reg", "plastic_regularization");

  // "friction angle" in degrees. We allow -plastic_phi without an
  // argument: MohrCoulombYieldStress interprets that as "set
  // constant till friction angle using the default read from a config
  // file or an override file".
  bool plastic_phi_set = options::Bool("-plastic_phi", "use constant till_phi");
  if (plastic_phi_set) {
    set_scalar_from_option(config, "plastic_phi", "default_till_phi");
  }

  // use pseudo plastic instead of pure plastic; see iMbasal.cc
  set_boolean_from_option(config, "pseudo_plastic", "do_pseudo_plastic_till");

  // power in denominator on pseudo_plastic_uthreshold; typical is q=0.25; q=0 is pure plastic
  set_scalar_from_option(config, "pseudo_plastic_q", "pseudo_plastic_q");

  // threshold; at this velocity tau_c is basal shear stress
  set_scalar_from_option(config, "pseudo_plastic_uthreshold",
                           "pseudo_plastic_uthreshold");

  set_boolean_from_option(config, "subgl", "sub_groundingline");

  // Ice shelves
  set_boolean_from_option(config, "part_grid", "part_grid");

  set_boolean_from_option(config, "part_grid_reduce_frontal_thickness",
                            "part_grid_reduce_frontal_thickness");

  set_boolean_from_option(config, "part_redist", "part_redist");

  set_scalar_from_option(config, "nu_bedrock", "nu_bedrock");
  bool nu_bedrock = options::Bool("-nu_bedrock", "constant viscosity near margins");
  if (nu_bedrock) {
    config.set_boolean("nu_bedrock_set", true, Config::USER);
  }

  // fracture density
  set_boolean_from_option(config, "fractures", "do_fracture_density");
  set_boolean_from_option(config, "write_fd_fields", "write_fd_fields");
  set_scalar_from_option(config, "fracture_softening",
                           "fracture_density_softening_lower_limit");

  // Calving
  set_string_from_option(config, "calving", "calving_methods");

  set_scalar_from_option(config, "thickness_calving_threshold", "thickness_calving_threshold");

  // evaluates the adaptive timestep based on a CFL criterion with respect to the eigenCalving rate
  set_boolean_from_option(config, "cfl_eigen_calving", "cfl_eigen_calving");
  set_scalar_from_option(config, "eigen_calving_K", "eigen_calving_K");

  set_boolean_from_option(config, "kill_icebergs", "kill_icebergs");

  // Output
  set_keyword_from_option(config, "o_order", "output_variable_order",
                            "xyz,yxz,zyx");

  set_keyword_from_option(config, "o_format", "output_format",
                            "netcdf3,quilt,netcdf4_parallel,pnetcdf,hdf5");

  set_scalar_from_option(config, "summary_vol_scale_factor_log10",
                           "summary_vol_scale_factor_log10");
  set_scalar_from_option(config, "summary_area_scale_factor_log10",
                           "summary_area_scale_factor_log10");

  // Metadata
  set_string_from_option(config, "title", "run_title");
  set_string_from_option(config, "institution", "institution");

  // Skipping
  set_boolean_from_option(config, "skip", "do_skip");
  set_scalar_from_option(config, "skip_max", "skip_max");

  // Shortcuts

  // option "-pik" turns on a suite of PISMPIK effects (but NOT a calving choice,
  // and in particular NOT  "-calving eigen_calving")
  bool pik = options::Bool("-pik", "enable suite of PISM-PIK mechanisms");
  if (pik) {
    config.set_boolean("calving_front_stress_boundary_condition", true, Config::USER);
    config.set_boolean("part_grid", true, Config::USER);
    config.set_boolean("part_redist", true, Config::USER);
    config.set_boolean("kill_icebergs", true, Config::USER);
    config.set_boolean("sub_groundingline", true, Config::USER);
  }

  if (config.get_string("calving_methods").find("eigen_calving") != std::string::npos) {
    config.set_boolean("part_grid", true, Config::USER);
    // eigen-calving requires a wider stencil:
    config.set_double("grid_max_stencil_width", 3);
  }

  // all calving mechanisms require iceberg removal
  if (config.get_string("calving_methods").empty() == false) {
    config.set_boolean("kill_icebergs", true, Config::USER);
  }

  // kill_icebergs requires part_grid
  if (config.get_boolean("kill_icebergs")) {
    config.set_boolean("part_grid", true, Config::USER);
  }

  set_keyword_from_option(config, "stress_balance", "stress_balance_model",
                            "none,prescribed_sliding,sia,ssa,prescribed_sliding+sia,ssa+sia");

  bool test_climate_models = options::Bool("-test_climate_models",
                                           "Disable ice dynamics to test climate models");
  if (test_climate_models) {
    config.set_string("stress_balance_model", "none", Config::USER);
    config.set_boolean("do_energy", false, Config::USER);
    config.set_boolean("do_age", false, Config::USER);
    // let the user decide if they want to use "-no_mass" or not
  }

  set_keyword_from_option(config, "bed_def",
                            "bed_deformation_model", "none,iso,lc");
  set_boolean_from_option(config, "bed_def_lc_elastic_model", "bed_def_lc_elastic_model");

  set_boolean_from_option(config, "dry", "is_dry_simulation");

  set_boolean_from_option(config, "clip_shelf_base_salinity",
                            "ocean_three_equation_model_clip_salinity");

  set_scalar_from_option(config, "meltfactor_pik", "ocean_pik_melt_factor");

  // old options
  options::deprecated("-sliding_scale_brutal",
                      "-brutal_sliding' and '-brutal_sliding_scale");
  options::deprecated("-ssa_sliding", "-stress_balance ...");
  options::deprecated("-ssa_floating_only", "-stress_balance ...");
  options::deprecated("-sia", "-stress_balance ...");
  options::deprecated("-no_sia", "-stress_balance ...");
  options::deprecated("-hold_tauc", "-yield_stress constant");
  options::deprecated("-ocean_kill", "-calving ocean_kill -ocean_kill_file foo.nc");
  options::deprecated("-eigen_calving", "-calving eigen_calving -eigen_calving_K XXX");
  options::deprecated("-calving_at_thickness",
                      "-calving thickness_calving -thickness_calving_threshold XXX");
  options::deprecated("-float_kill", "-calving float_kill");
  options::deprecated("-no_energy", "-energy none");
  options::deprecated("-cold", "-energy cold");
}


} // end of namespace pism
