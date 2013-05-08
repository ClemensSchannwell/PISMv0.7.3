/* Copyright (C) 2013 PISM Authors
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

#include "PSCache.hh"
#include "PISMTime.hh"
#include "pism_options.hh"

PSCache::PSCache(IceGrid &g, const NCConfigVariable &conf, PISMSurfaceModel* in)
  : PSModifier(g, conf, in) {

  m_next_update_time = grid.time->current();
  m_update_interval_years = 10;

  PetscErrorCode ierr = allocate_PSCache();
  if (ierr != 0) {
    PetscPrintf(grid.com, "PISM ERROR: failed to allocate storage for PSCache.\n");
    PISMEnd();
  }
}

PetscErrorCode PSCache::allocate_PSCache() {
  PetscErrorCode ierr;

  ierr = m_mass_flux.create(grid, "climatic_mass_balance", false); CHKERRQ(ierr);
  ierr = m_mass_flux.set_attrs("climate_state",
                               "ice-equivalent surface mass balance (accumulation/ablation) rate",
                               "m s-1",
                               "land_ice_surface_specific_mass_balance"); CHKERRQ(ierr);
  ierr = m_mass_flux.set_glaciological_units("m year-1"); CHKERRQ(ierr);
  m_mass_flux.write_in_glaciological_units = true;

  ierr = m_temperature.create(grid, "ice_surface_temp", false); CHKERRQ(ierr);
  ierr = m_temperature.set_attrs("climate_state",
                                 "ice temperature at the ice surface",
                                 "K", ""); CHKERRQ(ierr);

  ierr = m_liquid_water_fraction.create(grid, "ice_surface_liquid_water_fraction", false); CHKERRQ(ierr);
  ierr = m_liquid_water_fraction.set_attrs("diagnostic",
                                           "ice surface liquid water fraction", "1", ""); CHKERRQ(ierr);

  ierr = m_mass_held_in_surface_layer.create(grid, "mass_held_in_surface_layer", false); CHKERRQ(ierr);
  ierr = m_mass_held_in_surface_layer.set_attrs("diagnostic",
                                                "mass held in surface layer", "kg", ""); CHKERRQ(ierr);

  ierr = m_surface_layer_thickness.create(grid, "surface_layer_thickness", false); CHKERRQ(ierr);
  ierr = m_surface_layer_thickness.set_attrs("diagnostic",
                                             "surface layer thickness", "1", ""); CHKERRQ(ierr);

  return 0;
}

PSCache::~PSCache() {
  // empty
}


PetscErrorCode PSCache::init(PISMVars &vars) {
  PetscErrorCode ierr;
  int update_interval = m_update_interval_years;
  bool flag;

  ierr = input_model->init(vars); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "* Initializing the 'caching' surface model modifier...\n"); CHKERRQ(ierr);

  ierr = PetscOptionsBegin(grid.com, "", "-surface ...,cache options", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsInt("-surface_cache_update_interval",
                          "Interval (in years) between surface model updates",
                          update_interval, flag); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  if (update_interval <= 0) {
    PetscPrintf(grid.com,
                "PISM ERROR: -surface_cache_update_interval has to be strictly positive.\n");
    PISMEnd();
  }

  m_update_interval_years = update_interval;
  m_next_update_time = grid.time->current();

  return 0;
}

PetscErrorCode PSCache::update(PetscReal my_t, PetscReal my_dt) {
  PetscErrorCode ierr;

  if (my_t + my_dt > m_next_update_time) {
    ierr = input_model->update(my_t + 0.5*my_dt,
                               grid.convert(1.0, "year", "seconds")); CHKERRQ(ierr);

    m_next_update_time = grid.time->increment_date(m_next_update_time,
                                                   m_update_interval_years);

    ierr = input_model->ice_surface_mass_flux(m_mass_flux);                         CHKERRQ(ierr); 
    ierr = input_model->ice_surface_temperature(m_temperature);                     CHKERRQ(ierr); 
    ierr = input_model->ice_surface_liquid_water_fraction(m_liquid_water_fraction); CHKERRQ(ierr); 
    ierr = input_model->mass_held_in_surface_layer(m_mass_held_in_surface_layer);   CHKERRQ(ierr); 
    ierr = input_model->surface_layer_thickness(m_surface_layer_thickness);         CHKERRQ(ierr); 
  }

  return 0;
}

PetscErrorCode PSCache::ice_surface_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = m_mass_flux.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PSCache::ice_surface_temperature(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = m_temperature.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PSCache::ice_surface_liquid_water_fraction(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = m_liquid_water_fraction.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PSCache::mass_held_in_surface_layer(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = m_mass_held_in_surface_layer.copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PSCache::surface_layer_thickness(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = m_surface_layer_thickness.copy_to(result); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PSCache::define_variables(set<string> vars, const PIO &nc, PISM_IO_Type nctype) {
  PetscErrorCode ierr;

  if (set_contains(vars, m_mass_flux.string_attr("short_name"))) {
    ierr = m_mass_flux.define(nc, nctype); CHKERRQ(ierr);
    vars.erase(m_mass_flux.string_attr("short_name"));
  }

  if (set_contains(vars, m_temperature.string_attr("short_name"))) {
    ierr = m_temperature.define(nc, nctype); CHKERRQ(ierr);
    vars.erase(m_temperature.string_attr("short_name"));
  }

  if (set_contains(vars, m_liquid_water_fraction.string_attr("short_name"))) {
    ierr = m_liquid_water_fraction.define(nc, nctype); CHKERRQ(ierr);
    vars.erase(m_liquid_water_fraction.string_attr("short_name"));
  }

  if (set_contains(vars, m_mass_held_in_surface_layer.string_attr("short_name"))) {
    ierr = m_mass_held_in_surface_layer.define(nc, nctype); CHKERRQ(ierr);
    vars.erase(m_mass_held_in_surface_layer.string_attr("short_name"));
  }

  if (set_contains(vars, m_surface_layer_thickness.string_attr("short_name"))) {
    ierr = m_surface_layer_thickness.define(nc, nctype); CHKERRQ(ierr);
    vars.erase(m_surface_layer_thickness.string_attr("short_name"));
  }

  ierr = input_model->define_variables(vars, nc, nctype); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PSCache::write_variables(set<string> vars, const PIO &nc) {
  PetscErrorCode ierr;

  if (set_contains(vars, m_mass_flux.string_attr("short_name"))) {
    ierr = m_mass_flux.write(nc); CHKERRQ(ierr);
    vars.erase(m_mass_flux.string_attr("short_name"));
  }

  if (set_contains(vars, m_temperature.string_attr("short_name"))) {
    ierr = m_temperature.write(nc); CHKERRQ(ierr);
    vars.erase(m_temperature.string_attr("short_name"));
  }

  if (set_contains(vars, m_liquid_water_fraction.string_attr("short_name"))) {
    ierr = m_liquid_water_fraction.write(nc); CHKERRQ(ierr);
    vars.erase(m_liquid_water_fraction.string_attr("short_name"));
  }

  if (set_contains(vars, m_mass_held_in_surface_layer.string_attr("short_name"))) {
    ierr = m_mass_held_in_surface_layer.write(nc); CHKERRQ(ierr);
    vars.erase(m_mass_held_in_surface_layer.string_attr("short_name"));
  }

  if (set_contains(vars, m_surface_layer_thickness.string_attr("short_name"))) {
    ierr = m_surface_layer_thickness.write(nc); CHKERRQ(ierr);
    vars.erase(m_surface_layer_thickness.string_attr("short_name"));
  }

  ierr = input_model->write_variables(vars, nc); CHKERRQ(ierr);

  return 0;
}
