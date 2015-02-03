// Copyright (C) 2012, 2013, 2014, 2015 PISM Authors
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

#include "PACosineYearlyCycle.hh"
#include "Timeseries.hh"
#include "PISMTime.hh"
#include "pism_options.hh"
#include "PISMConfig.hh"
#include "IceGrid.hh"

#include "error_handling.hh"

namespace pism {
namespace atmosphere {

PACosineYearlyCycle::PACosineYearlyCycle(const IceGrid &g)
  : PAYearlyCycle(g), A(NULL) {
}

PACosineYearlyCycle::~PACosineYearlyCycle() {
  if (A != NULL) {
    delete A;
  }
}

void PACosineYearlyCycle::init() {

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  verbPrintf(2, m_grid.com,
             "* Initializing the 'cosine yearly cycle' atmosphere model (-atmosphere yearly_cycle)...\n");


  options::String input_file("-atmosphere_yearly_cycle_file",
                             "PACosineYearlyCycle input file name");
  options::String scaling_file("-atmosphere_yearly_cycle_scaling_file",
                               "PACosineYearlyCycle amplitude scaling input file name");

  if (not input_file.is_set()) {
    throw RuntimeError("Please specify an '-atmosphere yearly_cycle' input file\n"
                       "using the -atmosphere_yearly_cycle_file option.");
  }

  verbPrintf(2, m_grid.com,
             "  Reading mean annual air temperature, mean July air temperature, and\n"
             "  precipitation fields from '%s'...\n", input_file->c_str());

  m_air_temp_mean_annual.regrid(input_file, CRITICAL);
  m_air_temp_mean_july.regrid(input_file, CRITICAL);
  m_precipitation.regrid(input_file, CRITICAL);

  if (scaling_file.is_set()) {

    if (A == NULL) {
      A = new Timeseries(&m_grid, "amplitude_scaling",
                         m_config.get_string("time_dimension_name"));
      A->metadata().set_units("1");
      A->metadata().set_string("long_name", "cosine yearly cycle amplitude scaling");
      A->dimension_metadata().set_units(m_grid.time->units_string());
    }

    verbPrintf(2, m_grid.com,
               "  Reading cosine yearly cycle amplitude scaling from '%s'...\n",
               scaling_file->c_str());

    PIO nc(m_grid, "netcdf3");    // OK to use netcdf3
    nc.open(scaling_file, PISM_READONLY);
    {
      A->read(nc, m_grid.time);
    }
    nc.close();

  } else {
    if (A != NULL) {
      delete A;
    }
    A = NULL;
  }
}

MaxTimestep PACosineYearlyCycle::max_timestep_impl(double t) {
  (void) t;
  return MaxTimestep();
}

void PACosineYearlyCycle::update_impl(double my_t, double my_dt) {
  m_t = my_t;
  m_dt = my_dt;
}

void PACosineYearlyCycle::temp_snapshot(IceModelVec2S &result) {
  const double
    julyday_fraction = m_grid.time->day_of_the_year_to_day_fraction(m_snow_temp_july_day),
    T                = m_grid.time->year_fraction(m_t + 0.5 * m_dt) - julyday_fraction,
    cos_T            = cos(2.0 * M_PI * T);

  double scaling = 1.0;
  if (A != NULL) {
    scaling = (*A)(m_t + 0.5 * m_dt);
  }

  IceModelVec::AccessList list;
  list.add(result);
  list.add(m_air_temp_mean_annual);
  list.add(m_air_temp_mean_july);

  for (Points p(m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    result(i,j) = m_air_temp_mean_annual(i,j) + (m_air_temp_mean_july(i,j) - m_air_temp_mean_annual(i,j)) * scaling * cos_T;
  }
}

void PACosineYearlyCycle::init_timeseries(const std::vector<double> &ts) {

  PAYearlyCycle::init_timeseries(ts);

  if (A != NULL) {
    for (unsigned int k = 0; k < ts.size(); ++k) {
      m_cosine_cycle[k] *= (*A)(ts[k]);
    }
  }
}

} // end of namespace atmosphere
} // end of namespace pism
