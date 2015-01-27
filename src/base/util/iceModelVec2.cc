// Copyright (C) 2008--2015 Ed Bueler and Constantine Khroulev
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

#include <petscdraw.h>

#include <cstring>
#include <cstdlib>
#include <petscdmda.h>

#include <cassert>

#ifdef PISM_USE_TR1
#include <tr1/memory>
using std::tr1::dynamic_pointer_cast;
#else
#include <memory>
using std::dynamic_pointer_cast;
#endif

#include "PIO.hh"
#include "iceModelVec.hh"
#include "IceGrid.hh"
#include "PISMConfig.hh"

#include "error_handling.hh"
#include "iceModelVec_helpers.hh"

#include "Vec.hh"
#include "VecScatter.hh"

namespace pism {

// this file contains methods for derived classes IceModelVec2S and IceModelVec2Int

// methods for base class IceModelVec are in "iceModelVec.cc"

IceModelVec2::IceModelVec2()
  : IceModelVec() {
  // empty
}

IceModelVec2V::~IceModelVec2V() {
  // empty
}

IceModelVec2S::Ptr IceModelVec2S::To2DScalar(IceModelVec::Ptr input) {
  IceModelVec2S::Ptr result = dynamic_pointer_cast<IceModelVec2S,IceModelVec>(input);
  if (not (bool)result) {
    throw RuntimeError("dynamic cast failure");
  }
  return result;
}

IceModelVec2S::IceModelVec2S() {
  begin_end_access_use_dof = false;
}

IceModelVec2Stag::IceModelVec2Stag()
  : IceModelVec2() {
  m_dof = 2;
  begin_end_access_use_dof = true;
}

void  IceModelVec2S::create(const IceGrid &my_grid, const std::string &my_name, IceModelVecKind ghostedp, int width) {
  assert(m_v == NULL);
  IceModelVec2::create(my_grid, my_name, ghostedp, width, m_dof);
}


double** IceModelVec2S::get_array() {
  begin_access();
  return static_cast<double**>(array);
}

/*! Allocate a copy on processor zero and the scatter needed to move data.
 */
petsc::Vec::Ptr IceModelVec2S::allocate_proc0_copy() const {
  PetscErrorCode ierr;
  Vec v_proc0 = NULL;
  Vec result = NULL;

  ierr = PetscObjectQuery((PetscObject)m_da->get(), "v_proc0", (PetscObject*)&v_proc0);
  PISM_PETSC_CHK(ierr, "PetscObjectQuery")
                                                                                          ;
  if (v_proc0 == NULL) {

    // natural_work will be destroyed at the end of scope, but it will
    // only decrement the reference counter incremented by
    // PetscObjectCompose below.
    petsc::Vec natural_work;
    // create a work vector with natural ordering:
    ierr = DMDACreateNaturalVector(*m_da, natural_work.rawptr());
    PISM_PETSC_CHK(ierr, "DMDACreateNaturalVector");

    // this increments the reference counter of natural_work
    ierr = PetscObjectCompose((PetscObject)m_da->get(), "natural_work",
                              (PetscObject)((::Vec)natural_work));
    PISM_PETSC_CHK(ierr, "PetscObjectCompose");

    // scatter_to_zero will be destroyed at the end of scope, but it
    // will only decrement the reference counter incremented by
    // PetscObjectCompose below.
    petsc::VecScatter scatter_to_zero;

    // initialize the scatter to processor 0 and create storage on processor 0
    ierr = VecScatterCreateToZero(natural_work, scatter_to_zero.rawptr(),
                                  &v_proc0);
    PISM_PETSC_CHK(ierr, "VecScatterCreateToZero");

    // this increments the reference counter of scatter_to_zero
    ierr = PetscObjectCompose((PetscObject)m_da->get(), "scatter_to_zero",
                              (PetscObject)((::VecScatter)scatter_to_zero));
    PISM_PETSC_CHK(ierr, "PetscObjectCompose");

    // this increments the reference counter of v_proc0
    ierr = PetscObjectCompose((PetscObject)m_da->get(), "v_proc0",
                              (PetscObject)v_proc0);
    PISM_PETSC_CHK(ierr, "PetscObjectCompose");

    // We DO NOT call VecDestroy(v_proc0): the petsc::Vec wrapper will
    // take care of this.
    result = v_proc0;
  } else {
    // We DO NOT call VecDestroy(result): the petsc::Vec wrapper will
    // take care of this.
    ierr = VecDuplicate(v_proc0, &result);
    PISM_PETSC_CHK(ierr, "VecDuplicate");
  }
  return petsc::Vec::Ptr(new petsc::Vec(result));
}

//! Puts a local IceModelVec2S on processor 0.
void IceModelVec2S::put_on_proc0(Vec onp0) const {
  PetscErrorCode ierr;
  assert(m_v != NULL);

  VecScatter scatter_to_zero = NULL;
  Vec natural_work = NULL;

  ierr = PetscObjectQuery((PetscObject)m_da->get(), "scatter_to_zero",
                          (PetscObject*)&scatter_to_zero);
  PISM_PETSC_CHK(ierr, "PetscObjectQuery");

  ierr = PetscObjectQuery((PetscObject)m_da->get(), "natural_work",
                          (PetscObject*)&natural_work);
  PISM_PETSC_CHK(ierr, "PetscObjectQuery");

  if (natural_work == NULL || scatter_to_zero == NULL) {
    throw RuntimeError("call allocate_proc0_copy() before calling put_on_proc0");
  }

  Vec global = NULL;

  if (m_has_ghosts) {
    DMGetGlobalVector(*m_da, &global);
    this->copy_to_vec(m_da, global);
  } else {
    global = m_v;
  }

  DMDAGlobalToNaturalBegin(*m_da, global, INSERT_VALUES, natural_work);
  DMDAGlobalToNaturalEnd(*m_da, global, INSERT_VALUES, natural_work);

  if (m_has_ghosts) {
    DMRestoreGlobalVector(*m_da, &global);
  }

  ierr = VecScatterBegin(scatter_to_zero, natural_work, onp0,
                         INSERT_VALUES, SCATTER_FORWARD);
  PISM_PETSC_CHK(ierr, "VecScatterBegin");

  ierr = VecScatterEnd(scatter_to_zero, natural_work, onp0,
                       INSERT_VALUES, SCATTER_FORWARD);
  PISM_PETSC_CHK(ierr, "VecScatterEnd");
}

//! Gets a local IceModelVec2 from processor 0.
void IceModelVec2S::get_from_proc0(Vec onp0) {
  PetscErrorCode ierr;
  assert(m_v != NULL);

  VecScatter scatter_to_zero = NULL;
  Vec natural_work = NULL;
  ierr = PetscObjectQuery((PetscObject)m_da->get(), "scatter_to_zero",
                          (PetscObject*)&scatter_to_zero);
  PISM_PETSC_CHK(ierr, "PetscObjectQuery");
  ierr = PetscObjectQuery((PetscObject)m_da->get(), "natural_work",
                          (PetscObject*)&natural_work);
  PISM_PETSC_CHK(ierr, "PetscObjectQuery");

  if (natural_work == NULL || scatter_to_zero == NULL) {
    throw RuntimeError("call allocate_proc0_copy() before calling get_from_proc0");
  }

  ierr = VecScatterBegin(scatter_to_zero, onp0, natural_work,
                         INSERT_VALUES, SCATTER_REVERSE);
  PISM_PETSC_CHK(ierr, "VecScatterBegin");
  VecScatterEnd(scatter_to_zero, onp0, natural_work,
                INSERT_VALUES, SCATTER_REVERSE);

  Vec global = NULL;

  if (m_has_ghosts) {
    DMGetGlobalVector(*m_da, &global);
  } else {
    global = m_v;
  }

  DMDANaturalToGlobalBegin(*m_da, natural_work, INSERT_VALUES, global);
  DMDANaturalToGlobalEnd(*m_da, natural_work, INSERT_VALUES, global);

  if (m_has_ghosts) {
    this->copy_from_vec(global);
    DMRestoreGlobalVector(*m_da, &global);
  }

  inc_state_counter();          // mark as modified
}

//! Sets an IceModelVec2 to the magnitude of a 2D vector field with components `v_x` and `v_y`.
/*! Computes the magnitude \b pointwise, so any of v_x, v_y and the IceModelVec
  this is called on can be the same.

  Does not communicate.
 */
void IceModelVec2S::set_to_magnitude(const IceModelVec2S &v_x,
                                     const IceModelVec2S &v_y) {
  IceModelVec::AccessList list(*this);
  list.add(v_x);
  list.add(v_y);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    (*this)(i,j) = sqrt(PetscSqr(v_x(i,j)) + PetscSqr(v_y(i,j)));
  }

