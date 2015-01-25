// Copyright (C) 2004-2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cstring>
#include <petscsys.h>
#include <stdarg.h>
#include <stdlib.h>

#include "iceModel.hh"
#include "Mask.hh"
#include "PISMStressBalance.hh"
#include "PISMOcean.hh"
#include "enthalpyConverter.hh"
#include "PISMTime.hh"

#include "error_handling.hh"
#include "PISMBedDef.hh"
#include "IceGrid.hh"

namespace pism {

/*!
  Computes fraction of the base which is melted.

  Communication occurs here.

  FIXME: energyStats should use cell_area(i,j).
 */
double IceModel::compute_temperate_base_fraction(double ice_area) {
  double result = 0.0, meltarea = 0.0;
  const double a = grid.dx() * grid.dy() * 1e-3 * 1e-3; // area unit (km^2)

  IceModelVec2S &Enthbase = vWork2d[0];
  // use Enth3 to get stats
  Enth3.getHorSlice(Enthbase, 0.0);  // z=0 slice

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(ice_thickness);
  list.add(Enthbase);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.icy(i, j)) {
      // accumulate area of base which is at melt point
      if (EC->isTemperate(Enthbase(i,j), EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
        meltarea += a;
      }
    }
  }

  // communication
  result = GlobalSum(grid.com, meltarea);

  // normalize fraction correctly
  if (ice_area > 0.0) {
    result = result / ice_area;
  } else {
    result = 0.0;
  }
  return result;
}


/*!
  Computes fraction of the ice which is as old as the start of the run (original).
  Communication occurs here.

  FIXME: ageStats should use cell_area(i,j).
 */
double IceModel::compute_original_ice_fraction(double ice_volume) {

  double result = -1.0;  // result value if not do_age

  if (not config.get_flag("do_age")) {
    return result;  // leave now
  }

  const double a = grid.dx() * grid.dy() * 1e-3 * 1e-3, // area unit (km^2)
    currtime = grid.time->current(); // seconds

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(ice_thickness);
  list.add(age3);

  const double one_year = grid.convert(1.0, "year", "seconds");
  double original_ice_volume = 0.0;

  // compute local original volume
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.icy(i, j)) {
      // accumulate volume of ice which is original
      double *age = age3.get_column(i, j);
      const int  ks = grid.kBelowHeight(ice_thickness(i,j));
      for (int k = 1; k <= ks; k++) {
        // ice in segment is original if it is as old as one year less than current time
        if (0.5 * (age[k - 1] + age[k]) > currtime - one_year) {
          original_ice_volume += a * 1.0e-3 * (grid.z(k) - grid.z(k - 1));
        }
      }
    }
  }

  // communicate to turn into global original fraction
  result = GlobalSum(grid.com, original_ice_volume);

  // normalize fraction correctly
  if (ice_volume > 0.0) {
    result = result / ice_volume;
  } else {
    result = 0.0;
  }
  return result;
}


void IceModel::summary(bool tempAndAge) {

  // report CFL violations
  if (CFLviolcount > 0.0) {
    const double CFLviolpercent = 100.0 * CFLviolcount / (grid.Mx() * grid.My() * grid.Mz());
    // at default verbosity level, only report CFL viols if above:
    const double CFLVIOL_REPORT_VERB2_PERCENT = 0.1;
    if (CFLviolpercent > CFLVIOL_REPORT_VERB2_PERCENT ||
        getVerbosityLevel() > 2) {
      char tempstr[90] = "";
      snprintf(tempstr,90,
              "  [!CFL#=%d (=%5.2f%% of 3D grid)] ",
              CFLviolcount,CFLviolpercent);
      stdout_flags = tempstr + stdout_flags;
    }
  }

  // get maximum diffusivity
  double max_diffusivity = stress_balance->max_diffusivity();
  // get volumes in m^3 and areas in m^2
  double ice_volume = compute_ice_volume();
  double ice_area = compute_ice_area();

  double meltfrac = 0.0;
  if (tempAndAge or getVerbosityLevel() >= 3) {
    meltfrac = compute_temperate_base_fraction(ice_area);
  }

  // main report: 'S' line
  summaryPrintLine(false, tempAndAge, dt,
                   ice_volume, ice_area, meltfrac, max_diffusivity);
}


