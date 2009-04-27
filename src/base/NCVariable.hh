// Copyright (C) 2009 Constantine Khroulev
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

#ifndef __NCVariable_hh
#define __NCVariable_hh

#include <map>
#include <vector>
#include <string>
#include <petscda.h>
#include <netcdf.h>
#include "../udunits/udunits.h"
#include "nc_util.hh"

// use namespace std BUT remove trivial namespace browser from doxygen-erated HTML source browser
/// @cond NAMESPACE_BROWSER
using namespace std;
/// @endcond


//! \brief A class for handling variable metadata, reading, writing and converting
//! from input units and to output units.
/*! A NetCDF variable can have any number of attributes, but some of them are
  treated differently:

  \li units: specifies internal units. When read, a variable is converted to
  these units. When written, it is converted from these to glaciological_units
  if write_in_glaciological_units is true.

  \li glaciological_units: is never written to a file; replaces 'units' in the
  output if write_in_glaciological_units is true.

  \li valid_min, valid_max: specify the valid range of a variable. Are read
  from an input file \b only if not specified previously. If both are set, then
  valid_range is used in the output instead.

  Also:

  \li empty string attributes are ignored (they are not written to the output
  file and has("foo") returns false if "foo" is absent or equal to an empty
  string.
 */
struct NCVariable {
public:
  NCVariable();
  void init(string name, IceGrid &g, GridType d);
  PetscErrorCode set_units(string);
  PetscErrorCode set_glaciological_units(string);
  PetscErrorCode write(const char filename[], nc_type nctype,
		       bool write_in_glaciological_units, Vec v);
  PetscErrorCode read(const char filename[], unsigned int time, Vec v);
  PetscErrorCode regrid(const char filename[], LocalInterpCtx &lic,
			bool critical, bool set_default_value,
			PetscScalar default_value, MaskInterp *, Vec v);
  PetscErrorCode change_units(Vec v, utUnit *from, utUnit *to);
  PetscErrorCode reset();
  void set(string, double);
  double get(string);
  bool has(string);
			
  string short_name;

  // Attributes:
  /*! Typical attributes stored here:

    \li long_name
    \li standard_name
    \li pism_intent
    \li units
    \li glaciological_units
   */
  map<string, string> strings;
  map<string, vector<double> > doubles;

protected:
  PetscErrorCode write_attributes(int ncid, int varid, nc_type nctype,
				  bool write_in_glaciological_units);
  PetscErrorCode report_range(Vec v, bool found_by_standard_name);
  PetscErrorCode check_range(Vec v);
  PetscErrorCode read_valid_range(int ncid, int varid);
  PetscErrorCode define(int ncid, nc_type nctype, int &varid);
  IceGrid *grid;
  GridType dims;
  utUnit units,		      //!< internal (PISM) units
         glaciological_units; //!< \brief for diagnostic variables: units to
			      //!use when writing to a NetCDF file and for
			      //!standard out reports
};

#endif