  inc_state_counter();          // mark as modified
  
}

void IceModelVec2S::set_to_magnitude(const IceModelVec2V &input) {
  IceModelVec::AccessList list;
  list.add(*this);
  list.add(input);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    (*this)(i, j) = input(i, j).magnitude();
  }
}

//! Masks out all the areas where \f$ M \le 0 \f$ by setting them to `fill`. 
void IceModelVec2S::mask_by(const IceModelVec2S &M, double fill) {
  IceModelVec::AccessList list(*this);
  list.add(M);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (M(i,j) <= 0.0) {
      (*this)(i,j) = fill;
    }
  }

  inc_state_counter();          // mark as modified
}

void IceModelVec2::write_impl(const PIO &nc, IO_Type nctype) const {
  PetscErrorCode ierr;

  assert(m_v != NULL);

  // The simplest case:
  if ((m_dof == 1) && (m_has_ghosts == false)) {
    IceModelVec::write_impl(nc, nctype);
    return;
  }

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  petsc::DM::Ptr da2 = m_grid->get_dm(1, 0);

  // a temporary one-component vector, distributed across processors
  // the same way v is
  petsc::TemporaryGlobalVec tmp(da2);

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(m_grid->com, "  Writing %s...\n", m_name.c_str());
    PISM_PETSC_CHK(ierr, "PetscPrintf");
  }

  for (unsigned int j = 0; j < m_dof; ++j) {
    IceModelVec2::get_dof(da2, tmp, j);

    petsc::VecArray tmp_array(tmp);
    m_metadata[j].write(nc, nctype, write_in_glaciological_units, tmp_array.get());
  }
}

