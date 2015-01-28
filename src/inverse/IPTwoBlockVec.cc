// Copyright (C) 2012, 2014, 2015  David Maxwell and Constantine Khroulev
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

#include <cassert>

#include "IPTwoBlockVec.hh"
#include "error_handling.hh"

namespace pism {

IPTwoBlockVec::IPTwoBlockVec(Vec a, Vec b) {
  PetscErrorCode ierr = this->construct(a, b);
  if (ierr != 0) {
    throw std::runtime_error("memory allocation failed");
  }
}

IPTwoBlockVec::~IPTwoBlockVec() {
  // empty
}

//! @note Uses `PetscErrorCode` *intentionally*.
PetscErrorCode IPTwoBlockVec::construct(Vec a, Vec b)  {
  PetscErrorCode ierr;
  
  MPI_Comm comm, comm_b;
  ierr = PetscObjectGetComm((PetscObject)a, &comm); CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject)b, &comm_b); CHKERRQ(ierr);
  assert(comm==comm_b);
  
  PetscInt lo_a, hi_a;
  ierr = VecGetOwnershipRange(a, &lo_a, &hi_a); CHKERRQ(ierr);
  ierr = VecGetSize(a, &m_na_global); CHKERRQ(ierr);
  m_na_local = hi_a - lo_a;

  PetscInt lo_b, hi_b;
  ierr = VecGetOwnershipRange(b, &lo_b, &hi_b); CHKERRQ(ierr);
  ierr = VecGetSize(b, &m_nb_global); CHKERRQ(ierr);
  m_nb_local = hi_b - lo_b;

  petsc::IS is_a, is_b;
  ierr = ISCreateStride(comm, m_na_local, lo_a, 1, is_a.rawptr()); CHKERRQ(ierr);  // a in a
  ierr = ISCreateStride(comm, m_na_local, lo_a+lo_b, 1, m_a_in_ab.rawptr()); CHKERRQ(ierr); // a in ab

  ierr = ISCreateStride(comm, m_nb_local, lo_b, 1, is_b.rawptr()); CHKERRQ(ierr); // b in b
  ierr = ISCreateStride(comm, m_nb_local, lo_a+lo_b+m_na_local, 1, m_b_in_ab.rawptr()); CHKERRQ(ierr);  // b in ab

  ierr = VecCreate(comm, m_ab.rawptr()); CHKERRQ(ierr);
  ierr = VecSetType(m_ab, "mpi"); CHKERRQ(ierr);
  ierr = VecSetSizes(m_ab, m_na_local+m_nb_local, m_na_global+m_nb_global); CHKERRQ(ierr);

  ierr = VecScatterCreate(m_ab, m_a_in_ab, a, is_a, m_scatter_a.rawptr()); CHKERRQ(ierr);
  ierr = VecScatterCreate(m_ab, m_b_in_ab, b, is_b, m_scatter_b.rawptr()); CHKERRQ(ierr);
  
  return 0;
}

IS IPTwoBlockVec::blockAIndexSet() {
  return m_a_in_ab;
}

IS IPTwoBlockVec::blockBIndexSet() {
  return m_b_in_ab;
}

void IPTwoBlockVec::scatter(Vec a, Vec b) {
  this->scatterToA(m_ab,a);
  this->scatterToB(m_ab,b);
}

void IPTwoBlockVec::scatterToA(Vec a) {
  this->scatterToA(m_ab,a);
}

void IPTwoBlockVec::scatterToB(Vec b) {
  this->scatterToB(m_ab,b);
}

void IPTwoBlockVec::scatter(Vec ab, Vec a, Vec b) {
  this->scatterToA(ab,a);
  this->scatterToB(ab,b);  
}

void IPTwoBlockVec::scatter_begin_end(VecScatter s, Vec a, Vec b, ScatterMode m) {
  PetscErrorCode ierr;
  ierr = VecScatterBegin(s, a, b, INSERT_VALUES, m);
  PISM_PETSC_CHK(ierr, "VecScatterBegin");

  ierr = VecScatterEnd(s, a, b, INSERT_VALUES, m);
  PISM_PETSC_CHK(ierr, "VecScatterEnd");
}

void IPTwoBlockVec::scatterToA(Vec ab, Vec a) {
  scatter_begin_end(m_scatter_a, ab, a, SCATTER_FORWARD);
}

void IPTwoBlockVec::scatterToB(Vec ab, Vec b) {
  scatter_begin_end(m_scatter_b, ab, b, SCATTER_FORWARD);
}

void IPTwoBlockVec::gather(Vec a, Vec b) {
  this->gatherFromA(a,m_ab);
  this->gatherFromB(b,m_ab);
}

void IPTwoBlockVec::gatherFromA(Vec a) {
  this->gatherFromA(a,m_ab);
}

void IPTwoBlockVec::gatherFromB(Vec b) {
  this->gatherFromA(b,m_ab);
}

void IPTwoBlockVec::gather(Vec a, Vec b, Vec ab) {
  this->gatherFromA(a,ab);
  this->gatherFromB(b,ab);
}

void IPTwoBlockVec::gatherFromA(Vec a, Vec ab) {
  scatter_begin_end(m_scatter_a, a, ab, SCATTER_REVERSE);
}

void IPTwoBlockVec::gatherFromB(Vec b, Vec ab) {
  scatter_begin_end(m_scatter_b, b, ab, SCATTER_REVERSE);
}

} // end of namespace pism
