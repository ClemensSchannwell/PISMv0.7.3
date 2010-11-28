static const char help[] = 
"Solve Bodvardsson equations (Bueler interpretation) using SNES, a dof=2 Vec\n"
"holding both thickness H and velocity u, and a 2nd-order finite difference\n"
"scheme to approximate the coupled mass continuity and SSA stress balance PDEs.\n\n"
"These runs show success with both matrix-free and finite-difference-Jacobian:\n"
"  ./bodvardsson -snes_mf -da_grid_x 181 -snes_monitor\n"
"  ./bodvardsson -snes_fd -da_grid_x 181 -snes_monitor\n"
"Visualization and low resolution:\n"
"  ./bodvardsson -snes_mf -da_grid_x 16 -snes_monitor -snes_monitor_solution -draw_pause 1\n"
"See conv.sh for convergence graph.  Add option -upwind1 to see first-order upwinding.\n"
"But note wiggles in:\n"
"  ./bodvardsson -snes_fd -da_grid_x 30 -snes_monitor -snes_monitor_solution -draw_pause 1\n"
"TO DO:  * add Picard or Jacobian\n"
"        * reasonable initial guesses\n";

#include "petscda.h"
#include "petscsnes.h"

#include "../exactTestN.h"


/* we will use dof=2 DA, and at each grid point have a thickness H and a velocity u */
typedef struct {
  PetscReal H, u;
} Node;


/* User-defined application context.  Used esp. by FormFunctionLocal().  */
typedef struct {
  DA          da;      /* 1d,dof=2 distributed array for soln and residual */
  DA          scalarda;/* 1d,dof=1 distributed array for parameters depending on x */
  PetscInt    Mx, xs, xm;
  PetscTruth  upwind1; /* if true, used low-order upwinding */
  PetscReal   dx, secpera, n, rho, rhow, g;
  PetscReal   H0;      /* thickness at x=0, for Dirichlet condition on mass cont */
  PetscReal   xc;      /* location at which stress (Neumann) condition applied to SSA eqn */
  PetscReal   Txc;     /* vertically-integrated longitudinal stress at xc, for Neumann cond:
                            T = 2 H B |u_x|^{(1/n)-1} u_x  */
  PetscReal   epsilon; /* regularization of viscosity, a strain rate */
  PetscReal   scaleNode[2], descaleNode[2]; /* scaling "inside" SNES */
  Vec         Huexact; /* exact thickness (Huexact[i][0]) and exact velocity
                          (Huexact[i][1]) on regular grid */
  Vec         M;       /* surface mass balance on regular grid */
  Vec         beta;    /* sliding coefficient on regular grid */
  Vec         B_stag;  /* ice hardness on staggered grid*/
} AppCtx;