//! Print a line to stdout which summarizes the state of the modeled ice sheet at the end of the time step.
/*!
This method is for casual inspection of model behavior, and to provide the user
with some indication of the state of the run.  Use of DiagnosticTimeseries is
superior for precise analysis of model output.

Generally, two lines are printed to stdout, the first starting with a space
and the second starting with the character 'S' in the left-most column (column 1).

The first line shows flags for which processes executed, and the length of the
time step (and/or substeps under option -skip).  See IceModel::run()
for meaning of these flags.

If printPrototype is TRUE then the first line does not appear and
the second line has alternate appearance.  Specifically, different column 1
characters are printed:
  - 'P' line gives names of the quantities reported in the 'S' line, the
    "prototype", while
  - 'U' line gives units of these quantities.
This column 1 convention allows automatic tools to read PISM stdout
and produce time-series.  The 'P' and 'U' lines are intended to appear once at
the beginning of the run, while an 'S' line appears at every time step.

These quantities are reported in this base class version:
  - `time` is the current model time
  - `ivol` is the total ice sheet volume
  - `iarea` is the total area occupied by positive thickness ice
  - `max_diffusivity` is the maximum diffusivity
  - `max_hor_vel` is the maximum diffusivity

Configuration parameters `summary_time_unit_name`, `summary_vol_scale_factor_log10`,
and `summary_area_scale_factor_log10` control the appearance and units.

For more description and examples, see the PISM User's Manual.
Derived classes of IceModel may redefine this method and print alternate
information.
 */
void IceModel::summaryPrintLine(bool printPrototype,  bool tempAndAge,
                                double delta_t,
                                double volume,  double area,
                                double /* meltfrac */,  double max_diffusivity) {
  const bool do_energy = config.get_flag("do_energy");
  const int log10scalevol  = static_cast<int>(config.get("summary_vol_scale_factor_log10")),
            log10scalearea = static_cast<int>(config.get("summary_area_scale_factor_log10"));
  const std::string tunitstr = config.get_string("summary_time_unit_name");
  const bool use_calendar = config.get_flag("summary_time_use_calendar");

  const double scalevol  = pow(10.0, static_cast<double>(log10scalevol)),
               scalearea = pow(10.0, static_cast<double>(log10scalearea));
  char  volscalestr[10] = "     ", areascalestr[10] = "   "; // blank when 10^0 = 1 scaling
  if (log10scalevol != 0) {
    snprintf(volscalestr, sizeof(volscalestr), "10^%1d_", log10scalevol);
  }
  if (log10scalearea != 0) {
    snprintf(areascalestr, sizeof(areascalestr), "10^%1d_", log10scalearea);
  }

  if (printPrototype == true) {
    verbPrintf(2,grid.com,
               "P         time:       ivol      iarea  max_diffusivity  max_hor_vel\n");
    verbPrintf(2,grid.com,
               "U         %s   %skm^3  %skm^2         m^2 s^-1       m/%s\n",
               tunitstr.c_str(),volscalestr,areascalestr,tunitstr.c_str());
    return;
  }

  // this version keeps track of what has been done so as to minimize stdout:
  // FIXME: turn these static variables into class members.
  static std::string stdout_flags_count0;
  static int         mass_cont_sub_counter = 0;
  static double      mass_cont_sub_dtsum   = 0.0;
  if (mass_cont_sub_counter == 0) {
    stdout_flags_count0 = stdout_flags;
  }
  if (delta_t > 0.0) {
    mass_cont_sub_counter++;
    mass_cont_sub_dtsum += delta_t;
  }

  if ((tempAndAge == true) || (!do_energy) || (getVerbosityLevel() > 2)) {
    char tempstr[90]    = "",
         velunitstr[90] = "";

    const double major_dt = grid.time->convert_time_interval(mass_cont_sub_dtsum, tunitstr);
    if (mass_cont_sub_counter <= 1) {
      snprintf(tempstr,90, " (dt=%.5f)", major_dt);
    } else {
      snprintf(tempstr,90, " (dt=%.5f in %d substeps; av dt_sub_mass_cont=%.5f)",
               major_dt, mass_cont_sub_counter, major_dt / mass_cont_sub_counter);
    }
    stdout_flags_count0 += tempstr;

    if (delta_t > 0.0) { // avoids printing an empty line if we have not done anything
      stdout_flags_count0 += "\n";
      verbPrintf(2,grid.com, stdout_flags_count0.c_str());
    }

    if (use_calendar) {
      snprintf(tempstr,90, "%s", grid.time->date().c_str());
    } else {
      snprintf(tempstr,90, "%.3f", grid.time->convert_time_interval(grid.time->current(), tunitstr));
    }

    snprintf(velunitstr,90, "m/%s", tunitstr.c_str());
    const double maxvel = grid.convert(gmaxu > gmaxv ? gmaxu : gmaxv, "m/s", velunitstr);

    verbPrintf(2,grid.com,
               "S %s:   %8.5f  %9.5f     %12.5f %12.5f\n",
               tempstr,
               volume/(scalevol*1.0e9), area/(scalearea*1.0e6),
               max_diffusivity, maxvel);

    mass_cont_sub_counter = 0;
    mass_cont_sub_dtsum = 0.0;
  }
}


