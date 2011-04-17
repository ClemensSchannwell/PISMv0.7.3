#! /usr/bin/env python
#
# Copyright (C) 2011 Ed Bueler and Constantine Khroulev and David Maxwell
# 
# This file is part of PISM.
# 
# PISM is free software; you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
# 
# PISM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
# 
# You should have received a copy of the GNU General Public License
# along with PISM; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


import sys, petsc4py
petsc4py.init(sys.argv)
from petsc4py import PETSc

import PISM

H0 = 2000.; #//m
L=50.e3; #// 50km half-width
dhdx = 0.001; #// pure number, slope of surface & bed
tauc0 = 0.; #// No basal shear stress
B0 = 3.7e8; #// Pa s^{1/3}; hardness 
           #// given on p. 239 of Schoof; why so big?
glen_n = 3.
#this->glen_n = n;      


class test_plug(PISM.ssa.SSAExactTestCase):
  def initGrid(self,Mx,My):
    PISM.util.init_shallow_grid(self.grid,L,L,Mx,My,PISM.NONE)

  def initPhysics(self):
    config = self.config
    
    #// The following is irrelevant because tauc=0
    linear_q = 1.;
    self.basal = PISM.IceBasalResistancePlasticLaw(
          config.get("plastic_regularization") / PISM.secpera,
          True, #// do not force a pure-plastic law
          linear_q,
          config.get("pseudo_plastic_uthreshold") / PISM.secpera);

    #// Use constant hardness
    self.ice = PISM.CustomGlenIce(self.grid.com, "", config);
    self.ice.setHardness(B0);
    self.ice.setExponent(glen_n);

    #// Enthalpy converter is irrelevant for this test.
    self.enthalpyconverter = PISM.EnthalpyConverter(config);

  def initSSACoefficients(self):
    solver = self.solver
    solver.allocateBCs()

    #// The finite difference code uses the following flag to treat the non-periodic grid correctly.
    # self.config.set_flag("compute_surf_grad_inward_ssa", True);
    # self.config.set("epsilon_ssafd", 0.0);

    # Ensure we never use the strength extension.
    solver.ssa.strength_extension.set_min_thickness(H0/2);

    # Set constant coefficients.
    solver.thickness.set(H0)
    solver.tauc.set(tauc0)
    solver.ice_mask.set(PISM.MASK_GROUNDED)
  
    bc_mask = solver.bc_mask
    vel_bc  = solver.vel_bc
    bed     = solver.bed
    surface = solver.surface
    
    vars = [bc_mask, vel_bc, bed, surface]
    grid = self.grid
    with PISM.util.Access(vars):
      for (i,j) in grid.points():
        x=grid.x[i]; y=grid.y[j]
        
        bed[i,j] = -x*(dhdx);
        surface[i,j] = bed[i,j] + H0;
      
        edge = ( (j == 0) or (j == grid.My - 1) ) or ( (i==0) or (i==grid.Mx-1) );
        if edge:
          bc_mask[i,j] = 1;
          [u,v] = self.exactSolution(i,j,x,y);
          vel_bc(i,j).u = u;
          vel_bc(i,j).v = v;
  
    for v in vars:
      v.beginGhostComm(); v.endGhostComm()

  def exactSolution(self,i,j,x,y):
    earth_grav = self.config.get("standard_gravity");
    f = self.ice.rho * earth_grav * H0* dhdx;
    ynd = y/L
  
    u = 0.5*(f**3)*(L**4)/((B0*H0)**3)*(1-ynd**4);
    return [u,0]

# The main code for a run follows:
if __name__ == '__main__':
  context = PISM.Context()

  PISM.set_abort_on_sigint(True)

  for o in PISM.OptionsGroup(context.com,"","Test J"):
    Mx = PISM.optionsInt("-Mx","Number of grid points in x-direction",default=61)
    My = PISM.optionsInt("-My","Number of grid points in y-direction",default=61)
    output_file = PISM.optionsString("-o","output file",default="test_plug.nc")
    verbosity = PISM.optionsInt("-verbose","verbosity level",default=3)

  PISM.setVerbosityLevel(verbosity)
  tc = test_plug(Mx,My)
  tc.solve()
  tc.report()
  tc.write(output_file)
