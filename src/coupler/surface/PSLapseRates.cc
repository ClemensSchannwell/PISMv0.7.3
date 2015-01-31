// Copyright (C) 2011, 2012, 2013, 2014, 2015 PISM Authors
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

#include "PSLapseRates.hh"

namespace pism {

PSLapseRates::PSLapseRates(const IceGrid &g, SurfaceModel* in)
  : PLapseRates<SurfaceModel,PSModifier>(g, in),
    m_climatic_mass_balance(g.config.get_unit_system(), "climatic_mass_balance", m_grid),
    m_ice_surface_temp(g.config.get_unit_system(), "ice_surface_temp", m_grid) {
  m_smb_lapse_rate = 0;
  m_option_prefix = "-surface_lapse_rate";

  m_climatic_mass_balance.set_string("pism_intent", "diagnostic");
  m_climatic_mass_balance.set_string("long_name",
                  "surface mass balance (accumulation/ablation) rate");
  m_climatic_mass_balance.set_string("standard_name",
                  "land_ice_surface_specific_mass_balance_flux");
  m_climatic_mass_balance.set_units("kg m-2 s-1");
  m_climatic_mass_balance.set_glaciological_units("kg m-2 year-1");

  m_ice_surface_temp.set_string("pism_intent", "diagnostic");
  m_ice_surface_temp.set_string("long_name",
                              "ice temperature at the ice surface");
  m_ice_surface_temp.set_units("K");
}

PSLapseRates::~PSLapseRates() {
  // empty
}

void PSLapseRates::init() {

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  input_model->init();

  verbPrintf(2, m_grid.com,
             "  [using temperature and mass balance lapse corrections]\n");

  init_internal();

  m_smb_lapse_rate = options::Real("-smb_lapse_rate",
                                   "Elevation lapse rate for the surface mass balance,"
                                   " in m/year per km",
                                   m_smb_lapse_rate);

  verbPrintf(2, m_grid.com,
             "   ice upper-surface temperature lapse rate: %3.3f K per km\n"
             "   ice-equivalent surface mass balance lapse rate: %3.3f m/year per km\n",
             m_temp_lapse_rate, m_smb_lapse_rate);

  m_temp_lapse_rate = m_grid.convert(m_temp_lapse_rate, "K/km", "K/m");

  // convert from [m/year / km] to [kg m-2 / year / km]
  m_smb_lapse_rate *= m_config.get("ice_density");
  m_smb_lapse_rate = m_grid.convert(m_smb_lapse_rate,
                                    "(kg m-2) / year / km", "(kg m-2) / s / m");
}

void PSLapseRates::ice_surface_mass_flux(IceModelVec2S &result) {
  input_model->ice_surface_mass_flux(result);
  lapse_rate_correction(result, m_smb_lapse_rate);
}

void PSLapseRates::ice_surface_temperature(IceModelVec2S &result) {
  input_model->ice_surface_temperature(result);
  lapse_rate_correction(result, m_temp_lapse_rate);
}

void PSLapseRates::add_vars_to_output_impl(const std::string &keyword,
                                           std::set<std::string> &result) {
  if (keyword == "medium" || keyword == "big") {
    result.insert("ice_surface_temp");
    result.insert("climatic_mass_balance");
  }

  input_model->add_vars_to_output(keyword, result);
}

void PSLapseRates::define_variables_impl(const std::set<std::string> &vars,
                                         const PIO &nc, IO_Type nctype) {

  if (set_contains(vars, "ice_surface_temp")) {
    m_ice_surface_temp.define(nc, nctype, true);
  }

  if (set_contains(vars, "climatic_mass_balance")) {
    m_climatic_mass_balance.define(nc, nctype, true);
  }

  input_model->define_variables(vars, nc, nctype);
}

void PSLapseRates::write_variables_impl(const std::set<std::string> &vars_input,
                                        const PIO &nc) {
  std::set<std::string> vars = vars_input;

  if (set_contains(vars, "ice_surface_temp")) {
    IceModelVec2S tmp;
    tmp.create(m_grid, "ice_surface_temp", WITHOUT_GHOSTS);
    tmp.metadata() = m_ice_surface_temp;

    ice_surface_temperature(tmp);

    tmp.write(nc);

    vars.erase("ice_surface_temp");
  }

  if (set_contains(vars, "climatic_mass_balance")) {
    IceModelVec2S tmp;
    tmp.create(m_grid, "climatic_mass_balance", WITHOUT_GHOSTS);
    tmp.metadata() = m_climatic_mass_balance;

    ice_surface_mass_flux(tmp);
    tmp.write_in_glaciological_units = true;
    tmp.write(nc);

    vars.erase("climatic_mass_balance");
  }

  input_model->write_variables(vars, nc);
}

} // end of namespace pism