#undef __FUNCT__
#define __FUNCT__ "FillExactSoln"
/*  Compute the exact thickness and velocity on the regular grid. */
static PetscErrorCode FillExactSoln(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt       i;
  PetscScalar    x, dum1, dum2, dum3, dum4;
  Node           *Hu;

  PetscFunctionBegin;
  /* Compute regular grid exact soln and staggered-grid thickness over the
     locally-owned part of the grid */
  ierr = DAVecGetArray(user->da,user->Huexact,&Hu);CHKERRQ(ierr);
  for (i = user->xs; i < user->xs + user->xm; i++) {
    x = user->dx * (PetscReal)i;  /* = x_i = distance from dome */
    ierr = exactN(x, &(Hu[i].H), &dum1, &(Hu[i].u), &dum2, &dum3, &dum4); CHKERRQ(ierr);
  }
  ierr = DAVecRestoreArray(user->da,user->Huexact,&Hu);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "FillInitial"
/*  Put a not-unreasonable initial guess in Hu. */
static PetscErrorCode FillInitial(AppCtx *user, Vec *vHu)
{
  PetscErrorCode ierr;
  PetscInt       i;
  Node           *Hu;

  PetscFunctionBegin;
  ierr = DAVecGetArray(user->da,*vHu,&Hu);CHKERRQ(ierr);
  for (i = user->xs; i < user->xs + user->xm; i++) {
    Hu[i].H = 1000.0;
    Hu[i].u = 100.0 / user->secpera;
  }
  ierr = DAVecRestoreArray(user->da,*vHu,&Hu);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "FillDistributedParams"
/*  Compute the values of the surface mass balance, ice hardness, and sliding coeff. */
static PetscErrorCode FillDistributedParams(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt       i,Mx=user->Mx;
  PetscScalar    x, dum1, dum2, dum3, dum4, dum5, *M, *Bstag, *beta;

  PetscFunctionBegin;
  /* Compute regular grid exact soln and staggered-grid thickness over the
     locally-owned part of the grid */
  ierr = DAVecGetArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->B_stag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);
  for (i = user->xs; i < user->xs + user->xm; i++) {
    x = user->dx * (PetscReal)i;  /* = x_i = distance from dome; regular grid point */
    ierr = exactN(x, &dum1, &dum2, &dum3, &(M[i]), &dum4, &(beta[i])); CHKERRQ(ierr);
    x = x + (user->dx/2.0);       /* = x_{i+1/2}; staggered grid point */
    if (i < Mx-1) {
      ierr = exactN(x, &dum1, &dum2, &dum3, &dum4, &(Bstag[i]), &dum5); CHKERRQ(ierr);
    } else {
      Bstag[i] = -9999.9999;
    }
  }
  ierr = DAVecRestoreArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->B_stag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "GetFSR"
/* define a power of the strain rate:   F  \approx  |u_x|^q u_x   */
static inline PetscScalar GetFSR(PetscScalar dx, PetscScalar eps, PetscScalar n, 
                                 PetscScalar ul, PetscScalar ur) {
  PetscScalar dudx = (ur - ul) / dx, 
              q    = (1.0 / n) - 1.0;
  return PetscPowScalar(dudx * dudx + eps * eps, q / 2.0) * dudx;
}


#undef __FUNCT__
#define __FUNCT__ "BodFunctionLocal"
static PetscErrorCode BodFunctionLocal(DALocalInfo *info, Node *Hu, Node *f, AppCtx *user)
{
  PetscErrorCode ierr;
  PetscReal      rg = user->rho * user->g, dx = user->dx;
  PetscScalar    *M, *Bstag, *beta,
                 duH, ul, u, ur, dHdx, Fl, Fr, Tl, Tr;
  PetscInt       i, Mx = info->mx;
  Vec            locBstag;

  PetscFunctionBegin;

  /* we need stencil width on Bstag (but not for M, beta) */
  ierr = DAGetLocalVector(user->scalarda,&locBstag);CHKERRQ(ierr);  /* do NOT destroy it */
  ierr = DAGlobalToLocalBegin(user->scalarda,user->B_stag,INSERT_VALUES,locBstag); CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(user->scalarda,user->B_stag,INSERT_VALUES,locBstag); CHKERRQ(ierr);

  ierr = DAVecGetArray(user->scalarda,locBstag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);
  for (i = info->xs; i < info->xs + info->xm; i++) {
  
    /* MASS CONT */
    if (i == 0) {
      /* residual at left-most point is Dirichlet cond. */
      f[0].H = Hu[0].H - user->H0;
    } else {
      /* centered difference IS UNSTABLE:
        duH = Hu[i+1].u * Hu[i+1].H - ( (i == 1) ? 0.0 : Hu[i-1].u * Hu[i-1].H );
        duH *= 0.5; */

      if (user->upwind1) {
        /* 1st-order upwind; leftward difference because u > 0 (because dH/dx < 0) */
        duH = Hu[i].u * Hu[i].H - ( (i == 1) ? 0.0 : Hu[i-1].u * Hu[i-1].H );
      } else {
        /* 2nd-order upwind; see Beam-Warming discussion in R. LeVeque, "Finite Volume ..." */
        if (i > 1) {
          duH = 3.0 * Hu[i].u * Hu[i].H - 4.0 * Hu[i-1].u * Hu[i-1].H; 
          if (i >= 3) {
            duH += Hu[i-2].u * Hu[i-2].H;
          } /* if i == 2 then u=0 so uH = 0 */
          duH *= 0.5;
        } else { /* i == 1: use PDE  M - (uH)_x = 0 to get quadratic poly, then diff that */
          duH = - dx * M[0] + 2.0 * Hu[1].u * Hu[1].H;
        }
      }

      f[i].H = dx * M[i] - duH;
    }

    /* SSA */
    if (i == 0) {
      /* residual at left-most point is Dirichlet cond. */
      f[0].u = Hu[0].u - 0.0;
    } else {
      /* residual: SSA eqn */
      /* consecutive values of u */
      ul = (i == 1) ? 0.0 : Hu[i-1].u;
      u  = Hu[i].u;
      ur = (i == Mx-1) ? -1.1e30 : Hu[i+1].u;
      /* surface slope */
      if (i == 1) { 
        dHdx  = (Hu[i+1].H - user->H0) / (2.0 * dx);
      } else if (i == Mx-1) {
        /* nearly 2nd-order global convergence seems to occur even with this:
             dHdx  = (Hu[i].H - Hu[i-1].H) / dx; */
        dHdx  = (3.0*Hu[i].H - 4.0*Hu[i-1].H + Hu[i-2].H) / (2.0 * dx);
      } else { /* generic case */
        dHdx  = (Hu[i+1].H - Hu[i-1].H) / (2.0 * dx);
      }
      /* vertically-integrated longitudinal stress */
      Fl = GetFSR(dx,user->epsilon,user->n, ul,u);
      if (i == Mx-1) {
        Tl = 2.0 * (Hu[i-1].H + Hu[i].H) * Bstag[i-1] * Fl;
        Tr = (1.0 - user->rho / user->rhow) * user->rho * user->g * Hu[i].H * Hu[i].H;
        /* exact value: Tr = 2.0 * user->Txc; */
      } else {
        Fr = GetFSR(dx,user->epsilon,user->n, u,ur);
        Tl = (Hu[i-1].H + Hu[i].H) * Bstag[i-1] * Fl;
        Tr = (Hu[i].H + Hu[i+1].H) * Bstag[i] * Fr;        
      }

      f[i].u = (Tr - Tl) - dx * beta[i] * u - dx * rg * Hu[i].H * dHdx; /* SSA */

    }
  }
  ierr = DAVecRestoreArray(user->scalarda,locBstag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);

  ierr = DARestoreLocalVector(user->scalarda,&locBstag);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "FunctionLocalScaleShell"
static PetscErrorCode FunctionLocalScaleShell(DALocalInfo *info, Node *Hu, Node *f, AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt       i;
  /* residual scaling coeffs */
  PetscReal rscH      = 1.0 / user->H0,
            rscu      = user->secpera / 100.0,
            rscuH     = user->secpera / user->H0,
            rscstress = 1.0 / (user->rho * user->g * user->H0 * user->dx * 0.001);
  /* dimensionalize unknowns (put in "real" scale) */
  for (i = info->xs; i < info->xs + info->xm; i++) {
    Hu[i].H *= (user->scaleNode)[0];  Hu[i].u *= (user->scaleNode)[1];
  }
  /* compute residual in dimensional units */
  ierr = BodFunctionLocal(info, Hu, f, user); CHKERRQ(ierr);
  /* scale the residual to be O(1) */
  for (i = info->xs; i < info->xs + info->xm; i++) {
    if (i == 0) {
      f[0].H *= rscH;   f[0].u *= rscu;
    } else {
      f[i].H *= rscuH;  f[i].u *= rscstress; }
  }
  /* de-dimensionalize unknowns */
  for (i = info->xs; i < info->xs + info->xm; i++) {
    Hu[i].H *= (user->descaleNode)[0];  Hu[i].u *= (user->descaleNode)[1];
  }
  return 0;
}


/* TODO:
static PetscErrorCode BodJacobianMatrixLocal(DALocalInfo*,Node*,Mat,AppCtx*); */


#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  PetscErrorCode         ierr;

  SNES                   snes;                 /* nonlinear solver */
  Vec                    Hu,r;                 /* solution, residual vectors */
  Mat                    J;                    /* Jacobian matrix */
  AppCtx                 user;                 /* user-defined work context */
  PetscInt               its, i;               /* iteration count, index */
  PetscReal              tmp1, tmp2, tmp3, tmp4, tmp5,
                         errnorms[2];
  PetscTruth             eps_set = PETSC_FALSE, dump = PETSC_FALSE, exactinitial = PETSC_FALSE,
                         snes_mf_set, snes_fd_set;
  MatFDColoring          matfdcoloring = 0;
  ISColoring             iscoloring;
  SNESConvergedReason    reason;               /* Check convergence */
  
  PetscInitialize(&argc,&argv,(char *)0,help);

  ierr = PetscPrintf(PETSC_COMM_WORLD,
    "BODVARDSSON solves for thickness and velocity in 1D, steady ice stream\n"
    "  [run with -help for info and options]\n");CHKERRQ(ierr);

  user.n       = 3.0;          /* Glen flow law exponent */
  user.secpera = 31556926.0;
  user.rho     = 910.0;        /* kg m^-3 */
  user.rhow    = 1028.0;       /* kg m^-3 */
  user.g       = 9.81;         /* m s^-2 */
  
  /* ask Test N for its parameters, but only those we need to solve */
  ierr = params_exactN(&(user.H0), &tmp1, &(user.xc), &tmp2, &tmp3, &tmp4, &tmp5, 
                       &(user.Txc)); CHKERRQ(ierr);
  /* regularize using strain rate of 1/xc per year */
  user.epsilon = (1.0 / user.secpera) / user.xc;
  /* tools for non-dimensionalizing to improve equation scaling */
  user.scaleNode[0] = 1000.0;  user.scaleNode[1] = 100.0 / user.secpera;
  for (i = 0; i < 2; i++)  user.descaleNode[i] = 1.0 / user.scaleNode[i];
  
  ierr = PetscOptionsTruth("-snes_mf","","",PETSC_FALSE,&snes_mf_set,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsTruth("-snes_fd","","",PETSC_FALSE,&snes_fd_set,NULL);CHKERRQ(ierr);
  if (!snes_mf_set && !snes_fd_set) { 
    ierr = PetscPrintf(PETSC_COMM_WORLD,
       "\n***ERROR: bodvardsson currently needs -snes_mf or -snes_fd***\n\n"
       "USAGE BELOW:\n\n%s",help);CHKERRQ(ierr);
    PetscEnd();
  }
  if (snes_fd_set) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,
             "  Jacobian: approximated as matrix by finite-differencing using coloring\n"); CHKERRQ(ierr);
  } else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,
             "  matrix free: no precondition\n"); CHKERRQ(ierr);
  }

  ierr = PetscOptionsBegin(PETSC_COMM_WORLD,NULL,
      "bodvardsson program options",__FILE__);CHKERRQ(ierr);
  {
    ierr = PetscOptionsTruth("-bod_up_one","","",PETSC_FALSE,&user.upwind1,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsTruth("-bod_exact_init","","",PETSC_FALSE,&exactinitial,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsTruth("-bod_dump",
      "dump out exact and approximate solution and residual, as asci","",
      dump,&dump,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-bod_epsilon","regularization (a strain rate in units of 1/a)","",
                            user.epsilon * user.secpera,&user.epsilon,&eps_set);CHKERRQ(ierr);
    if (eps_set)  user.epsilon *= 1.0 / user.secpera;
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);

  /* Create machinery for parallel grid management (DA), nonlinear solver (SNES), 
     and Vecs for fields (solution, RHS).  Note default Mx=46 grid points means
     dx=10 km.  Also degrees of freedom = 2 (thickness and velocity
     at each point) and stencil radius = ghost width = 2 for 2nd-order upwinding.  */
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,-46,2,2,PETSC_NULL,&user.da);CHKERRQ(ierr);
  ierr = DASetUniformCoordinates(user.da,0.0,user.xc,
                                 PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
  ierr = DASetFieldName(user.da,0,"ice thickness (m)"); CHKERRQ(ierr);
  ierr = DASetFieldName(user.da,1,"ice velocity (m a-1)"); CHKERRQ(ierr);
  ierr = DAGetInfo(user.da,PETSC_IGNORE,&user.Mx,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);
  ierr = DAGetCorners(user.da,&user.xs,PETSC_NULL,PETSC_NULL,&user.xm,PETSC_NULL,PETSC_NULL);
                   CHKERRQ(ierr);
  user.dx = user.xc / (PetscReal)(user.Mx-1);

  /* another DA for scalar parameters, with same length */
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,user.Mx,1,1,PETSC_NULL,&user.scalarda);CHKERRQ(ierr);
  ierr = DASetUniformCoordinates(user.scalarda,0.0,user.xc,
                                 PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,
      "  Mx = %D points, dx = %.3f m\n  H0 = %.2f m, xc = %.2f km, Txc = %.5e Pa m\n",
      user.Mx, user.dx, user.H0, user.xc/1000.0, user.Txc);CHKERRQ(ierr);

  /* Extract/allocate global vectors from DAs and duplicate for remaining same types */
  ierr = DACreateGlobalVector(user.da,&Hu);CHKERRQ(ierr);
  ierr = VecSetBlockSize(Hu,2);CHKERRQ(ierr);
  ierr = VecDuplicate(Hu,&r);CHKERRQ(ierr); /* inherits block size */
  ierr = VecDuplicate(Hu,&user.Huexact);CHKERRQ(ierr); /* ditto */

  ierr = DACreateGlobalVector(user.scalarda,&user.M);CHKERRQ(ierr);
  ierr = VecDuplicate(user.M,&user.B_stag);CHKERRQ(ierr);
  ierr = VecDuplicate(user.M,&user.beta);CHKERRQ(ierr);

  ierr = DASetLocalFunction(user.da,(DALocalFunction1)FunctionLocalScaleShell);CHKERRQ(ierr);

  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);

  ierr = SNESSetFunction(snes,r,SNESDAFormFunction,&user);CHKERRQ(ierr);

  /* setting up a matrix is only actually needed for -snes_fd case */
  ierr = DAGetMatrix(user.da,MATAIJ,&J);CHKERRQ(ierr);

  /* tools needed so DA can use sparse matrix for its F.D. Jacobian approx */
  ierr = DAGetColoring(user.da,IS_COLORING_GLOBAL,MATAIJ,&iscoloring);CHKERRQ(ierr);
  ierr = MatFDColoringCreate(J,iscoloring,&matfdcoloring);CHKERRQ(ierr);
  ierr = ISColoringDestroy(iscoloring);CHKERRQ(ierr);
  ierr = MatFDColoringSetFunction(matfdcoloring,
               (PetscErrorCode (*)(void))SNESDAFormFunction,&user);CHKERRQ(ierr);
  ierr = MatFDColoringSetFromOptions(matfdcoloring);CHKERRQ(ierr);
  ierr = SNESSetJacobian(snes,J,J,SNESDefaultComputeJacobianColor,matfdcoloring);CHKERRQ(ierr);

  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  /* the the Bodvardsson (1955) exact solution allows setting M(x), B(x), beta(x), T(xc) */
  ierr = FillDistributedParams(&user);CHKERRQ(ierr);

  /* the exact thickness and exact ice velocity (user.uHexact) are known from Bodvardsson (1955) */
  ierr = FillExactSoln(&user); CHKERRQ(ierr);

  if (exactinitial) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"  using exact solution as initial guess\n");
             CHKERRQ(ierr);
    /* the initial guess is the exact continuum solution */
    ierr = VecCopy(user.Huexact,Hu); CHKERRQ(ierr);
  } else {
    ierr = FillInitial(&user, &Hu); CHKERRQ(ierr);
  }
  
  /************ SOLVE NONLINEAR SYSTEM  ************/
  /* recall that RHS  r  is used internally by KSP, and is set by the SNES */
  ierr = VecStrideScaleAll(Hu,user.descaleNode); CHKERRQ(ierr); /* de-dimensionalize initial guess */
  ierr = SNESSolve(snes,PETSC_NULL,Hu);CHKERRQ(ierr);
  ierr = VecStrideScaleAll(Hu,user.scaleNode); CHKERRQ(ierr); /* put back in "real" scale */

  ierr = SNESGetIterationNumber(snes,&its);CHKERRQ(ierr);
  ierr = SNESGetConvergedReason(snes,&reason);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  %s Number of Newton iterations = %D\n",
           SNESConvergedReasons[reason],its);CHKERRQ(ierr);

  if (dump) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  viewing combined result Hu\n");CHKERRQ(ierr);
    ierr = VecView(Hu,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  viewing combined exact result Huexact\n");CHKERRQ(ierr);
    ierr = VecView(user.Huexact,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  viewing final combined residual at Hu\n");CHKERRQ(ierr);
    ierr = VecView(r,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);
  }

  /* evaluate error relative to exact solution */
  ierr = VecAXPY(Hu,-1.0,user.Huexact); CHKERRQ(ierr);  /* Hu = - Huexact + Hu */
  ierr = VecStrideNormAll(Hu,NORM_INFINITY,errnorms); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "(dx,errHinf,erruinf) %.3f %.4e %.4e\n",
           user.dx,errnorms[0],errnorms[1]*user.secpera);CHKERRQ(ierr);

  ierr = VecDestroy(Hu);CHKERRQ(ierr);
  ierr = VecDestroy(r);CHKERRQ(ierr);
  ierr = VecDestroy(user.Huexact);CHKERRQ(ierr);
  ierr = VecDestroy(user.M);CHKERRQ(ierr);
  ierr = VecDestroy(user.B_stag);CHKERRQ(ierr);
  ierr = VecDestroy(user.beta);CHKERRQ(ierr);

  ierr = MatDestroy(J); CHKERRQ(ierr);

  ierr = SNESDestroy(snes);CHKERRQ(ierr);

  ierr = DADestroy(user.da);CHKERRQ(ierr);
  ierr = DADestroy(user.scalarda);CHKERRQ(ierr);

  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}

