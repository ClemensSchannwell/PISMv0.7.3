// Copyright (C) 2011, 2012, 2013, 2014, 2015 PISM Authors
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

#ifndef _PISMCONSTANTYIELDSTRESS_H_
#define _PISMCONSTANTYIELDSTRESS_H_

#include "PISMYieldStress.hh"
#include "iceModelVec.hh"

namespace pism {

class IceGrid;

class ConstantYieldStress : public YieldStress
{
public:
  ConstantYieldStress(const IceGrid &g);
  virtual ~ConstantYieldStress();

  virtual void init();

  virtual void write_variables(const std::set<std::string> &vars, const PIO &nc);

  virtual void update(double my_t, double my_dt);

  virtual const IceModelVec2S& basal_material_yield_stress();
protected:
  virtual void add_vars_to_output_impl(const std::string &keyword, std::set<std::string> &result);
  virtual void define_variables_impl(const std::set<std::string> &vars, const PIO &nc,
                                     IO_Type nctype);
protected:
  IceModelVec2S m_tauc;
};

} // end of namespace pism

#endif /* _PISMCONSTANTYIELDSTRESS_H_ */
