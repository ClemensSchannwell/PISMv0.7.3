// Copyright (C) 2009--2011 Ed Bueler, Constantine Khroulev and David Maxwell
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

#ifndef _SSATESTCASE_H_
#define _SSATESTCASE_H_

#include "SSA.hh"


//! Callback for constructing a new SSA subclass.  The caller is
//! responsible for deleting the newly constructed SSA.
/* The algorithm for solving the SSA in a test case can be selected
at runtime via the ssafactory argument of SSATestCase::init.  The
factory is a function pointer that takes all the arguments of an SSA
constructor and returns a newly constructed instance.  By using this
mechanism, the member variables needed for construction of an SSA
do not need to be exposed to the outside code making the choice of algorithm.
*/
typedef SSA * (*SSAFactory)(IceGrid &, IceBasalResistancePlasticLaw &, 
              IceFlowLaw &, EnthalpyConverter &, const NCConfigVariable &);

//! Constructs a new SSAFEM
SSA * SSAFEMFactory(IceGrid &, IceBasalResistancePlasticLaw &, 
                  IceFlowLaw &, EnthalpyConverter &, const NCConfigVariable &);
//! Constructs a new SSAFD
SSA * SSAFDFactory(IceGrid &, IceBasalResistancePlasticLaw &, 
                  IceFlowLaw &, EnthalpyConverter &, const NCConfigVariable &);

//! Helper function for initializing a grid with the given dimensions.
//! The grid is shallow (3 z-layers) and is periodic in the x and y directions.
PetscErrorCode init_shallow_periodic_grid(IceGrid &grid, 
                                            PetscReal Lx, PetscReal Ly, 
                                                  PetscInt Mx, PetscInt My);


/*! An SSATestCase manages running an SSA instance against a particular
test.  Subclasses must implement the following abstract methods to define
the input to an SSA for a test case:

1) initializeGrid (to build a grid of the specified size appropraite for the test)
2) initializeSSAModel (to specify the laws used by the model, e.g. ice flow and basal sliding laws)
3) initializeSSACoefficients (to initialize the ssa coefficients, e.g. ice thickness)

The SSA itself is constructed between steps 2) and 3).

Additionally, a subclass can implement \c report to handle
printing statistics after a run.  The default report method relies
on subclasses implementing of the exactSolution method for comparision.

A driver uses an SSATestCase by calling 1-3 below and 4,5 as desired:

1) its constructor
2) init (to specify the grid size and choice of SSA algorithm)
3) run (to actually solve the ssa)
4) report
5) write (to save the results of the computation to a file)
*/
class SSATestCase
{
public:
  SSATestCase( MPI_Comm com, PetscMPIInt rank, 
               PetscMPIInt size, NCConfigVariable &config ): 
                  config(config), grid(com,rank,size,config), 
                  basal(0), ice(0), enthalpyconverter(0), ssa(0)
  {  };

  virtual ~SSATestCase()
  {
    delete basal;
    delete ice;
    delete enthalpyconverter;
    delete ssa;
  }

  virtual PetscErrorCode init(PetscInt Mx, PetscInt My,SSAFactory ssafactory);

  virtual PetscErrorCode run();

  virtual PetscErrorCode report();

  virtual PetscErrorCode write(const string &filename);

protected:

  virtual PetscErrorCode buildSSACoefficients();

  //! Initialize the member variable grid as appropriate for the test case.
  virtual PetscErrorCode initializeGrid(PetscInt Mx,PetscInt My) = 0;

  //! Allocate the member variables basal, ice, and enthalpyconverter as
  //! appropriate for the test case.
  virtual PetscErrorCode initializeSSAModel() = 0;

  //! Set up the coefficient variables as appropriate for the test case.
  virtual PetscErrorCode initializeSSACoefficients() = 0;

  //! Return the value of the exact solution at grid index (i,j) or equivalently
  //! at coordinates (x,y).
  virtual PetscErrorCode exactSolution(PetscInt i, PetscInt j, 
    PetscReal x, PetscReal y, PetscReal *u, PetscReal *v );

  NCConfigVariable &config;
  IceGrid grid;

  // SSA model variables.
  IceBasalResistancePlasticLaw *basal;
  IceFlowLaw *ice; 
  EnthalpyConverter *enthalpyconverter;

  // SSA coefficient variables.
  PISMVars vars;
  IceModelVec2S  surface, thickness, bed, tauc;
  IceModelVec3 enthalpy;
  IceModelVec2V vel_bc;
  IceModelVec2Mask mask;

  SSA *ssa;

};

#endif /* _SSATESTCASE_H_ */
