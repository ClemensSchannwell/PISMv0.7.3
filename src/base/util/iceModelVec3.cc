// Copyright (C) 2008--2014 Ed Bueler and Constantine Khroulev
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

#include <gsl/gsl_math.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <petscdmda.h>

#include "PIO.hh"
#include "iceModelVec.hh"
#include "IceGrid.hh"
#include "PISMConfig.hh"

#include "error_handling.hh"

#include <cassert>

namespace pism {

// this file contains method for derived class IceModelVec3

// methods for base class IceModelVec and derived class IceModelVec2S
// are in "iceModelVec.cc"

IceModelVec3D::IceModelVec3D() : IceModelVec() {
}

IceModelVec3D::~IceModelVec3D() {
  destroy();
}

//! Allocate a DA and a Vec from information in IceGrid.
void  IceModelVec3D::allocate(IceGrid &my_grid, const std::string &my_name,
                                        IceModelVecKind ghostedp, const std::vector<double> &levels,
                                        unsigned int stencil_width) {

  assert(m_v == NULL);
  
  grid = &my_grid;

  zlevels = levels;
  m_n_levels = (unsigned int)zlevels.size();
  m_da_stencil_width = stencil_width;

  m_da = grid->get_dm(this->m_n_levels, this->m_da_stencil_width);

  m_has_ghosts = (ghostedp == WITH_GHOSTS);

  if (m_has_ghosts == true) {
    DMCreateLocalVector(*m_da, &m_v);
  } else {
    DMCreateGlobalVector(*m_da, &m_v);
  }

  m_name = my_name;

  m_metadata.push_back(NCSpatialVariable(grid->get_unit_system(),
                                         my_name, *grid, zlevels));
}

bool IceModelVec3D::isLegalLevel(double z) const {
  double z_min = zlevels.front(),
    z_max = zlevels.back();
  if (z < z_min - 1.0e-6 || z > z_max + 1.0e-6) {
    return false;
  }
  return true;
}


//! Set values of an ice scalar quantity in a column by linear *interpolation*.
/*!
  Input array `source` and `must` contain `grid.Mz_fine` scalars
  (`double`).  Upon completion, internal storage will hold values derived from 
  linearly interpolating the input values.
 */
void  IceModelVec3::setValColumnPL(int i, int j, std::vector<double> &source) {
#if (PISM_DEBUG==1)
  assert(m_v != NULL);
  assert(source.size() == grid->Mz_fine);
  check_array_indices(i, j, 0);
#endif

  std::vector<double> &zlevels_fine = grid->zlevels_fine;

  double ***arr = (double***) array;
  
  for (unsigned int k=0; k < m_n_levels-1; ++k) {
    int m = grid->ice_fine2storage[k];

    const double increment = (zlevels[k] - zlevels_fine[m])
                                  / (zlevels_fine[m+1] - zlevels_fine[m]);
    arr[i][j][k] = source[m] +  increment * (source[m+1] - source[m]);
  }
  
  arr[i][j][m_n_levels-1] = source[grid->ice_fine2storage[m_n_levels-1]];
}


//! Set all values of scalar quantity to given a single value in a particular column.
void IceModelVec3D::setColumn(int i, int j, double c) {
  PetscErrorCode ierr;
#if (PISM_DEBUG==1)
  assert(array != NULL);
  check_array_indices(i, j, 0);
#endif

  double ***arr = (double***) array;

  if (c == 0.0) {
    ierr = PetscMemzero(arr[i][j], m_n_levels * sizeof(double));
    PISM_PETSC_CHK(ierr, "PetscMemzero");
  } else {
    for (unsigned int k=0; k < m_n_levels; k++) {
      arr[i][j][k] = c;
    }
  }
}


//! Return value of scalar quantity at level z (m) above base of ice (by linear interpolation).
double IceModelVec3D::getValZ(int i, int j, double z) const {
#if (PISM_DEBUG==1)
  assert(array != NULL);
  check_array_indices(i, j, 0);

  if (not isLegalLevel(z)) {
    throw RuntimeError::formatted("IceModelVec3 getValZ(): level %f is not legal; name = %s",
                                  z, m_name.c_str());
  }
#endif

  double ***arr = (double***) array;
  if (z >= zlevels.back()) {
    return arr[i][j][m_n_levels - 1];
  } else if (z <= zlevels.front()) {
    return arr[i][j][0];
  }

  int mcurr = 0;
  while (zlevels[mcurr+1] < z) {
    mcurr++;
  }

  const double incr = (z - zlevels[mcurr]) / (zlevels[mcurr+1] - zlevels[mcurr]);
  const double valm = arr[i][j][mcurr];
  return valm + incr * (arr[i][j][mcurr+1] - valm);
}


//! Return values on planar star stencil of scalar quantity at level z (by linear interpolation).
void   IceModelVec3::getPlaneStarZ(int i, int j, double z,
                                             planeStar<double> *star) const {
#if (PISM_DEBUG==1)
  assert(array != NULL);
  assert(m_has_ghosts == true);
  assert(isLegalLevel(z));
  check_array_indices(i, j, 0);
#endif

  unsigned int kbz = 0;
  double incr = 0.0;
  if (z >= zlevels.back()) {
    kbz = m_n_levels - 1;
    incr = 0.0;
  } else if (z <= zlevels.front()) {
    kbz = 0;
    incr = 0.0;
  } else {
    kbz = 0;
    while (zlevels[kbz+1] < z) {
      kbz++;
    }

    incr = (z - zlevels[kbz]) / (zlevels[kbz+1] - zlevels[kbz]);
  }

  double ***arr = (double***) array;

  if (kbz < m_n_levels - 1) {
    star->ij  = arr[i][j][kbz]   + incr * (arr[i][j][kbz + 1]   - arr[i][j][kbz]);
    star->e = arr[i+1][j][kbz] + incr * (arr[i+1][j][kbz + 1] - arr[i+1][j][kbz]);
    star->w = arr[i-1][j][kbz] + incr * (arr[i-1][j][kbz + 1] - arr[i-1][j][kbz]);
    star->n = arr[i][j+1][kbz] + incr * (arr[i][j+1][kbz + 1] - arr[i][j+1][kbz]);
    star->s = arr[i][j-1][kbz] + incr * (arr[i][j-1][kbz + 1] - arr[i][j-1][kbz]);
  } else {
    star->ij  = arr[i][j][kbz];
    star->e = arr[i+1][j][kbz];
    star->w = arr[i-1][j][kbz];
    star->n = arr[i][j+1][kbz];
    star->s = arr[i][j-1][kbz];
  }
}

//! Gets a map-plane star stencil directly from the storage grid.
void IceModelVec3::getPlaneStar(int i, int j, unsigned int k,
                                          planeStar<double> *star) const {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif

  double ***arr = (double***) array;

  star->ij  = arr[i][j][k];
  star->e = arr[i+1][j][k];
  star->w = arr[i-1][j][k];
  star->n = arr[i][j+1][k];
  star->s = arr[i][j-1][k];
}

//! Gets a map-plane star stencil on the fine vertical grid.
void IceModelVec3::getPlaneStar_fine(int i, int j, unsigned int k,
                                               planeStar<double> *star) const {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif

  unsigned int kbz = grid->ice_storage2fine[k];

  if (kbz < m_n_levels - 1) {
    double z = grid->zlevels_fine[k],
      incr = (z - zlevels[kbz]) / (zlevels[kbz+1] - zlevels[kbz]);
    double ***arr = (double***) array;

    star->ij  = arr[i][j][kbz]   + incr * (arr[i][j][kbz + 1]   - arr[i][j][kbz]);
    star->e = arr[i+1][j][kbz] + incr * (arr[i+1][j][kbz + 1] - arr[i+1][j][kbz]);
    star->w = arr[i-1][j][kbz] + incr * (arr[i-1][j][kbz + 1] - arr[i-1][j][kbz]);
    star->n = arr[i][j+1][kbz] + incr * (arr[i][j+1][kbz + 1] - arr[i][j+1][kbz]);
    star->s = arr[i][j-1][kbz] + incr * (arr[i][j-1][kbz + 1] - arr[i][j-1][kbz]);
  } else {
    return getPlaneStar(i, j, kbz, star);
  }
}

//! \brief Return values of ice scalar quantity at given levels (m)
//! above base of ice, using piecewise linear interpolation.
/*!
 * ks is the top-most fine vertical grid level within the ice
 */
void IceModelVec3::getValColumnPL(int i, int j, unsigned int ks,
                                            double *result) const {
#if (PISM_DEBUG==1)
  assert(m_v != NULL);
  check_array_indices(i, j, 0);
#endif

  std::vector<double> &zlevels_fine = grid->zlevels_fine;
  double ***arr = (double***) array;

  for (unsigned int k = 0; k < grid->Mz_fine; k++) {
    if (k > ks) {
      result[k] = arr[i][j][grid->ice_storage2fine[k]];
      continue;
    }

    unsigned int m = grid->ice_storage2fine[k];

    // extrapolate (if necessary):
    if (m == m_n_levels - 1) {
      result[k] = arr[i][j][m_n_levels-1];
      continue;
    }

    const double incr = (zlevels_fine[k] - zlevels[m]) / (zlevels[m+1] - zlevels[m]);
    const double valm = arr[i][j][m];
    result[k] = valm + incr * (arr[i][j][m+1] - valm);
  }
}

//! \brief Return values of ice scalar quantity on the fine
//! computational grid, using local quadratic interpolation.
void  IceModelVec3::getValColumnQUAD(int i, int j, unsigned int ks,
                                               double *result) const {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif

  // Assume that the fine grid is equally-spaced:
  const double dz_fine = grid->zlevels_fine[1] - grid->zlevels_fine[0];
  const double *column = static_cast<const double***>(array)[i][j];

  unsigned int k = 0, m = 0;
  for (m = 0; m < m_n_levels - 2; m++) {
    if (k > ks) {
      break;
    }

    const double
      z0 = zlevels[m],
      z1 = zlevels[m+1],
      z2 = zlevels[m+2],
      f0 = column[m],
      f1 = column[m+1],
      f2 = column[m+2];

    const double
      d1 = (f1 - f0) / (z1 - z0),
      d2 = (f2 - f0) / (z2 - z0),
      b  = (d2 - d1) / (z2 - z1),
      a  = d1 - b * (z1 - z0),
      c  = f0;

    double z_fine = k * dz_fine;
    while (z_fine < z1) {
      if (k > ks) {
        break;
      }

      const double s = z_fine - z0;

      result[k] = s * (a + b * s) + c;

      k++;
      z_fine = k * dz_fine;
    }
  } // m-loop

  // check if we got to the end of the m-loop and use linear
  // interpolation between the remaining 2 coarse levels
  if (m == m_n_levels - 2) {
    const double
      z0 = zlevels[m],
      z1 = zlevels[m+1],
      f0 = column[m],
      f1 = column[m+1],
      lambda = (f1 - f0) / (z1 - z0);

    double z_fine = k * dz_fine;
    while (z_fine < z1) {
      result[k] = f0 + lambda * (z_fine - z0);

      k++;
      z_fine = k * dz_fine;
    }
  }

  // fill the rest using constant extrapolation
  const double f0 = column[m_n_levels - 1];
  while (k <= ks) {
    result[k] = f0;
    k++;
  }
}


//! If the grid is equally spaced in the ice then use PL, otherwise use QUAD.
void  IceModelVec3::getValColumn(int i, int j, unsigned int ks,
                                           double *result) const {
  if (grid->ice_vertical_spacing == EQUAL) {
    return getValColumnPL(i, j, ks, result);
  } else {
    return getValColumnQUAD(i, j, ks, result);
  }
}


//! Copies a horizontal slice at level z of an IceModelVec3 into a Vec gslice.
/*!
 * FIXME: this method is misnamed: the slice is horizontal in the PISM
 * coordinate system, not in reality.
 */
void  IceModelVec3::getHorSlice(Vec &gslice, double z) const {
  double    **slice_val;

  PISMDM::Ptr da2 = grid->get_dm(1, grid->config.get("grid_max_stencil_width"));

  IceModelVec::AccessList list(*this);
  DMDAVecGetArray(*da2, gslice, &slice_val);
  for (Points p(*grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    slice_val[i][j] = getValZ(i,j,z);
  }
  DMDAVecRestoreArray(*da2, gslice, &slice_val);
}

//! Copies a horizontal slice at level z of an IceModelVec3 into an IceModelVec2S gslice.
/*!
 * FIXME: this method is misnamed: the slice is horizontal in the PISM
 * coordinate system, not in reality.
 */
void  IceModelVec3::getHorSlice(IceModelVec2S &gslice, double z) const {
  IceModelVec::AccessList list(*this);
  list.add(gslice);

  for (Points p(*grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    gslice(i, j) = getValZ(i, j, z);
  }
}


//! Copies the values of an IceModelVec3 at the ice surface (specified by the level myH) to an IceModelVec2S gsurf.
void  IceModelVec3::getSurfaceValues(IceModelVec2S &surface_values,
                                               const IceModelVec2S &H) const {
  IceModelVec::AccessList list(*this);
  list.add(surface_values);
  list.add(H);

  for (Points p(*grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    surface_values(i, j) = getValZ(i, j, H(i, j));
  }
}


void  IceModelVec3D::getInternalColumn(int i, int j, double **valsPTR) {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif
  double ***arr = (double***) array;
  *valsPTR = arr[i][j];
}

void  IceModelVec3D::getInternalColumn(int i, int j, const double **valsPTR) const {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif
  double ***arr = (double***) array;
  *valsPTR = arr[i][j];
}


void  IceModelVec3D::setInternalColumn(int i, int j, double *valsIN) {
#if (PISM_DEBUG==1)
  check_array_indices(i, j, 0);
#endif
  double ***arr = (double***) array;
  PetscErrorCode ierr = PetscMemcpy(arr[i][j], valsIN, m_n_levels*sizeof(double));
  PISM_PETSC_CHK(ierr, "PetscMemcpy");
}


void  IceModelVec3::create(IceGrid &my_grid, const std::string &my_name, IceModelVecKind ghostedp,
                                     unsigned int stencil_width) {

  IceModelVec3D::allocate(my_grid, my_name, ghostedp,
                          my_grid.z(), stencil_width);
}

} // end of namespace pism
