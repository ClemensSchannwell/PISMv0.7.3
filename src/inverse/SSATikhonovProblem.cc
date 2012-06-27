// Copyright (C) 2012  David Maxwell
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


#include "SSATikhonovProblem.hh"
#include <assert.h>

SSATikhonovProblem::SSATikhonovProblem( SSAForwardProblem &ssaforward,
                 DesignVec &d0, StateVec &u_obs, PetscReal eta,
                 Functional<DesignVec> &designFunctional, Functional<StateVec> &stateFunctional ):
                  m_ssaforward(ssaforward), m_d0(d0), m_u_obs(u_obs), m_eta(eta),
                  m_designFunctional(designFunctional), m_stateFunctional(stateFunctional)
{
  PetscErrorCode ierr = this->construct();
  CHKERRCONTINUE(ierr);
  assert(ierr == 0);
}


PetscErrorCode SSATikhonovProblem::construct() {
  PetscErrorCode ierr;

  m_grid = m_d0.get_grid();

  m_tikhonov_atol = m_grid->config.get("tikhonov_atol");
  m_tikhonov_rtol = m_grid->config.get("tikhonov_rtol");

  PetscInt design_stencil_width = m_d0.get_stencil_width();
  PetscInt state_stencil_width = m_u_obs.get_stencil_width();
  ierr = m_d.create(*m_grid, "design variable", kHasGhosts, design_stencil_width); CHKERRQ(ierr);
  ierr = m_dGlobal.create(*m_grid, "design variable (global)", kNoGhosts, design_stencil_width); CHKERRQ(ierr);
  ierr = m_dGlobal.copy_from(m_d0); CHKERRQ(ierr);

  ierr = m_u_diff.create( *m_grid, "state residual", kHasGhosts, state_stencil_width); CHKERRQ(ierr);
  ierr = m_d_diff.create( *m_grid, "design residual", kHasGhosts, design_stencil_width); CHKERRQ(ierr);

  ierr = m_grad_state.create( *m_grid, "state gradient", kNoGhosts, design_stencil_width); CHKERRQ(ierr);
  ierr = m_grad_design.create( *m_grid, "design gradient", kNoGhosts, design_stencil_width); CHKERRQ(ierr);
  ierr = m_grad.create( *m_grid, "gradient", kNoGhosts, design_stencil_width); CHKERRQ(ierr);

  ierr = m_adjointRHS.create(*m_grid,"work vector", kNoGhosts, design_stencil_width); CHKERRQ(ierr);

  return 0;
}

SSATikhonovProblem::~SSATikhonovProblem() {}

