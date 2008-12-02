// Copyright (C) 2008 Ed Bueler and Constantine Khroulev
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

#include <cstring>
#include <cstdlib>
#include <petscda.h>
#include <netcdf.h>
#include "pism_const.hh"
#include "nc_util.hh"
#include "iceModelVec.hh"


IceModelVec::IceModelVec() {

  v = PETSC_NULL;
  da = PETSC_NULL;
  grid = PETSC_NULL;
  array = PETSC_NULL;
  localp = true;
  IOwnDA = true;
  use_interpolation_mask = false;
  
  strcpy(short_name,"*****UNKNOWN***** variable name");

  strcpy(long_name,"unknown long name");
  strcpy(units,"unknown units");
  strcpy(pism_intent,"unknown pism_intent");

  has_standard_name = PETSC_FALSE;
  strcpy(standard_name,"unknown NetCDF CF 1.0 standard_name");

  conversion_factor = 1.0;
  strcpy(glaciological_units, "unknown glaciological units");

#ifdef PISM_DEBUG
  creation_counter = 0;
  access_counter = 0;
#endif // PISM_DEBUG
}


IceModelVec::~IceModelVec() {
}


PetscErrorCode  IceModelVec::create(IceGrid &mygrid, const char my_short_name[], bool local) {
  SETERRQ(1,"VIRTUAL ONLY: not implemented");
  return 0;
}


PetscErrorCode  IceModelVec::destroy() {
  PetscErrorCode ierr;
  if (v != PETSC_NULL) {
    ierr = VecDestroy(v); CHKERRQ(ierr);
    v = PETSC_NULL;
  }
  if ((IOwnDA) && (da != PETSC_NULL)) {
    ierr = DADestroy(da); CHKERRQ(ierr);
    da = PETSC_NULL;
  }
#ifdef PISM_DEBUG
  creation_counter -= 1;
  PetscPrintf(grid->com, "%20s:\tcreate: %d\taccess: %d\n", short_name, creation_counter, access_counter);
#endif // PISM_DEBUG
  return 0;
}


PetscErrorCode  IceModelVec::printInfo(const PetscInt verbosity) {
  PetscErrorCode ierr;
  
  if (grid == PETSC_NULL) {
    SETERRQ1(1,"ERROR: cannot print info for IceModelVec with short_name='%s'\n"
               "   because grid=PETSC_NULL.  ENDING.\n\n",short_name);
  }

  ierr = verbPrintf(verbosity,grid->com,
         "\nprinting info for IceModelVec with short_name='%s':\n",
         short_name); CHKERRQ(ierr);
  if (da == PETSC_NULL) {
    ierr = verbPrintf(verbosity,grid->com,
          "  WARNING:  da == PETSC_NULL for IceModelVec with short_name='%s'!\n",
          short_name); CHKERRQ(ierr);
  }
  if (v == PETSC_NULL) {
    ierr = verbPrintf(verbosity,grid->com,
          "  WARNING:  v == PETSC_NULL for IceModelVec with short_name='%s'!\n",
          short_name); CHKERRQ(ierr);
  }
  if (array == PETSC_NULL) {
    ierr = verbPrintf(verbosity,grid->com,
          "  WARNING:  array == PETSC_NULL for IceModelVec with short_name='%s'!\n",
          short_name); CHKERRQ(ierr);
  }
  
  ierr = verbPrintf(verbosity,grid->com,
           "  boolean flags:  localp = %d,  IOwnDA = %d,  has_standard_name = %d\n",
           (int)localp, (int)IOwnDA, has_standard_name);  CHKERRQ(ierr);

  ierr = verbPrintf(verbosity,grid->com,
           "                  long_name = '%s'\n", long_name);  CHKERRQ(ierr);
  ierr = verbPrintf(verbosity,grid->com,
           "                  standard_name = '%s'\n", standard_name);  CHKERRQ(ierr);
  ierr = verbPrintf(verbosity,grid->com,
           "                  units = '%s'\n", units);  CHKERRQ(ierr);
  ierr = verbPrintf(verbosity,grid->com,
           "                  pism_intent = '%s'\n\n", pism_intent);  CHKERRQ(ierr);
  return 0;
}