void IceModelVec2::read_impl(const PIO &nc, const unsigned int time) {
  PetscErrorCode ierr;

  if ((m_dof == 1) && (m_has_ghosts == false)) {
    IceModelVec::read_impl(nc, time);
    return;
  }

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(m_grid->com, "  Reading %s...\n", m_name.c_str());
    PISM_PETSC_CHK(ierr, "PetscPrintf");
  }

  assert(m_v != NULL);

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  petsc::DM::Ptr da2 = m_grid->get_dm(1, 0);

  // a temporary one-component vector, distributed across processors
  // the same way v is
  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int j = 0; j < m_dof; ++j) {

    {
      petsc::VecArray tmp_array(tmp);
      m_metadata[j].read(nc, time, tmp_array.get());
    }

    IceModelVec2::set_dof(da2, tmp, j);
  }
  
  // The calls above only set the values owned by a processor, so we need to
  // communicate if m_has_ghosts == true:
  if (m_has_ghosts) {
    update_ghosts();
  }
}

void IceModelVec2::regrid_impl(const PIO &nc, RegriddingFlag flag,
                                         double default_value) {
  PetscErrorCode ierr;

  if ((m_dof == 1) && (m_has_ghosts == false)) {
    IceModelVec::regrid_impl(nc, flag, default_value);
    return;
  }

  if (getVerbosityLevel() > 3) {
    ierr = PetscPrintf(m_grid->com, "  Regridding %s...\n", m_name.c_str());
    PISM_PETSC_CHK(ierr, "PetscPrintf");
  }

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  petsc::DM::Ptr da2 = m_grid->get_dm(1, 0);

  // a temporary one-component vector, distributed across processors
  // the same way v is
  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int j = 0; j < m_dof; ++j) {
    {
      petsc::VecArray tmp_array(tmp);
      m_metadata[j].regrid(nc, flag, m_report_range, default_value, tmp_array.get());
    }

    IceModelVec2::set_dof(da2, tmp, j);
  }

  // The calls above only set the values owned by a processor, so we need to
  // communicate if m_has_ghosts == true:
  if (m_has_ghosts == true) {
    update_ghosts();
  }
}