PetscErrorCode SSATikhonovProblem::connect(TaoSolver tao) {
  PetscErrorCode ierr;
  ierr = TaoObjGradCallback<SSATikhonovProblem>::connect(tao,*this); CHKERRQ(ierr);
  ierr = TaoMonitorCallback<SSATikhonovProblem>::connect(tao,*this); CHKERRQ(ierr);
  ierr = TaoConvergenceCallback<SSATikhonovProblem>::connect(tao,*this); CHKERRQ(ierr);

  const char *type;
  ierr = TaoGetType(tao,&type); CHKERRQ(ierr);
  if( strcmp(type,"blmvm") == 0 ) {
    ierr = TaoGetVariableBoundsCallback<SSATikhonovProblem>::connect(tao,*this); CHKERRQ(ierr);    
  }

  PetscReal fatol = 1e-10, frtol = 1e-20;
  PetscReal gatol = PETSC_DEFAULT, grtol = PETSC_DEFAULT, gttol = PETSC_DEFAULT;
  ierr = TaoSetTolerances(tao, fatol, frtol, gatol, grtol, gttol); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode SSATikhonovProblem::monitorTao(TaoSolver tao) {
  PetscErrorCode ierr;
  
  PetscInt its;
  ierr =  TaoGetSolutionStatus(tao, &its, NULL, NULL, NULL, NULL, NULL ); CHKERRQ(ierr);
  
  int nListeners = m_listeners.size();
  for(int k=0; k<nListeners; k++) {
   ierr = m_listeners[k]->iteration(*this,m_eta,
                 its,m_val_design,m_val_state,
                 m_d, m_d_diff, m_grad_design,
                 m_ssaforward.solution(), m_u_diff, m_grad_state,
                 m_grad );
   CHKERRQ(ierr);
  }
  return 0;
}

PetscErrorCode SSATikhonovProblem::convergenceTest(TaoSolver tao) {
  PetscErrorCode ierr;
  PetscReal designNorm, stateNorm, sumNorm;
  PetscReal dWeight, sWeight;
  dWeight = 1/m_eta;
  sWeight = 1;
  
  ierr = m_grad_design.norm(NORM_2,designNorm); CHKERRQ(ierr);
  ierr = m_grad_state.norm(NORM_2,stateNorm); CHKERRQ(ierr);
  ierr = m_grad.norm(NORM_2,sumNorm); CHKERRQ(ierr);
  designNorm *= dWeight;    
  stateNorm  *= sWeight;
  
  if( sumNorm < m_tikhonov_atol && sumNorm < m_tikhonov_rtol*PetscMax(designNorm,stateNorm) ) {
    ierr = TaoSetTerminationReason(tao,TAO_CONVERGED_USER); CHKERRQ(ierr);
  } else {
    ierr = TaoDefaultConvergenceTest(tao,NULL); CHKERRQ(ierr);
  }
  return 0;
}

PetscErrorCode SSATikhonovProblem::getVariableBounds(TaoSolver /*tao*/, Vec lo, Vec hi) {
  PetscErrorCode ierr;
  
  PetscReal zeta_min, zeta_max, tauc_min, tauc_max;
  
  tauc_min = m_grid->config.get("inv_ssa_tauc_min");
  tauc_max = m_grid->config.get("inv_ssa_tauc_max");
  
  InvTaucParameterization &tauc_param = m_ssaforward.tauc_param();
  ierr = tauc_param.fromTauc(tauc_min,&zeta_min); CHKERRQ(ierr);
  ierr = tauc_param.fromTauc(tauc_max,&zeta_max); CHKERRQ(ierr);

  ierr = VecSet(lo,zeta_min); CHKERRQ(ierr);
  ierr = VecSet(hi,zeta_max); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode SSATikhonovProblem::evaluateObjectiveAndGradient(TaoSolver tao, Vec x, PetscReal *value, Vec gradient) {
  PetscErrorCode ierr;

  (void) tao;
  // Variable 'x' has no ghosts.  We need ghosts for computation with the design variable.
  ierr = m_d.copy_from(x); CHKERRQ(ierr);

  bool success;
  ierr = m_ssaforward.linearize_at(m_d, success); CHKERRQ(ierr);
  if(!success) {
    SETERRQ(m_grid->com,1,"Failure in SSATikhonovProblem forward solve.");
  }

  ierr = m_d_diff.copy_from(m_d); CHKERRQ(ierr);
  ierr = m_d_diff.add(-1,m_d0); CHKERRQ(ierr);
  ierr = m_designFunctional.gradientAt(m_d_diff,m_grad_design); CHKERRQ(ierr);

  ierr = m_u_diff.copy_from(m_ssaforward.solution()); CHKERRQ(ierr);
  ierr = m_u_diff.add(-1, m_u_obs); CHKERRQ(ierr);

  // The following computes the reduced gradient.
  ierr = m_stateFunctional.gradientAt(m_u_diff,m_adjointRHS); CHKERRQ(ierr);  
  ierr = m_ssaforward.apply_linearization_transpose(m_adjointRHS,m_grad_state); CHKERRQ(ierr);

  ierr = m_grad.copy_from(m_grad_design); CHKERRQ(ierr);
  ierr = m_grad.scale(1./m_eta); CHKERRQ(ierr);    
  ierr = m_grad.add(1,m_grad_state); CHKERRQ(ierr);

  ierr = m_grad.copy_to(gradient); CHKERRQ(ierr);      

  PetscReal valDesign, valState;
  ierr = m_designFunctional.valueAt(m_d_diff,&valDesign); CHKERRQ(ierr);
  ierr = m_stateFunctional.valueAt(m_u_diff,&valState); CHKERRQ(ierr);

  m_val_design = valDesign;
  m_val_state = valState;
  
  *value = valDesign / m_eta + valState;

  return 0;
}
