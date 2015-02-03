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

#include "PALapseRates.hh"

namespace pism {
namespace atmosphere {

PALapseRates::PALapseRates(const IceGrid &g, AtmosphereModel* in)
  : PLapseRates<AtmosphereModel,PAModifier>(g, in),
    m_precipitation(g.config.get_unit_system(), "precipitation", g),
    m_air_temp(g.config.get_unit_system(), "air_temp", g) {
  m_precip_lapse_rate = 0;
  m_option_prefix     = "-atmosphere_lapse_rate";

  m_precipitation.set_string("pism_intent", "diagnostic");
  m_precipitation.set_string("long_name",
                           "ice-equivalent precipitation rate with a lapse-rate correction");
  m_precipitation.set_units("m s-1");
  m_precipitation.set_glaciological_units("m year-1");

  m_air_temp.set_string("pism_intent", "diagnostic");
  m_air_temp.set_string("long_name",
                      "near-surface air temperature with a lapse-rate correction");
  m_air_temp.set_units("K");

  m_surface = NULL;
}

PALapseRates::~PALapseRates() {
  // empty
}

void PALapseRates::init() {

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  input_model->init();

  verbPrintf(2, m_grid.com,
             "  [using air temperature and precipitation lapse corrections]\n");

  init_internal();

  m_precip_lapse_rate = options::Real("-precip_lapse_rate",
                                    "Elevation lapse rate for the surface mass balance,"
                                    " in m/year per km",
                                    m_precip_lapse_rate);

  verbPrintf(2, m_grid.com,
             "   air temperature lapse rate: %3.3f K per km\n"
             "   precipitation lapse rate:   %3.3f m/year per km\n",
             m_temp_lapse_rate, m_precip_lapse_rate);

  m_temp_lapse_rate = m_grid.convert(m_temp_lapse_rate, "K/km", "K/m");

  m_precip_lapse_rate = m_grid.convert(m_precip_lapse_rate, "m/year / km", "m/s / m");
}

void PALapseRates::mean_precipitation(IceModelVec2S &result) {
  input_model->mean_precipitation(result);
  lapse_rate_correction(result, m_precip_lapse_rate);
}

void PALapseRates::mean_annual_temp(IceModelVec2S &result) {
  input_model->mean_annual_temp(result);
  lapse_rate_correction(result, m_temp_lapse_rate);
}

void PALapseRates::begin_pointwise_access() {
  input_model->begin_pointwise_access();
  m_reference_surface.begin_access();
  m_surface->begin_access();
}

void PALapseRates::end_pointwise_access() {
  input_model->end_pointwise_access();
  m_reference_surface.end_access();
  m_surface->end_access();
}

void PALapseRates::init_timeseries(const std::vector<double> &ts) {
  input_model->init_timeseries(ts);

  m_ts_times = ts;

  m_reference_surface.init_interpolation(ts);

  m_surface = m_grid.variables().get_2d_scalar("surface_altitude");
}

void PALapseRates::temp_time_series(int i, int j, std::vector<double> &result) {
  std::vector<double> usurf(m_ts_times.size());

  input_model->temp_time_series(i, j, result);

  m_reference_surface.interp(i, j, usurf);

  for (unsigned int m = 0; m < m_ts_times.size(); ++m) {
    result[m] -= m_temp_lapse_rate * ((*m_surface)(i, j) - usurf[m]);
  }
}

void PALapseRates::precip_time_series(int i, int j, std::vector<double> &result) {
  std::vector<double> usurf(m_ts_times.size());

  input_model->precip_time_series(i, j, result);

  m_reference_surface.interp(i, j, usurf);

  for (unsigned int m = 0; m < m_ts_times.size(); ++m) {
    result[m] -= m_precip_lapse_rate * ((*m_surface)(i, j) - usurf[m]);
  }
}

void PALapseRates::temp_snapshot(IceModelVec2S &result) {
  input_model->temp_snapshot(result);
  lapse_rate_correction(result, m_temp_lapse_rate);
}

void PALapseRates::define_variables_impl(const std::set<std::string> &vars,
                                         const PIO &nc, IO_Type nctype) {

  if (set_contains(vars, "air_temp")) {
    m_air_temp.define(nc, nctype, true);
  }

  if (set_contains(vars, "precipitation")) {
    m_precipitation.define(nc, nctype, true);
  }

  input_model->define_variables(vars, nc, nctype);
}

void PALapseRates::write_variables_impl(const std::set<std::string> &vars_input, const PIO &nc) {
  std::set<std::string> vars = vars_input;

  if (set_contains(vars, "air_temp")) {
    IceModelVec2S tmp;
    tmp.create(m_grid, "air_temp", WITHOUT_GHOSTS);
    tmp.metadata() = m_air_temp;

    temp_snapshot(tmp);

    tmp.write(nc);

    vars.erase("air_temp");
  }

  if (set_contains(vars, "precipitation")) {
    IceModelVec2S tmp;
    tmp.create(m_grid, "precipitation", WITHOUT_GHOSTS);
    tmp.metadata() = m_precipitation;

    mean_precipitation(tmp);
    tmp.write_in_glaciological_units = true;
    tmp.write(nc);

    vars.erase("precipitation");
  }

  input_model->write_variables(vars, nc);
}

void PALapseRates::add_vars_to_output_impl(const std::string &keyword,
                                           std::set<std::string> &result) {
  input_model->add_vars_to_output(keyword, result);

  if (keyword == "medium" || keyword == "big") {
    result.insert("air_temp");
    result.insert("precipitation");
  }
}

} // end of namespace atmosphere
} // end of namespace pism
