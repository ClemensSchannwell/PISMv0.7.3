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

#ifndef _PSGIVEN_H_
#define _PSGIVEN_H_

#include "PGivenClimate.hh"
#include "PISMSurface.hh"
#include "PSModifier.hh"
#include "PISMAtmosphere.hh"

namespace pism {
namespace surface {

class Given : public PGivenClimate<SurfaceModifier,SurfaceModel>
{
public:
  Given(const IceGrid &g);
  virtual ~Given();

  virtual void attach_atmosphere_model(atmosphere::AtmosphereModel *input);

  virtual void init();

  virtual void ice_surface_mass_flux_impl(IceModelVec2S &result);
  virtual void ice_surface_temperature(IceModelVec2S &result);
protected:
  virtual void update_impl(double my_t, double my_dt);
  IceModelVec2T *climatic_mass_balance, *ice_surface_temp;
};

} // end of namespace surface
} // end of namespace pism

#endif /* _PSGIVEN_H_ */
