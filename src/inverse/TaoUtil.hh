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

#ifndef TAOUTIL_HH_W42GJNRO
#define TAOUTIL_HH_W42GJNRO

#include <tao.h>
#include <string>
#include "pism_const.hh"
#include "TerminationReason.hh"

extern const char *const* TaoConvergedReasons;

class TaoInitializer {
public:
  TaoInitializer(int *argc, char ***argv, char *file, char *help);
  TaoInitializer(int *argc, char ***argv, char *help);
  TaoInitializer(int *argc, char ***argv);
  ~TaoInitializer();
private:
};


class TAOTerminationReason: public TerminationReason {
public:
  TAOTerminationReason( TaoSolverTerminationReason r);
  virtual void get_description( std::ostream &desc,int indent_level=0);
};

template<class Problem>
class TaoObjectiveCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetObjectiveRoutine(tao,
      TaoObjectiveCallback<Problem>::evaluateObjectiveCallback,
      &p ); CHKERRQ(ierr);
    return 0;
  }

protected:

  static PetscErrorCode evaluateObjectiveCallback(TaoSolver tao,
                                 Vec x, PetscReal *value, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->evaluateObjective(tao,x,value); CHKERRQ(ierr);
    return 0;
  }
};

template<class Problem>
class TaoMonitorCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetMonitor(tao,
      TaoMonitorCallback<Problem>::monitorTao,
      &p, NULL ); CHKERRQ(ierr);
    return 0;
  }

protected:

  static PetscErrorCode monitorTao(TaoSolver tao, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->monitorTao(tao); CHKERRQ(ierr);
    return 0;
  }
};

template<class Problem>
class TaoGetVariableBoundsCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetVariableBoundsRoutine(tao,
      TaoGetVariableBoundsCallback<Problem>::getVariableBounds,
      &p); CHKERRQ(ierr);
    return 0;
  }

protected:

  static PetscErrorCode getVariableBounds(TaoSolver tao, Vec lo, Vec hi, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->getVariableBounds(tao,lo,hi); CHKERRQ(ierr);
    return 0;
  }
};

template<class Problem>
class TaoGradientCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetGradientRoutine(tao,
      TaoGradientCallback<Problem>::evaluateGradient,
      &p ); CHKERRQ(ierr);
    return 0;
  }

protected:

  static PetscErrorCode evaluateGradientCallback(TaoSolver tao,
                                 Vec x, Vec gradient, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->evaluateGradient(tao,x,gradient); CHKERRQ(ierr);
    return 0;
  }
};

template<class Problem>
class TaoConvergenceCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetConvergenceTest(tao,
      TaoConvergenceCallback<Problem>::convergenceTestCallback,
      &p ); CHKERRQ(ierr);
    return 0;
  }

protected:

  static PetscErrorCode convergenceTestCallback(TaoSolver tao, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->convergenceTest(tao); CHKERRQ(ierr);
    return 0;
  }
};



template<class Problem, PetscErrorCode (Problem::*Callback)(TaoSolver,Vec,PetscReal*,Vec) >
class TaoObjGradCallback {
public:

  static PetscErrorCode connect(TaoSolver tao, Problem &p) {
    PetscErrorCode ierr;
    ierr = TaoSetObjectiveAndGradientRoutine(tao,
      TaoObjGradCallback<Problem,Callback>::evaluateObjectiveAndGradientCallback,
      &p ); CHKERRQ(ierr);
    return 0;
  }
  
protected:

  static PetscErrorCode evaluateObjectiveAndGradientCallback(TaoSolver tao,
                                 Vec x, PetscReal *value, Vec gradient, void *ctx ) {
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = (p->*Callback)(tao,x,value,gradient); CHKERRQ(ierr);
    return 0;
  }
};

//! \brief An interface for solving an optimization problem with TAO where the
//! problem itself is defined by a separate Problem class.
/*!  The primary interface to a TAO optimization problem is mediated by a
TaoSolver. This class wraps a solver and some of its initialization boilierplate,
and allows a separate class to define the function to be minimized.

All TAO minimization algorithms require computation of an objective function,
some also require either gradient or gradient and hessian computations.  Currently
a TaoBasicProblem only supports algorithms that use both objective and
gradient computations but no Hessians. This would be easy to extend in the future.

To use a TaoBasicSolver you craete a class that defines the objective function.
There are two forms (currently); the Problem class can either compute
the objective and gradient all at once or separately.  For an all at once
form, the following methods must be available.

class MyProblem {
public:

  PetscErrorCode evaluateObjectiveAndGradient(TaoSolver tao, Vec x, Vec gradient);  
  Vec initialValue();

}

Otherwise the following methods must be available:

class MyProblem {
public:

  PetscErrorCode evaluateObjective(TaoSolver tao, Vec x);
  PetscErrorCode evaluateGradient(TaoSolver tao, Vec gradient);
  Vec initialValue();
}

To use a TaoBasicSolver, you instantiate it as follows

MyProblem problem;
TaoBasicSolver<MyProblem> solver(PETSC_COMM_WORLD,"tao_cg",problem);

Solution is then performed as follows:

if(solver.solve()) {
  printf("Success: %s\n",solver.reasonDescription().c_str());
} else {
  printf("Failure: %s\n",solver.reasonDescription().c_str());
}

The Problem class provides the initial guess for the solution
Vec via its initialValue method, which is called once before
the main algorithm begins.  If the minimization algorithm
converges, then the same Vec will contain the solution.

If using the separate Objective/Gradient callbacks, the TaoBasicSolver
should be instatiated as:

TaoBasicSolver<MyProblem> solver(PETSC_COMM_WORLD,"tao_cg",problem,kTaoObjectiveGradientCallbacksCombined);

*/
template<class Problem>
class TaoBasicSolver {
public:
    