//! \brief View a 2D field.
void IceModelVec2::view(int viewer_size) const {
  Viewer::Ptr viewers[2];

  if (m_dof > 2) {
    throw RuntimeError("dof > 2 is not supported");
  }

  for (unsigned int j = 0; j < std::min(m_dof, 2U); ++j) {
    std::string c_name = m_metadata[j].get_name(),
      long_name = m_metadata[j].get_string("long_name"),
      units = m_metadata[j].get_string("glaciological_units"),
      title = long_name + " (" + units + ")";

    if (not map_viewers[c_name]) {
      map_viewers[c_name].reset(new Viewer(m_grid->com, title, viewer_size,
                                           m_grid->Lx(), m_grid->Ly()));
    }

    viewers[j] = map_viewers[c_name];
  }

  view(viewers[0], viewers[1]);
}

//! \brief View a 2D vector field using existing PETSc viewers.
//! Allocates and de-allocates g2, the temporary global vector; performance
//! should not matter here.
void IceModelVec2::view(Viewer::Ptr v1, Viewer::Ptr v2) const {
  PetscErrorCode ierr;

  Viewer::Ptr viewers[2] = {v1, v2};

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  petsc::DM::Ptr da2 = m_grid->get_dm(1, 0);

  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int i = 0; i < std::min(m_dof, 2U); ++i) {
    std::string
      long_name = m_metadata[i].get_string("long_name"),
      units     = m_metadata[i].get_string("glaciological_units"),
      title     = long_name + " (" + units + ")";

    if (not (bool)viewers[i]) {
      continue;
    }

    PetscViewer v = *viewers[i].get();

    PetscDraw draw = NULL;
    ierr = PetscViewerDrawGetDraw(v, 0, &draw);
    PISM_PETSC_CHK(ierr, "PetscViewerDrawGetDraw");

    ierr = PetscDrawSetTitle(draw, title.c_str());
    PISM_PETSC_CHK(ierr, "PetscDrawSetTitle");

    IceModelVec2::get_dof(da2, tmp, i);

    convert_vec(tmp,
                m_metadata[i].get_units(),
                m_metadata[i].get_glaciological_units());

    ierr = VecView(tmp, v); PISM_PETSC_CHK(ierr, "VecView");
  }
}

//! \brief Returns the x-derivative at i,j approximated using centered finite
//! differences.
double IceModelVec2S::diff_x(int i, int j) const {
  return ((*this)(i + 1,j) - (*this)(i - 1,j)) / (2 * m_grid->dx());
}

//! \brief Returns the y-derivative at i,j approximated using centered finite
//! differences.
double IceModelVec2S::diff_y(int i, int j) const {
  return ((*this)(i,j + 1) - (*this)(i,j - 1)) / (2 * m_grid->dy());
}


//! \brief Returns the x-derivative at East staggered point i+1/2,j approximated 
//! using centered (obvious) finite differences.
double IceModelVec2S::diff_x_stagE(int i, int j) const {
  return ((*this)(i+1,j) - (*this)(i,j)) / (m_grid->dx());
}

//! \brief Returns the y-derivative at East staggered point i+1/2,j approximated 
//! using centered finite differences.
double IceModelVec2S::diff_y_stagE(int i, int j) const {
  return ((*this)(i+1,j+1) + (*this)(i,j+1)
           - (*this)(i+1,j-1) - (*this)(i,j-1)) / (4* m_grid->dy());
}

//! \brief Returns the x-derivative at North staggered point i,j+1/2 approximated 
//! using centered finite differences.
double IceModelVec2S::diff_x_stagN(int i, int j) const {
  return ((*this)(i+1,j+1) + (*this)(i+1,j)
           - (*this)(i-1,j+1) - (*this)(i-1,j)) / (4* m_grid->dx());
}