//! Result: min <- min(v[j]), max <- max(v[j]).
/*! Note that VecMin and VecMax are collective on Vec.
 */
PetscErrorCode IceModelVec::range(PetscReal &min, PetscReal &max) {
  PetscReal my_min, my_max;
  PetscErrorCode ierr;

  ierr = VecMin(v, PETSC_NULL, &my_min); CHKERRQ(ierr);
  ierr = VecMax(v, PETSC_NULL, &my_max); CHKERRQ(ierr);

  min = my_min; max = my_max;

  return 0;
}

//! Computes the norm of an IceModelVec by calling VecNorm.
PetscErrorCode IceModelVec::norm(NormType n, PetscReal &out) {
  PetscErrorCode ierr;
  PetscReal tmp;
  ierr = VecNorm(v, n, &tmp); CHKERRQ(ierr);
  out = tmp;
  return 0;
}

//! Result: v <- sqrt(v). Calls VecSqrt(v).
PetscErrorCode IceModelVec::sqrt() {
  PetscErrorCode ierr;
  ierr = VecSqrt(v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v + alpha * x. Calls VecAXPY.
PetscErrorCode IceModelVec::add(PetscScalar alpha, IceModelVec &x) {
  PetscErrorCode ierr;
  ierr = VecAXPY(v, alpha, x.v); CHKERRQ(ierr);
  return 0;
}

//! Result: result <- v + alpha * x. Calls VecWAXPY.
PetscErrorCode IceModelVec::add(PetscScalar alpha, IceModelVec &x, IceModelVec &result) {
  PetscErrorCode ierr;
  ierr = VecWAXPY(result.v, alpha, x.v, v); CHKERRQ(ierr);
  return 0;
}

//! Result: v[j] <- v[j] + alpha for all j. Calls VecShift.
PetscErrorCode IceModelVec::shift(PetscScalar alpha) {
  PetscErrorCode ierr;
  ierr = VecShift(v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v * alpha. Calls VecScale.
PetscErrorCode IceModelVec::scale(PetscScalar alpha) {
  PetscErrorCode ierr;
  ierr = VecScale(v, alpha); CHKERRQ(ierr);
  return 0;
}

//! Result: result <- v .* x. Calls VecPointwiseMult.
PetscErrorCode  IceModelVec::multiply_by(IceModelVec &x, IceModelVec &result) {
  PetscErrorCode ierr;
  ierr = VecPointwiseMult(result.v, v, x.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- v .* x. Calls VecPointwiseMult.
PetscErrorCode  IceModelVec::multiply_by(IceModelVec &x) {
  PetscErrorCode ierr;
  ierr = VecPointwiseMult(v, v, x.v); CHKERRQ(ierr);
  return 0;
}

//! Copies v to a global vector 'destination'. Ghost points are discarded.
/*! This is potentially dangerous: make sure that \c destination has the same
    dimensions as the current IceModelVec.
 */
PetscErrorCode  IceModelVec::copy_to_global(Vec destination) {
  PetscErrorCode ierr;

  if (!localp)
    SETERRQ2(1, "Use copy_to(...). (Called %s.copy_to_global(...) and %s is global)", short_name, short_name);

  ierr = DALocalToGlobal(da, v, INSERT_VALUES, destination); CHKERRQ(ierr);
  return 0;
}

//! Result: destination <- v. Uses VecCopy.
PetscErrorCode  IceModelVec::copy_to(IceModelVec &destination) {
  PetscErrorCode ierr;
  PetscInt X_size, Y_size;

  ierr = VecGetSize(v, &X_size); CHKERRQ(ierr);
  ierr = VecGetSize(destination.v, &Y_size); CHKERRQ(ierr);

  if (X_size != Y_size)
    SETERRQ1(1, "IceModelVec::copy_to(...): incompatible Vec sizes (called as %s.copy_to(...))\n", short_name);

  ierr = VecCopy(v, destination.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v <- source. Uses VecCopy.
PetscErrorCode  IceModelVec::copy_from(IceModelVec &source) {
  PetscErrorCode ierr;
  PetscInt X_size, Y_size;

  ierr = VecGetSize(source.v, &X_size); CHKERRQ(ierr);
  ierr = VecGetSize(v, &Y_size); CHKERRQ(ierr);

  if (X_size != Y_size)
    SETERRQ1(1, "IceModelVec::copy_from(...): incompatible Vec sizes (called as %s.copy_from(...))\n", short_name);

  ierr = VecCopy(source.v, v); CHKERRQ(ierr);
  return 0;
}

//! Puts a local IceModelVec on processor 0.
/*!
 <ul>
 <li> onp0 and ctx should be created by calling VecScatterCreateToZero or be identical to one,
 <li> g2 is a preallocated temporary global vector,
 <li> g2natural is a preallocated temporary global vector with natural ordering.
 </ul>
*/
PetscErrorCode IceModelVec::put_on_proc0(Vec onp0, VecScatter ctx, Vec g2, Vec g2natural) {
  PetscErrorCode ierr;
  if (!localp)
    SETERRQ1(1, "Can't put a global IceModelVec '%s' on proc 0.", short_name);

  ierr =        DALocalToGlobal(da, v,  INSERT_VALUES, g2);        CHKERRQ(ierr);
  ierr = DAGlobalToNaturalBegin(da, g2, INSERT_VALUES, g2natural); CHKERRQ(ierr);
  ierr =   DAGlobalToNaturalEnd(da, g2, INSERT_VALUES, g2natural); CHKERRQ(ierr);

  ierr = VecScatterBegin(ctx, g2natural, onp0, INSERT_VALUES, SCATTER_FORWARD); CHKERRQ(ierr);
  ierr =   VecScatterEnd(ctx, g2natural, onp0, INSERT_VALUES, SCATTER_FORWARD); CHKERRQ(ierr);

  return 0;
}

//! Gets a local IceModelVec from processor 0.
/*!
 <ul>
 <li> onp0 and ctx should be created by calling VecScatterCreateToZero or be identical to one,
 <li> g2 is a preallocated temporary global vector,
 <li> g2natural is a preallocated temporary global vector with natural ordering.
 </ul>
*/
PetscErrorCode IceModelVec::get_from_proc0(Vec onp0, VecScatter ctx, Vec g2, Vec g2natural) {
  PetscErrorCode ierr;
  if (!localp)
    SETERRQ1(1, "Can't get a global IceModelVec '%s' from proc 0.", short_name);

  ierr = VecScatterBegin(ctx, onp0, g2natural, INSERT_VALUES, SCATTER_REVERSE); CHKERRQ(ierr);
  ierr =   VecScatterEnd(ctx, onp0, g2natural, INSERT_VALUES, SCATTER_REVERSE); CHKERRQ(ierr);

  ierr = DANaturalToGlobalBegin(da, g2natural, INSERT_VALUES, g2); CHKERRQ(ierr);
  ierr =   DANaturalToGlobalEnd(da, g2natural, INSERT_VALUES, g2); CHKERRQ(ierr);
  ierr =   DAGlobalToLocalBegin(da, g2,        INSERT_VALUES, v);  CHKERRQ(ierr);
  ierr =     DAGlobalToLocalEnd(da, g2,        INSERT_VALUES, v);  CHKERRQ(ierr);

  return 0;
}

//! Sets the variable name to \c name.
PetscErrorCode  IceModelVec::set_name(const char name[]) {
  strcpy(short_name, name);
  return 0;
}

//! Sets the glaciological units and the conversion factor of an IceModelVec.
/*! This affects IceModelVec::report_range() and IceModelVec::write().
 */
PetscErrorCode  IceModelVec::set_glaciological_units(const char units[], PetscReal factor) {
  strcpy(glaciological_units, units);
  conversion_factor = factor;
  return 0;
}

//! Sets NetCDF attributes of an IceModelVec object.
/*! Call set_attrs("new long name", "new units", "new pism_intent", NULL) if a
  variable does not have a standard name. Similarly, by putting NULL in an
  appropriate spot, it is possible tp leave long_name, units or pism_intent
  unmodified.
 */
PetscErrorCode  IceModelVec::set_attrs(const char my_pism_intent[],
				       const char my_long_name[], const char my_units[],
				       const char my_standard_name[]) {
  if (my_long_name != NULL)
    strcpy(long_name,my_long_name);

  if (my_units != NULL)
    strcpy(units,my_units);

  if (my_pism_intent != NULL)
    strcpy(pism_intent,my_pism_intent);

  if (my_standard_name != NULL) {
    strcpy(standard_name,my_standard_name);
    has_standard_name = PETSC_TRUE;
  }
  return 0;
}


//! Defines a netcdf variable corresponding to an IceModelVec object. Virtual only.
PetscErrorCode IceModelVec::define_netcdf_variable(int ncid, nc_type nctype, int *varidp) {
  SETERRQ(1, "define_netcdf_variable: virtual only");
}

//! Writes NetCDF attributes to a dataset.
/*! Call this <b>after</b> making sure that the NetCDF variable is defined.
 */
PetscErrorCode IceModelVec::write_attrs(const int ncid) {
  bool exists, use_glaciological_units = false;
  int varid, ierr;
  NCTool nc(grid);
  nc.ncid = ncid;

  // Use glaciological units if \c conversion_factor is not 1.0.
  use_glaciological_units = PetscAbsReal(1.0 - conversion_factor) > 1e-6;

  ierr = nc.find_variable(short_name, standard_name, &varid, exists); CHKERRQ(ierr);
  if (!exists)
    SETERRQ(1, "Can't write attributes of an undefined variable.");

  if (grid->rank == 0) {
    ierr = nc_redef(ncid); CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    ierr = nc_put_att_text(ncid, varid,"pism_intent", strlen(pism_intent), pism_intent);
    CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    ierr = nc_put_att_text(ncid, varid,"units",
			   use_glaciological_units ? strlen(glaciological_units) : strlen(units),
			   use_glaciological_units ? glaciological_units : units);
    CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    ierr = nc_put_att_text(ncid, varid,"long_name", strlen(long_name), long_name);
    CHKERRQ(check_err(ierr,__LINE__,__FILE__));

    if (has_standard_name) {
      ierr = nc_put_att_text(ncid, varid,"standard_name", strlen(standard_name), standard_name);
      CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    }

    ierr = nc_enddef(ncid); CHKERRQ(check_err(ierr,__LINE__,__FILE__));
  }

  return 0;
}

//! Virtual only. Reimplemented in derived classes.
PetscErrorCode IceModelVec::read(const char filename[], const unsigned int time) {           
  SETERRQ(1, "IceModelVec::read(...) is virtual only.");
  return 0;
}

//! Virtual only. Reimplemented in derived classes.
PetscErrorCode IceModelVec::regrid(const char filename[], LocalInterpCtx &lic, bool critical) {
  SETERRQ(1, "IceModelVec::regrid(...) is virtual only");
  return 0;
}

//! Virtual only. Reimplemented in derived classes.
PetscErrorCode IceModelVec::regrid(const char filename[], LocalInterpCtx &lic, PetscScalar default_value) {
  SETERRQ(1, "IceModelVec::regrid(...) is virtual only");
  return 0;
}

//! Calls the appropriate NCTool method to read a NetCDF variable into the IceModelVec.
/*!
  <ol>
  <li> Opens the file by calling NCTool::open_for_reading(...)
  <li> Finds the variable by calling NCTool::find_variable(...)
  <li> Reads data by calling NCTool::get_global_var(...) or NCTool::get_local_var(...)
  </ol>
 */
PetscErrorCode IceModelVec::read_from_netcdf(const char filename[], const unsigned int time,
					     int dims, const int Mz) {           
  PetscErrorCode ierr;
  bool file_exists, variable_exists;
  void *a_mpi;
  int a_len, max_a_len, varid;
  NCTool nc(grid);
  int s[] = {time, grid->xs, grid->ys, 0}; // Start local block: t dependent; 
  int c[] = {1, grid->xm, grid->ym, Mz}; // Count local block: t dependent

  // Memory allocation:
  max_a_len = a_len = grid->xm * grid->ym * Mz;
  ierr = MPI_Reduce(&a_len, &max_a_len, 1, MPI_INT, MPI_MAX, 0, grid->com); CHKERRQ(ierr);
  ierr = PetscMalloc(max_a_len * sizeof(double), &a_mpi); CHKERRQ(ierr);

  // Open the file:
  ierr = nc.open_for_reading(filename, file_exists); CHKERRQ(ierr);
  if (!file_exists)
    SETERRQ2(1, "Could not open file '%s' while trying to read '%s'.", filename, short_name);
  
  // Find the variable:
  ierr = nc.find_variable(short_name, standard_name, &varid, variable_exists); CHKERRQ(ierr);
  if (!variable_exists)
    SETERRQ2(1, "Can't find variable '%s' in '%s'.", short_name, filename);

  if (localp) {
    Vec g;
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);
    ierr = nc.get_local_var(varid, da, v, g,
			     s, c, dims, a_mpi, max_a_len); CHKERRQ(ierr);  
    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = nc.get_global_var(varid, da, v,
			      s, c, dims, a_mpi, max_a_len); CHKERRQ(ierr);  
  }

  ierr = nc.close(); CHKERRQ(ierr);

  ierr = PetscFree(a_mpi);
  return 0;
}

//! Virtual only. Reimplemented in derived classes.
PetscErrorCode IceModelVec::write(const char filename[], nc_type nctype) {
  SETERRQ(1, "IceModelVec::write(const char filename[], nc_type nctype) is virtual only.")
  return 0;
}

//! Writes an IceModelVec to a NetCDF file.
/*!
  1) Get the last time.
  2) Find the variable in the file. Call define_variable if not found.
  3) Call put_global_var or put_local_var.
 */
PetscErrorCode IceModelVec::write_to_netcdf(const char filename[], int dims, nc_type nctype,
					    const int Mz) {
  PetscErrorCode ierr;
  bool exists;
  NCTool nc(grid);
  int t, t_id, varid, max_a_len, a_len;
  void *a_mpi;

  ierr = nc.open_for_writing(filename, false); CHKERRQ(ierr); // replace = false, because
				// we want to *append* at this point

  // get the last time (index, not the value):
  if (grid->rank == 0) {
    size_t t_len;
    ierr = nc_inq_dimid(nc.ncid, "t", &t_id); CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    ierr = nc_inq_dimlen(nc.ncid, t_id, &t_len); CHKERRQ(check_err(ierr,__LINE__,__FILE__));
    t = (int)t_len;
  }
  ierr = MPI_Bcast(&t, 1, MPI_INT, 0, grid->com); CHKERRQ(ierr);

  int s[] = {t - 1, grid->xs, grid->ys, 0}; // Start local block: t dependent; 
  int c[] = {1, grid->xm, grid->ym, Mz}; // Count local block: t dependent

  // find or define the variable
  ierr = nc.find_variable(short_name, standard_name, &varid, exists); CHKERRQ(ierr);
  if (!exists) {
    ierr = define_netcdf_variable(nc.ncid, nctype, &varid); CHKERRQ(ierr);
  }

  if (PetscAbsReal(1.0 - conversion_factor) > 1e-6) {
    ierr = scale(conversion_factor); CHKERRQ(ierr); // change the units
  }

  // write the attributes
  write_attrs(nc.ncid);

  // Memory allocation:
  max_a_len = a_len = grid->xm * grid->ym * Mz;
  ierr = MPI_Reduce(&a_len, &max_a_len, 1, MPI_INT, MPI_MAX, 0, grid->com); CHKERRQ(ierr);
  ierr = PetscMalloc(max_a_len * sizeof(double), &a_mpi); CHKERRQ(ierr);

  // Actually write data:
  if (localp) {
    Vec g;
    ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);
    ierr = nc.put_local_var(varid, da, v, g,
                         s, c, dims, a_mpi, max_a_len); CHKERRQ(ierr);  
    ierr = VecDestroy(g); CHKERRQ(ierr);
  } else {
    ierr = nc.put_global_var(varid, da, v,
                          s, c, dims, a_mpi, max_a_len); CHKERRQ(ierr);  
  }

  
  if (PetscAbsReal(1.0 - conversion_factor) > 1e-6) {
    ierr = scale(1.0/conversion_factor); CHKERRQ(ierr); // restore the units
  }

  ierr = nc.close(); CHKERRQ(ierr);
  ierr = PetscFree(a_mpi); CHKERRQ(ierr);
  return 0;
}

//! Reports the range of an IceModelVec, with the appropriate units.
/*! Uses glaciological_units if set.
 */
PetscErrorCode IceModelVec::report_range() {

  PetscErrorCode ierr;
  PetscReal min, max;
  bool use_glaciological_units;

  use_glaciological_units = PetscAbsReal(1.0 - conversion_factor) > 1e-6;

  ierr = range(min, max);
  min *= conversion_factor;
  max *= conversion_factor;

  ierr = verbPrintf(2, grid->com, 
		    "%-10s|%-60s| min,max = %9.3f,%9.3f (%s)\n",
		    short_name, long_name, min, max,
		    use_glaciological_units ? glaciological_units : units); CHKERRQ(ierr);

  return 0;
}

//! Reads data from a NetCDF file, with regridding.
/*!
  <ol>
  <li> Open the file
  <li> Find the variable
  <li> If \c critical == true, break if it was not found.
  <li> If \c critical == false, regrid the variable or set the default value if asked to.
  </ol>
 */
PetscErrorCode IceModelVec::regrid_from_netcdf(const char filename[], const int dim_flag,
					       LocalInterpCtx &lic, bool critical,
					       bool set_default_value,
					       PetscScalar default_value) {
  int varid;
  bool exists;
  PetscErrorCode ierr;
  NCTool nc(grid);

  // Open the file
  ierr = nc.open_for_reading(filename, exists); CHKERRQ(ierr);
  if (!exists)
    SETERRQ1(1, "Regridding file '%s' does not exist.", filename);

  // Find the variable
  ierr = nc.find_variable(short_name, standard_name, &varid, exists); CHKERRQ(ierr);

  if (!exists) {		// couldn't find the variable
    if (critical)		// if it's critical, raise an error
      SETERRQ1(1, "Variable '%s' was not found.\n", short_name);
    if (set_default_value) {	// if it's not and we have a default value, set it
      ierr = verbPrintf(2, grid->com, 
			" ***  %-10s|%-60s| not found; using default constant %7.2f (%s)\n",
			short_name, long_name, default_value, units);
      CHKERRQ(ierr);
      ierr = set(default_value); CHKERRQ(ierr);
    } else {			// otherwise leave it alone
      ierr = verbPrintf(2, grid->com, 
			" ***  %-10s|%-60s| not found; continuing without setting it\n",
			short_name, long_name);
      CHKERRQ(ierr);
    }
  } else {			// the variable was found successfully
    ierr = verbPrintf(2, grid->com, "Found ");
    // Check if it is discrete
    if (use_interpolation_mask)
      nc.set_MaskInterp(&interpolation_mask);

    if (localp) {
      Vec g;
      ierr = DACreateGlobalVector(da, &g); CHKERRQ(ierr);
      ierr = nc.regrid_local_var(varid, dim_flag, lic, da, v, g,
				  use_interpolation_mask); CHKERRQ(ierr);
      ierr = VecDestroy(g); CHKERRQ(ierr);
    } else {
      ierr = nc.regrid_global_var(varid, dim_flag, lic, da, v,
				   use_interpolation_mask); CHKERRQ(ierr);
    }
    ierr = report_range(); CHKERRQ(ierr);
  }

  ierr = nc.close(); CHKERRQ(ierr);
  return 0;
}

//! Checks if an IceModelVec is allocated.
PetscErrorCode  IceModelVec::checkAllocated() {
  if (v == PETSC_NULL) {
    SETERRQ1(1,"IceModelVec ERROR: IceModelVec with short_name='%s' NOT allocated\n",
             short_name);
  }
  return 0;
}

//! Checks if the access to the array is available.
PetscErrorCode  IceModelVec::checkHaveArray() {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  if (array == PETSC_NULL) {
    SETERRQ1(1,"array for IceModelVec with short_name='%s' not available\n"
               "  (REMEMBER TO RUN begin_access() before access and end_access() after access)\n",
               short_name);
  }
  return 0;
}


/*
PetscErrorCode  IceModelVec::checkSelfOwnsIt(const PetscInt i, const PetscInt j) {
  if (allocated == PETSC_FALSE) {
    SETERRQ3(1,"IceModelVec ERROR: (i,j)=(%d,%d) not in ownership range of processor %d\n",
             i,j,grid->rank);
  }
  return 0;
}
*/

//! Checks if an IceModelVec is allocated and calls DAVecGetArray.
PetscErrorCode  IceModelVec::begin_access() {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DAVecGetArray(da, v, &array); CHKERRQ(ierr);
#ifdef PISM_DEBUG
  access_counter += 1;
#endif // PISM_DEBUG
  return 0;
}

//! Checks if an IceModelVec is allocated and calls DAVecRestoreArray.
PetscErrorCode  IceModelVec::end_access() {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(da, v, &array); CHKERRQ(ierr);
  array = PETSC_NULL;
#ifdef PISM_DEBUG
  access_counter -= 1;
#endif // PISM_DEBUG
  return 0;
}

//! Starts the communication of ghost points.
PetscErrorCode  IceModelVec::beginGhostComm() {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has short_name='%s')\n",
               short_name);
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalBegin(da, v, INSERT_VALUES, v);  CHKERRQ(ierr);
  return 0;
}

//! Ends the communication of ghost points.
PetscErrorCode  IceModelVec::endGhostComm() {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has short_name='%s')\n",
               short_name);
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(da, v, INSERT_VALUES, v); CHKERRQ(ierr);
  return 0;
}

//! Starts the communication of ghost points to IceModelVec destination.
PetscErrorCode  IceModelVec::beginGhostComm(IceModelVec &destination) {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has short_name='%s')\n",
               short_name);
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalBegin(da, v, INSERT_VALUES, destination.v);  CHKERRQ(ierr);
  return 0;
}

//! Ends the communication of ghost points to IceModelVec destination.
PetscErrorCode  IceModelVec::endGhostComm(IceModelVec &destination) {
  PetscErrorCode ierr;
  if (!localp) {
    SETERRQ1(1,"makes no sense to communicate ghosts for GLOBAL IceModelVec! (has short_name='%s')\n",
               short_name);
  }
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(da, v, INSERT_VALUES, destination.v); CHKERRQ(ierr);
  return 0;
}

//! Result: v[j] <- c for all j.
PetscErrorCode  IceModelVec::set(const PetscScalar c) {
  PetscErrorCode ierr;
  ierr = checkAllocated(); CHKERRQ(ierr);
  ierr = VecSet(v,c); CHKERRQ(ierr);
  return 0;
}


/********* IceModelVec3 and IceModelVec3Bedrock: SEE SEPARATE FILE  iceModelVec3.cc    **********/

/********* IceModelVec2 and IceModelVec2Box: SEE SEPARATE FILE  iceModelVec2.cc    **********/