  TaoBasicSolver(MPI_Comm comm, const char* tao_type, Problem &prob):
  m_comm(comm), m_problem(prob)
   {
    PetscErrorCode ierr;
    ierr = this->construct(tao_type);
    if(ierr) {
      CHKERRCONTINUE(ierr);
      PetscPrintf(m_comm, "FATAL ERROR: TaoBasicProblem allocation failed.\n");
      PISMEnd();
    }    
  }
  
  virtual ~TaoBasicSolver() {
    PetscErrorCode ierr;
    ierr = this->destruct();
    if(ierr) {
      CHKERRCONTINUE(ierr);
      PetscPrintf(m_comm, "FATAL ERROR: TaoBasicProblem deallocation failed.\n");
      PISMEnd();
    }
  };

  virtual PetscErrorCode solve(TerminationReason::Ptr &reason) {
    PetscErrorCode ierr;

    /* Solve the application */ 
    Vec x0;
    ierr = m_problem.formInitialGuess(&x0,reason); CHKERRQ(ierr);
    if(reason->failed()) {
      TerminationReason::Ptr root_cause = reason;
      reason.reset(new GenericTerminationReason(-1,"Unable to form initial guess"));
      reason->set_root_cause(root_cause);
      return 0;
    }
    ierr = TaoSetInitialVector(m_tao, x0); CHKERRQ(ierr);
    ierr = TaoSolve(m_tao); CHKERRQ(ierr);  

    TaoSolverTerminationReason tao_reason;
    ierr = TaoGetTerminationReason(m_tao, &tao_reason); CHKERRQ(ierr);
    reason.reset(new TAOTerminationReason(tao_reason));

    return 0;
  }

  virtual Problem &problem() {
    return m_problem;
  }

protected:

  virtual PetscErrorCode construct(const char* tao_type) {
    PetscErrorCode ierr;
    ierr = TaoCreate(m_comm ,&m_tao); CHKERRQ(ierr); 
    ierr = TaoSetType(m_tao,tao_type); CHKERRQ(ierr);    
    ierr = m_problem.connect(m_tao); CHKERRQ(ierr);
    ierr = TaoSetFromOptions(m_tao); CHKERRQ(ierr);
    return 0;
  }

  virtual PetscErrorCode destruct() {
    PetscErrorCode ierr;
    if(m_tao) {
      ierr = TaoDestroy(&m_tao); CHKERRQ(ierr);
    }
    return 0; 
  }
  
  MPI_Comm m_comm;
  TaoSolver m_tao;
  Problem  &m_problem;
};

template<class Problem>
class TaoLCLCallbacks {
public:
  static PetscErrorCode connect(TaoSolver tao, Problem &p, Vec c, Mat Jc, Mat Jd, Mat Jcpc=NULL, Mat Jcinv=NULL) {
    PetscErrorCode ierr;
    ierr = TaoSetConstraintsRoutine(tao,c,TaoLCLCallbacks<Problem>::evaluateConstraintsCallback,&p); CHKERRQ(ierr);
    if(Jcpc==NULL) Jcpc = Jc;
    ierr = TaoSetJacobianStateRoutine(tao,Jc,Jcpc,Jcinv,TaoLCLCallbacks<Problem>::evaluateJacobianStateCallback,&p); CHKERRQ(ierr);
    ierr = TaoSetJacobianDesignRoutine(tao,Jd,TaoLCLCallbacks<Problem>::evaluateJacobianDesignCallback,&p); CHKERRQ(ierr);
    return 0;
  }
protected:
  static PetscErrorCode evaluateConstraintsCallback(TaoSolver tao, Vec x,Vec c, void*ctx){
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->evaluateConstraints(tao,x,c); CHKERRQ(ierr);
    return 0;
  }

  static PetscErrorCode evaluateJacobianStateCallback(TaoSolver tao, Vec x, Mat *J, Mat *Jpc, Mat *Jinv, MatStructure *structure, void*ctx){
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->evaluateConstraintsJacobianState(tao,x,J,Jpc,Jinv,structure);
      CHKERRQ(ierr);
    return 0;
  }

  static PetscErrorCode evaluateJacobianDesignCallback(TaoSolver tao, Vec x, Mat *J, void*ctx){
    PetscErrorCode ierr;
    Problem *p = reinterpret_cast<Problem *>(ctx);
    ierr = p->evaluateConstraintsJacobianDesign(tao,x,J); CHKERRQ(ierr);
    return 0;
  }
};

#endif /* end of include guard: TAOUTIL_HH_W42GJNRO */