//! \brief Returns the y-derivative at North staggered point i,j+1/2 approximated 
//! using centered (obvious) finite differences.
double IceModelVec2S::diff_y_stagN(int i, int j) const {
  return ((*this)(i,j+1) - (*this)(i,j)) / (m_grid->dy());
}


//! \brief Returns the x-derivative at i,j approximated using centered finite
//! differences. Respects grid periodicity and uses one-sided FD at grid edges
//! if necessary.
double IceModelVec2S::diff_x_p(int i, int j) const {
  if (m_grid->periodicity() & X_PERIODIC) {
    return diff_x(i,j);
  }
  
  if (i == 0) {
    return ((*this)(i + 1,j) - (*this)(i,j)) / (m_grid->dx());
  } else if (i == (int)m_grid->Mx() - 1) {
    return ((*this)(i,j) - (*this)(i - 1,j)) / (m_grid->dx());
  } else {
    return diff_x(i,j);
 }
}

//! \brief Returns the y-derivative at i,j approximated using centered finite
//! differences. Respects grid periodicity and uses one-sided FD at grid edges
//! if necessary.
double IceModelVec2S::diff_y_p(int i, int j) const {
  if (m_grid->periodicity() & Y_PERIODIC) {
    return diff_y(i,j);
  }
  
  if (j == 0) {
    return ((*this)(i,j + 1) - (*this)(i,j)) / (m_grid->dy());
  } else if (j == (int)m_grid->My() - 1) {
    return ((*this)(i,j) - (*this)(i,j - 1)) / (m_grid->dy());
  } else {
    return diff_y(i,j);
  }
}

//! Sums up all the values in an IceModelVec2S object. Ignores ghosts.
/*! Avoids copying to a "global" vector.
 */
double IceModelVec2S::sum() const {
  double my_result = 0;

  IceModelVec::AccessList list(*this);
  
  // sum up the local part:
  for (Points p(*m_grid); p; p.next()) {
    my_result += (*this)(p.i(), p.j());
  }

  // find the global sum:
  return GlobalSum(m_grid->com, my_result);
}


//! Finds maximum over all the values in an IceModelVec2S object.  Ignores ghosts.
double IceModelVec2S::max() const {
  IceModelVec::AccessList list(*this);

  double my_result = (*this)(m_grid->xs(),m_grid->ys());
  for (Points p(*m_grid); p; p.next()) {
    my_result = std::max(my_result,(*this)(p.i(), p.j()));
  }

  return GlobalMax(m_grid->com, my_result);
}


//! Finds maximum over all the absolute values in an IceModelVec2S object.  Ignores ghosts.
double IceModelVec2S::absmax() const {

  IceModelVec::AccessList list(*this);
  double my_result = 0.0;
  for (Points p(*m_grid); p; p.next()) {
    my_result = std::max(my_result,fabs((*this)(p.i(), p.j())));
  }

  return GlobalMax(m_grid->com, my_result);
}


//! Finds minimum over all the values in an IceModelVec2S object.  Ignores ghosts.
double IceModelVec2S::min() const {
  IceModelVec::AccessList list(*this);

  double my_result = (*this)(m_grid->xs(),m_grid->ys());
  for (Points p(*m_grid); p; p.next()) {
    my_result = std::min(my_result,(*this)(p.i(), p.j()));
  }

  return GlobalMin(m_grid->com, my_result);
}


// IceModelVec2

void IceModelVec2::get_component(unsigned int n, IceModelVec2S &result) const {

  IceModelVec2::get_dof(result.get_dm(), result.m_v, n);
}

void IceModelVec2::set_component(unsigned int n, const IceModelVec2S &source) {

  IceModelVec2::set_dof(source.get_dm(), source.m_v, n);
}