//! Computes the ice volume, in m^3.
double IceModel::compute_ice_volume() {
  IceModelVec::AccessList list;
  list.add(cell_area);

  double volume = 0.0;

  {
    list.add(ice_thickness);
    for (Points p(grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      // count all ice, including cells which have so little they
      // are considered "ice-free"
      if (ice_thickness(i,j) > 0.0) {
        volume += ice_thickness(i,j) * cell_area(i,j);
      }
    }
  }

  // Add the volume of the ice in Href:
  if (config.get_flag("part_grid")) {
    list.add(vHref);
    for (Points p(grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      volume += vHref(i,j) * cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, volume);
}

//! Computes the ice volume, which is relevant for sea-level rise in m^3 in SEA-WATER EQUIVALENT.
double IceModel::compute_sealevel_volume() {
  double volume = 0.0;
  MaskQuery mask(vMask);
  double ocean_rho = config.get("sea_water_density");
  double ice_rho = config.get("ice_density");

  assert (ocean != NULL);
  double sea_level = ocean->sea_level_elevation();

  assert(beddef != NULL);
  const IceModelVec2S &bed_topography = beddef->bed_elevation();

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(ice_thickness);
  list.add(bed_topography);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.grounded(i,j)) {
      // count all ice, including cells which have so little they
      // are considered "ice-free"
      if (ice_thickness(i,j) > 0) {
        if (bed_topography(i, j) > sea_level) {
          volume += ice_thickness(i,j) * cell_area(i,j) * ice_rho/ocean_rho ;
        } else {
          volume += ice_thickness(i,j) * cell_area(i,j) * ice_rho/ocean_rho - cell_area(i,j) * (sea_level - bed_topography(i, j));
        }
      }
    }
  }
  const double ocean_area = 3.61e14; //in square meters
  volume /= ocean_area;

  return GlobalSum(grid.com, volume);
}

//! Computes the temperate ice volume, in m^3.
double  IceModel::compute_ice_volume_temperate() {
  double volume = 0.0;

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(Enth3);
  list.add(cell_area);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // count all ice, including cells which have so little they are
    // considered "ice-free"
    if (ice_thickness(i,j) > 0) {
      const int ks = grid.kBelowHeight(ice_thickness(i,j));
      const double *Enth = Enth3.get_column(i,j);
      const double A = cell_area(i, j);

      for (int k = 0; k < ks; ++k) {
        if (EC->isTemperate(Enth[k],EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
          volume += (grid.z(k + 1) - grid.z(k)) * A;
        }
      }

      if (EC->isTemperate(Enth[ks],EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
        volume += (ice_thickness(i,j) - grid.z(ks)) * A;
      }
    }
  }

  return GlobalSum(grid.com, volume);
}

//! Computes the cold ice volume, in m^3.
double IceModel::compute_ice_volume_cold() {
  double volume = 0.0;

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(Enth3);
  list.add(cell_area);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // count all ice, including cells which have so little they
    // are considered "ice-free"
    if (ice_thickness(i,j) > 0) {
      const int ks = grid.kBelowHeight(ice_thickness(i,j));
      const double *Enth = Enth3.get_column(i,j);
      const double A = cell_area(i, j);

      for (int k=0; k<ks; ++k) {
        if (not EC->isTemperate(Enth[k],EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
          volume += (grid.z(k+1) - grid.z(k)) * A;
        }
      }

      if (not EC->isTemperate(Enth[ks],EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
        volume += (ice_thickness(i,j) - grid.z(ks)) * A;
      }
    }
  }

  return GlobalSum(grid.com, volume);
}

//! Computes ice area, in m^2.
double IceModel::compute_ice_area() {
  double area = 0.0;

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(ice_thickness);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.icy(i, j)) {
      area += cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, area);
}

//! Computes area of basal ice which is temperate, in m^2.
double IceModel::compute_ice_area_temperate() {
  double area = 0.0;
  IceModelVec2S &Enthbase = vWork2d[0];

  Enth3.getHorSlice(Enthbase, 0.0);  // z=0 slice

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(Enthbase);
  list.add(ice_thickness);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.icy(i, j) and
        EC->isTemperate(Enthbase(i,j), EC->getPressureFromDepth(ice_thickness(i,j)))) { // FIXME issue #15
      area += cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, area);
}

//! Computes area of basal ice which is cold, in m^2.
double IceModel::compute_ice_area_cold() {
  double area = 0.0;
  IceModelVec2S &Enthbase = vWork2d[0];

  Enth3.getHorSlice(Enthbase, 0.0);  // z=0 slice

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(Enthbase);
  list.add(ice_thickness);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.icy(i, j) and
        EC->isTemperate(Enthbase(i,j), EC->getPressureFromDepth(ice_thickness(i,j))) == false) { // FIXME issue #15
      area += cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, area);
}

