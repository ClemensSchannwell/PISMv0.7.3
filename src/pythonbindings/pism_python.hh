// Copyright (C) 2011 David Maxwell
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


#ifndef _PISM_PYTHON_
#define _PISM_PYTHON_
#include "petsc.h"

PetscErrorCode globalMax(PetscReal local_max, PetscReal *result, MPI_Comm comm);
PetscErrorCode globalMin(PetscReal local_min, PetscReal *result, MPI_Comm comm);
PetscErrorCode globalSum(PetscReal local_sum, PetscReal *result, MPI_Comm comm);

PetscErrorCode optionsGroupBegin(MPI_Comm comm,const char *prefix,const char *mess,const char *sec);
void optionsGroupNext();
bool optionsGroupContinue();
PetscErrorCode optionsGroupEnd();


// PetscErrorCode optionsBegin(MPI_Comm comm, const char *prefix, const char *title, const char *mansec);
// PetscErrorCode optionsEnd();


#endif
