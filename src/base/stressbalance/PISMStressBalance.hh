// Copyright (C) 2010 Constantine Khroulev and Ed Bueler
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

#ifndef _PISMSTRESSBALANCE_H_
#define _PISMSTRESSBALANCE_H_

#include "PISMComponent.hh"
#include "iceModelVec.hh"
#include "ShallowStressBalance.hh"
#include "SSB_Modifier.hh"
#include "PISMDiagnostic.hh"

//! The class defining PISM's interface to the shallow stress balance code.
class PISMStressBalance : public PISMComponent_Diag
{
  friend class PSB_taud_mag;
public:
  PISMStressBalance(IceGrid &g, ShallowStressBalance *sb, SSB_Modifier *ssb_mod,
                    const NCConfigVariable &config);
  virtual ~PISMStressBalance();

  //! \brief Initialize the PISMStressBalance object.
  virtual PetscErrorCode init(PISMVars &vars);

  //! \brief Adds more variable names to result (to respect -o_size and
  //! -save_size).
  /*!
    Keyword can be one of "small", "medium" or "big".
   */
  virtual void add_vars_to_output(string keyword, set<string> &result);

  //! Writes requested fields to a file.
  virtual PetscErrorCode write_variables(set<string> vars, string filename);

  virtual PetscErrorCode write_model_state(string filename);

  //! \brief Set the vertically-averaged ice velocity boundary condition.
  /*!
   * Does not affect the SIA computation.
   */
  virtual PetscErrorCode set_boundary_conditions(IceModelVec2Mask &locations,
                                                 IceModelVec2V &velocities);

  virtual PetscErrorCode set_basal_melt_rate(IceModelVec2S *bmr);

  //! \brief Update all the fields if fast == false, only update diffusive flux
  //! and max. diffusivity otherwise.
  virtual PetscErrorCode update(bool fast);

  //! \brief Get the thickness-advective (SSA) 2D velocity.
  virtual PetscErrorCode get_advective_2d_velocity(IceModelVec2V* &result);

  //! \brief Get the diffusive (SIA) vertically-averaged flux on the staggered grid.
  virtual PetscErrorCode get_diffusive_flux(IceModelVec2Stag* &result);

  //! \brief Get the max diffusivity (for the adaptive time-stepping).
  virtual PetscErrorCode get_max_diffusivity(PetscReal &D);

  //! \brief Get the max advective velocity (for the adaptive time-stepping).
  virtual PetscErrorCode get_max_2d_velocity(PetscReal &u, PetscReal &v);

  // for the energy/age time step:

  //! \brief Get the 3D velocity (for the energy/age time-stepping).
  virtual PetscErrorCode get_3d_velocity(IceModelVec3* &u, IceModelVec3* &v, IceModelVec3* &w);
  //! \brief Get the max 3D velocity (for the adaptive time-stepping).
  virtual PetscErrorCode get_max_3d_velocity(PetscReal &u, PetscReal &v, PetscReal &w);
  //! \brief Get the basal frictional heating (for the energy time-stepping).
  virtual PetscErrorCode get_basal_frictional_heating(IceModelVec2S* &result);

  virtual PetscErrorCode get_volumetric_strain_heating(IceModelVec3* &result);

  //! \brief Produce a report string for the standard output.
  virtual PetscErrorCode stdout_report(string &result);

  //! \brief Extends the computational grid (vertically).
  virtual PetscErrorCode extend_the_grid(PetscInt old_Mz);

  virtual void get_diagnostics(map<string, PISMDiagnostic*> &/*dict*/);

protected:
  virtual PetscErrorCode allocate();
  virtual PetscErrorCode compute_vertical_velocity(
                IceModelVec3 *u, IceModelVec3 *v, IceModelVec2S *bmr,
                IceModelVec3 &result);

  PISMVars *variables;

  IceModelVec3 w;
  PetscReal w_max;
  IceModelVec2S *basal_melt_rate;

  ShallowStressBalance *stress_balance;
  SSB_Modifier *modifier;
};

#endif /* _PISMSTRESSBALANCE_H_ */