void  IceModelVec2::create(const IceGrid &my_grid, const std::string & my_name,
                           IceModelVecKind ghostedp,
                           unsigned int stencil_width, int my_dof) {

  assert(m_v == NULL);

  m_dof  = my_dof;
  m_grid = &my_grid;

  if ((m_dof != 1) || (stencil_width > m_grid->config.get("grid_max_stencil_width"))) {
    m_da_stencil_width = stencil_width;
  } else {
    m_da_stencil_width = m_grid->config.get("grid_max_stencil_width");
  }

  // initialize the da member:
  m_da = m_grid->get_dm(this->m_dof, this->m_da_stencil_width);

  if (ghostedp) {
    DMCreateLocalVector(*m_da, m_v.rawptr());
  } else {
    DMCreateGlobalVector(*m_da, m_v.rawptr());
  }

  m_has_ghosts = (ghostedp == WITH_GHOSTS);
  m_name       = my_name;

  if (m_dof == 1) {
    m_metadata.push_back(NCSpatialVariable(m_grid->config.get_unit_system(),
                                           my_name, *m_grid));
  } else {

    for (unsigned int j = 0; j < m_dof; ++j) {
      char tmp[TEMPORARY_STRING_LENGTH];

      snprintf(tmp, TEMPORARY_STRING_LENGTH, "%s[%d]",
               m_name.c_str(), j);
      m_metadata.push_back(NCSpatialVariable(m_grid->config.get_unit_system(),
                                             tmp, *m_grid));
    }
  }
}

void IceModelVec2S::add(double alpha, const IceModelVec &x) {
  add_2d<IceModelVec2S>(this, alpha, &x, this);
}

void IceModelVec2S::add(double alpha, const IceModelVec &x, IceModelVec &result) const {
  add_2d<IceModelVec2S>(this, alpha, &x, &result);
}

void IceModelVec2S::copy_to(IceModelVec &destination) const {
  copy_2d<IceModelVec2S>(this, &destination);
}

// IceModelVec2Stag

IceModelVec2Stag::Ptr IceModelVec2Stag::ToStaggered(IceModelVec::Ptr input) {
  IceModelVec2Stag::Ptr result = dynamic_pointer_cast<IceModelVec2Stag,IceModelVec>(input);
  if (not (bool)result) {
    throw RuntimeError("dynamic cast failure");
  }
  return result;
}


void IceModelVec2Stag::create(const IceGrid &my_grid, const std::string &my_short_name, IceModelVecKind ghostedp,
                                        unsigned int stencil_width) {

  IceModelVec2::create(my_grid, my_short_name, ghostedp, stencil_width, m_dof);
}

//! Averages staggered grid values of a scalar field and puts them on a regular grid.
/*!
 * The current IceModelVec needs to have ghosts.
 */
void IceModelVec2Stag::staggered_to_regular(IceModelVec2S &result) const {
  IceModelVec::AccessList list(*this);
  list.add(result);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j) = 0.25 * ((*this)(i,j,0) + (*this)(i,j,1)
                          + (*this)(i,j-1,1) + (*this)(i-1,j,0));
  }
}

//! \brief Averages staggered grid values of a 2D vector field (u on the
//! i-offset, v on the j-offset) and puts them on a regular grid.
/*!
 * The current IceModelVec needs to have ghosts.
 */
void IceModelVec2Stag::staggered_to_regular(IceModelVec2V &result) const {
  IceModelVec::AccessList list(*this);
  list.add(result);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i,j).u = 0.5 * ((*this)(i-1,j,0) + (*this)(i,j,0));
    result(i,j).v = 0.5 * ((*this)(i,j-1,1) + (*this)(i,j,1));
  }
}


//! For each component, finds the maximum over all the absolute values.  Ignores ghosts.
/*!
Assumes z is allocated.
 */
std::vector<double> IceModelVec2Stag::absmaxcomponents() const {
  std::vector<double> z(2, 0.0);

  IceModelVec::AccessList list(*this);
  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    z[0] = std::max(z[0],fabs((*this)(i,j,0)));
    z[1] = std::max(z[1],fabs((*this)(i,j,1)));
  }

  z[0] = GlobalMax(m_grid->com, z[0]);
  z[1] = GlobalMax(m_grid->com, z[1]);

  return z;
}

} // end of namespace pism