//! Computes grounded ice area, in m^2.
double IceModel::compute_ice_area_grounded() {
  double area = 0.0;

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.grounded_ice(i,j)) {
      area += cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, area);
}

//! Computes floating ice area, in m^2.
double IceModel::compute_ice_area_floating() {
  double area = 0.0;

  MaskQuery mask(vMask);

  IceModelVec::AccessList list;
  list.add(vMask);
  list.add(cell_area);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.floating_ice(i,j)) {
      area += cell_area(i,j);
    }
  }

  return GlobalSum(grid.com, area);
}


//! Computes the total ice enthalpy in J.
/*!
  Units of the specific enthalpy field \f$E=\f$(IceModelVec3::Enth3) are J kg-1.  We integrate
  \f$E(t,x,y,z)\f$ over the entire ice fluid region \f$\Omega(t)\f$, multiplying
  by the density to get units of energy:
  \f[ E_{\text{total}}(t) = \int_{\Omega(t)} E(t,x,y,z) \rho_i \,dx\,dy\,dz. \f]
*/
double IceModel::compute_ice_enthalpy() {
  double enthalpy_sum = 0.0;

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(Enth3);
  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // count all ice, including cells which have so little they
    // are considered "ice-free"
    if (ice_thickness(i,j) > 0) {
      const int ks = grid.kBelowHeight(ice_thickness(i,j));
      const double *Enth = Enth3.get_column(i,j);

      for (int k=0; k<ks; ++k) {
        enthalpy_sum += Enth[k] * (grid.z(k+1) - grid.z(k));
      }
      enthalpy_sum += Enth[ks] * (ice_thickness(i,j) - grid.z(ks));
    }
  }

  // FIXME: use cell_area.
  enthalpy_sum *= config.get("ice_density") * (grid.dx() * grid.dy());

  return GlobalSum(grid.com, enthalpy_sum);
}

} // end of namespace pism
