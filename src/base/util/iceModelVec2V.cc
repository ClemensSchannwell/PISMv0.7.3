// Copyright (C) 2009--2012 Constantine Khroulev
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

#include "iceModelVec.hh"
#include "pism_const.hh"
#include "IceGrid.hh"
#include "iceModelVec_helpers.hh"

IceModelVec2V::IceModelVec2V() : IceModelVec2() {
  dof = 2;
  begin_end_access_use_dof = false;
  vars.resize(dof);

  reset_attrs(0);
  reset_attrs(1);
}

PetscErrorCode  IceModelVec2V::create(IceGrid &my_grid, string my_short_name, bool local,
				      int stencil_width) {

  PetscErrorCode ierr = IceModelVec2::create(my_grid, my_short_name, local,
					     stencil_width, dof); CHKERRQ(ierr);

  vars[0].init_2d("u" + my_short_name, my_grid);
  vars[1].init_2d("v" + my_short_name, my_grid);

  name = "vel" + my_short_name;

  return 0;
}

PetscErrorCode IceModelVec2V::get_array(PISMVector2** &a) {
  PetscErrorCode ierr;
  ierr = begin_access(); CHKERRQ(ierr);
  a = static_cast<PISMVector2**>(array);
  return 0;
}

PetscErrorCode IceModelVec2V::magnitude(IceModelVec2S &result) {
  PetscErrorCode ierr;
  PISMVector2** a;
  PetscScalar **mag;

  ierr = result.get_array(mag); CHKERRQ(ierr);
  ierr = get_array(a);

  for (PetscInt i=grid->xs; i<grid->xs+grid->xm; ++i) {
    for (PetscInt j=grid->ys; j<grid->ys+grid->ym; ++j) {
      mag[i][j] = a[i][j].magnitude();
    }
  }

  ierr = result.end_access(); CHKERRQ(ierr);
  ierr = end_access();
  return 0;
}

bool IceModelVec2V::is_valid(PetscScalar U, PetscScalar V) {
  return vars[0].is_valid(U) && vars[1].is_valid(V);
}

PetscErrorCode IceModelVec2V::set_name(string new_name, int /*component = 0*/) {
  string tmp = new_name;
  reset_attrs(0);
  reset_attrs(1);
  
  name = "vel" + tmp;

  vars[0].short_name = "u" + tmp;
  vars[1].short_name = "v" + tmp;

  return 0;
}

//! Sets the variable's various names without changing any other metadata
PetscErrorCode IceModelVec2V::rename(const string &short_name, const string &long_name, 
                               const string &standard_name, int /* component */ )
{
  if(!short_name.empty())
  {
    string tmp = short_name;
    name = "vel" + tmp;

    vars[0].short_name = "u" + tmp;
    vars[1].short_name = "v" + tmp;    
  }

  if (!long_name.empty()) {
    string xprefix = "X component of ";
    string yprefix = "Y component of ";
    vars[0].set_string("long_name", xprefix + long_name);
    vars[1].set_string("long_name", yprefix + long_name);
  }

  if (!standard_name.empty()) {
    vars[0].set_string("standard_name", standard_name);
    vars[1].set_string("standard_name", standard_name);
  }

  return 0;
}  

//! Sets the variable's various names without changing any other metadata
PetscErrorCode IceModelVec2V::rename(const string &short_name, const vector<string> &long_names, 
                               const string &standard_name)
{
  if(!short_name.empty())
  {
    string tmp = short_name;

    name = "vel" + tmp;

    vars[0].short_name = "u" + tmp;
    vars[1].short_name = "v" + tmp;
  }

  vars[0].set_string("long_name", long_names[0]);
  vars[1].set_string("long_name", long_names[1]);

  if (!standard_name.empty()) {
    vars[0].set_string("standard_name", standard_name);
    vars[1].set_string("standard_name", standard_name);
  }

  return 0;
}

PetscErrorCode IceModelVec2V::add(PetscScalar alpha, IceModelVec &x) {
  return add_2d<IceModelVec2V>(this, alpha, &x, this);
}

PetscErrorCode IceModelVec2V::add(PetscScalar alpha, IceModelVec &x, IceModelVec &result) {
  return add_2d<IceModelVec2V>(this, alpha, &x, &result);
}

PetscErrorCode IceModelVec2V::copy_to(IceModelVec &destination) {
  return copy_2d<IceModelVec2V>(this, &destination);
}

PetscErrorCode IceModelVec2V::copy_from(IceModelVec &source) {
  return copy_2d<IceModelVec2V>(&source, this);
}
