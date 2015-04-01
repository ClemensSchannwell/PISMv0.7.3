// Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015 PISM Authors
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

#ifndef __BedDef_hh
#define __BedDef_hh

#include "base/util/PISMComponent.hh"
#include "base/util/iceModelVec.hh"

namespace pism {

//! @brief Bed-related models: bed deformation (provide bed elevation
//! and uplift) and (soon) bed erosion.
namespace bed {

//! PISM bed deformation model (base class).
class BedDef : public Component_TS {
public:
  BedDef(const IceGrid &g);
  virtual ~BedDef();

  void init();

  const IceModelVec2S& bed_elevation() const;
  const IceModelVec2S& uplift() const;

  void set_elevation(const IceModelVec2S &input);
  void set_uplift(const IceModelVec2S &input);
  
protected:
  virtual void init_impl();
  virtual void write_variables_impl(const std::set<std::string> &vars, const PIO &nc);
  virtual void add_vars_to_output_impl(const std::string &keyword, std::set<std::string> &result);
  virtual void define_variables_impl(const std::set<std::string> &vars, const PIO &nc,
                                     IO_Type nctype);
  void compute_uplift(double dt_beddef);
protected:
  //! time of the last bed deformation update
  double m_t_beddef_last;

  //! current bed elevation
  IceModelVec2S m_topg;

  //! bed elevation at the beginning of a run
  IceModelVec2S m_topg_initial;

  //! bed elevation at the time of the last update
  IceModelVec2S m_topg_last;

  //! bed uplift rate
  IceModelVec2S m_uplift;

  //! pointer to the current ice thickness
  const IceModelVec2S *m_thk;
};

class PBNull : public BedDef {
public:
  PBNull(const IceGrid &g);
protected:
  virtual MaxTimestep max_timestep_impl(double t);
  virtual void init_impl();
  virtual void update_impl(double my_t, double my_dt);
};

//! Pointwide isostasy bed deformation model.
class PBPointwiseIsostasy : public BedDef {
public:
  PBPointwiseIsostasy(const IceGrid &g); 
  virtual ~PBPointwiseIsostasy();
protected:
  virtual MaxTimestep max_timestep_impl(double t);
  virtual void init_impl();
  virtual void update_impl(double my_t, double my_dt);
  IceModelVec2S m_thk_last;       //!< last ice thickness
};

} // end of namespace bed
} // end of namespace pism

#endif  // __BedDef_hh
