// Copyright (C) 2004-2008 Jed Brown and Ed Bueler
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

#include <petscda.h>
#include <petscksp.h>
#include "iceModel.hh"


//! Manage the time-stepping and parallel communication for the temperature and age equations.
PetscErrorCode IceModel::temperatureAgeStep() {
  // update temp and age fields
  PetscErrorCode  ierr;

  PetscScalar  myCFLviolcount = 0.0,   // these are counts but they are type "PetscScalar"
               myVertSacrCount = 0.0;  // because that type works with PetscGlobalSum()

  // do CFL and vertical grid blow-out checking only in ageStep()
  ierr = ageStep(&myCFLviolcount); CHKERRQ(ierr);  // puts vtaunew in vWork3d[1]
    
  ierr = temperatureStep(&myVertSacrCount); CHKERRQ(ierr);  // puts vTnew in vWork3d[0]

  // no communication done in ageStep(), temperatureStep()
  // start temperature & age communication
  ierr = T3.beginGhostCommTransfer(Tnew3); CHKERRQ(ierr);
  ierr = tau3.beginGhostCommTransfer(taunew3); CHKERRQ(ierr);

  // none of this involves temp or age fields
  if (updateHmelt == PETSC_TRUE) {
    ierr = diffuseHmelt(); CHKERRQ(ierr);  // does communication
  }

  ierr = PetscGlobalSum(&myCFLviolcount, &CFLviolcount, grid.com); CHKERRQ(ierr);

  PetscScalar VertSacrCount;
  ierr = PetscGlobalSum(&myVertSacrCount, &VertSacrCount, grid.com); CHKERRQ(ierr);
  const PetscScalar bfsacrPRCNT = 100.0 * (VertSacrCount / (grid.Mx * grid.My));
  if (bfsacrPRCNT > 0.1) {
    ierr = verbPrintf(2,grid.com," [BPsacr=%.4f\%] ", bfsacrPRCNT); CHKERRQ(ierr);
  }

  // complete temperature & age communication
  ierr = T3.endGhostCommTransfer(Tnew3); CHKERRQ(ierr);
  ierr = tau3.endGhostCommTransfer(taunew3); CHKERRQ(ierr);

  return 0;
}


//! Takes a semi-implicit time-step for the temperature equation.
/*!
In summary, the conservation of energy equation is
    \f[ \rho c_p(T) \frac{dT}{dt} = k \frac{\partial^2 T}{\partial z^2} + \Sigma,\f] 
where \f$T(t,x,y,z)\f$ is the temperature of the ice.  This equation is the shallow approximation
of the full three-dimensional conservation of energy.  Note \f$dT/dt\f$ stands for the material
derivative, so advection is included.  Here \f$\rho\f$ is the density of ice, 
\f$c_p\f$ is its specific heat, and \f$k\f$ is its conductivity.  Also \f$\Sigma\f$ is the volume
strain heating (with SI units of J$/(\text{s} \text{m}^3)$).

\latexonly\index{BOMBPROOF!implementation for temperature equation}\endlatexonly
Note that both the temperature equation and the age equation involve advection.
We handle the horizontal advection explicitly by first-order upwinding.  We handle the
vertical advection implicitly by centered differencing when possible, and a retreat to
implicit first-order upwinding when necessary.  There is a CFL condition
for the horizontal explicit upwinding \lo\cite{MortonMayers}\elo.  We report any CFL violations,
but they are designed to not occur.

The vertical conduction term is also handled implicitly (i.e. by backward Euler).

We work from the bottom 
of the column upward in building the system to solve (in the semi-implicit time-stepping scheme).
The excess energy above pressure melting is converted to melt-water, and that a fraction 
of this melt water is transported to the base according to the scheme in excessToFromBasalMeltLayer().

The method uses equally-spaced calculation but the methods getValColumn(), setValColumn() interpolate 
back and forth from this equally-spaced calculational grid to the (usually) non-equally space storage 
grid.

In this procedure four scalar fields are modified: vHmelt, vbasalMeltRate, Tb3, and Tnew3.
But vHmelt, vbasalMeltRate and Tb3 will never need to communicate ghosted values (i.e. horizontal 
stencil neighbors.  The ghosted values for T3 are updated from the values in Tnew3 in the
communication done by temperatureAgeStep().
 
Here is a more complete discussion and derivation.

Consider a column of a slowly flowing and heat conducting material as shown in the left side 
of the next figure.  (The left side shows a general column of flowing and heat conduction 
material showing a small segment \f$V\f$.  The right side shows a more specific column of ice 
flowing and sliding over bedrock.)  This is an \em Eulerian view so the material flows through 
a column which remains fixed (and is notional).  The column is vertical.  We will 
assume when needed that it is rectangular in cross-section with cross-sectional area 
\f$\Delta x\Delta y\f$.

\image latex earlycols.png "Left: a general column of material.  Right: ice over bedrock." width=3in

[FIXME: CONTINUE TO MINE eqns3D.tex FOR MORE]

The application of the geothermal flux at the base of a column is a special case for which 
we give a finite difference argument.  This scheme follows the equation (2.114) in 
\lo\cite{MortonMayers}\elo.  We have the boundary condition
	\f[  -k \frac{\partial T}{\partial z} = G(t,x,y) \f]
where \f$G(t,x,y)\f$ is the applied geothermal flux, and it is applied at level \f$z=-B_0\f$ 
in the bedrock (which is the only case considered here).  We <em> add a virtual lower grid 
point </em> \f$z_{-1} = z_0 - \Delta  z\f$ and we approximate the above boundary condition 
at \f$z_0\f$ by the centered-difference
	\f[  -k \frac{T_{1} - T_{-1}}{2 \Delta z} = G. \f]
Here \f$T_k = T_{ijk}^{l+1}\f$.  We also apply the discretized conduction equation 
at \f$z_0\f$.  These two combined equations yield a simplified form
	\f[(1 + 2 KR) T_0 - 2 K T_1 = T_0 + \frac{2\Delta t}{\rho c_p \Delta z} G \f]
where \f$K = k \Delta t (\rho c \Delta z^2)^{-1}\f$.
 */
PetscErrorCode IceModel::temperatureStep(PetscScalar* vertSacrCount) {
  PetscErrorCode  ierr;

  const PetscScalar   dx = grid.dx, 
                      dy = grid.dy;

  PetscInt    Mz, Mbz;
  PetscScalar dzEQ, dzbEQ, *zlevEQ, *zblevEQ;

  ierr = getMzMbzForTempAge(Mz, Mbz); CHKERRQ(ierr);

  zlevEQ = new PetscScalar[Mz];
  zblevEQ = new PetscScalar[Mbz];

  ierr = getVertLevsForTempAge(Mz, Mbz, dzEQ, dzbEQ, zlevEQ, zblevEQ); CHKERRQ(ierr);

  ierr = verbPrintf((grid.isEqualVertSpacing()) ? 5 : 3,grid.com,
    "\n  [entering temperatureStep(); Mz = %d, dzEQ = %5.3f, Mbz = %d, dzbEQ = %5.3f]",
    Mz, dzEQ, Mbz, dzbEQ); CHKERRQ(ierr);

  const PetscScalar   nuEQ = dtTempAge / dzEQ;
                      
  const PetscInt      k0 = Mbz - 1;

  const PetscScalar   rho_c_I = ice->rho * ice->c_p;
  const PetscScalar   rho_c_br = bed_thermal.rho * bed_thermal.c_p;
  const PetscScalar   rho_c_av = (dzEQ * rho_c_I + dzbEQ * rho_c_br) / (dzEQ + dzbEQ);
  const PetscScalar   iceK = ice->k / rho_c_I;
  const PetscScalar   iceR = iceK * dtTempAge / PetscSqr(dzEQ);
  const PetscScalar   brK = bed_thermal.k / rho_c_br;
  const PetscScalar   brR = brK * dtTempAge / PetscSqr(dzbEQ);

  PetscScalar *Tb, *Tbnew;
  PetscScalar **Ts, **H, **b, **Ghf, **mask, **Hmelt, **Rb, **basalMeltRate;

  PetscScalar *u, *v, *w, *Sigma, *T, *Tnew;
  u = new PetscScalar[Mz];
  v = new PetscScalar[Mz];
  w = new PetscScalar[Mz];
  Sigma = new PetscScalar[Mz];
  T = new PetscScalar[Mz];
  Tnew = new PetscScalar[Mz];

  Tb = new PetscScalar[Mbz];
  Tbnew = new PetscScalar[Mbz];

  PetscScalar *Lp, *L, *D, *U, *x, *rhs, *work;  
  Lp = new PetscScalar[Mz+k0-1]; L = Lp-1; // ptr arith.; note L[0]=Lp[-1] not alloc
  D = new PetscScalar[Mz+k0];
  U = new PetscScalar[Mz+k0-1];
  x = new PetscScalar[Mz+k0];
  rhs = new PetscScalar[Mz+k0];
  work = new PetscScalar[Mz+k0];

  ierr = DAVecGetArray(grid.da2, vTs, &Ts); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbed, &b); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vGhf, &Ghf); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vHmelt, &Hmelt); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vRb, &Rb); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vbasalMeltRate, &basalMeltRate); CHKERRQ(ierr);
  
  ierr = u3.needAccessToVals(); CHKERRQ(ierr);
  ierr = v3.needAccessToVals(); CHKERRQ(ierr);
  ierr = w3.needAccessToVals(); CHKERRQ(ierr);
  ierr = Sigma3.needAccessToVals(); CHKERRQ(ierr);
  ierr = T3.needAccessToVals(); CHKERRQ(ierr);
  ierr = Tnew3.needAccessToVals(); CHKERRQ(ierr);

  ierr = Tb3.needAccessToVals(); CHKERRQ(ierr);

  PetscInt        myLowTempCount = 0;  // counts unreasonably low temperature values

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // this should *not* be replaced by call to grid.kBelowHeightEQ():
      const PetscInt  ks = static_cast<PetscInt>(floor(H[i][j]/dzEQ));

      // if isMarginal then only do vertical conduction for ice (i.e. ignor advection
      // and strain heating if isMarginal)
      const bool isMarginal = checkThinNeigh(H[i+1][j],H[i+1][j+1],H[i][j+1],H[i-1][j+1],
                                             H[i-1][j],H[i-1][j-1],H[i][j-1],H[i+1][j-1]);
      
      ierr = Tb3.getValColumn(i,j,Mbz,zblevEQ,Tb); CHKERRQ(ierr);

      if (Mbz > 1) { // bedrock present: build k=0:Mbz-1 eqns
        // gives O(\Delta t,\Delta z^2) convergence in Test K for equal spaced grid;
        // note L[0] not an allocated location:
        D[0] = (1.0 + 2.0 * brR);
        U[0] = - 2.0 * brR;  
        rhs[0] = Tb[0] + 2.0 * dtTempAge * Ghf[i][j] / (rho_c_br * dzbEQ);
      
        // bedrock only: pure vertical conduction problem
        // {from generic bedrock FV}
        for (PetscInt k=1; k < k0; k++) {
          L[k] = -brR;
          D[k] = 1.0 + 2.0 * brR;
          U[k] = -brR;
          rhs[k] = Tb[k];
        }
      }

      if (grid.isEqualVertSpacing()) {
        ierr = u3.getValColumnPL(i,j,Mz,zlevEQ,u); CHKERRQ(ierr);
        ierr = v3.getValColumnPL(i,j,Mz,zlevEQ,v); CHKERRQ(ierr);
        ierr = w3.getValColumnPL(i,j,Mz,zlevEQ,w); CHKERRQ(ierr);
        ierr = Sigma3.getValColumnPL(i,j,Mz,zlevEQ,Sigma); CHKERRQ(ierr);
        ierr = T3.getValColumnPL(i,j,Mz,zlevEQ,T); CHKERRQ(ierr);
      } else {
        // slower, but right for not-equal spaced
        ierr = u3.getValColumnQUAD(i,j,Mz,zlevEQ,u); CHKERRQ(ierr);
        ierr = v3.getValColumnQUAD(i,j,Mz,zlevEQ,v); CHKERRQ(ierr);
        ierr = w3.getValColumnQUAD(i,j,Mz,zlevEQ,w); CHKERRQ(ierr);
        ierr = Sigma3.getValColumnQUAD(i,j,Mz,zlevEQ,Sigma); CHKERRQ(ierr);
        ierr = T3.getValColumnQUAD(i,j,Mz,zlevEQ,T); CHKERRQ(ierr);
      }

      // bottom part of ice (and top of bedrock in some cases): k=Mbz eqn
      if (ks == 0) { // no ice; set T[0] to surface temp if grounded
        if (k0 > 0) { L[k0] = 0.0; } // note L[0] not allocated 
        D[k0] = 1.0;
        U[k0] = 0.0;
        // if floating and no ice then worry only about bedrock temps;
        // top of bedrock sees ocean
        if (modMask(mask[i][j]) == MASK_FLOATING) {
          rhs[k0] = ice->meltingTemp;
        } else { // top of bedrock sees atmosphere
          rhs[k0] = Ts[i][j];
        }
      } else { // ks > 0; there is ice
        planeStar ss;
        ierr = T3.getPlaneStarZ(i,j,0.0,&ss);
        const PetscScalar UpTu = (u[0] < 0) ? u[0] * (ss.ip1 -  ss.ij) / dx :
                                              u[0] * (ss.ij  - ss.im1) / dx;
        const PetscScalar UpTv = (v[0] < 0) ? v[0] * (ss.jp1 -  ss.ij) / dy :
                                              v[0] * (ss.ij  - ss.jm1) / dy;
        // for w, always difference *up* from base, but make it implicit
//O <-- means OLD (before BOMBPROOF)
//O        const PetscScalar UpTw = w[0] * (T[1] - T[0]) / dzEQ;
        if (modMask(mask[i][j]) == MASK_FLOATING) {
          // at base of ice shelf, set T = Tpmp but also determine dHmelt/dt
          // by ocean flux; note volume for which energy is being computed is 
          // *half* a segment
          if (k0 > 0) { L[k0] = 0.0; } // note L[0] not allocated 
//O          D[k0] = 1.0 + 2.0 * iceR; 
//O          U[k0] = - 2.0 * iceR;
          const PetscScalar AA = dtTempAge * w[0] / (2.0 * dzEQ);
          D[k0] = 1.0 + 2.0 * iceR - AA;
          U[k0] = - 2.0 * iceR + AA;
          rhs[k0] = T[0] + 2.0 * dtTempAge * oceanHeatFlux / (rho_c_I * dzEQ);
          if (!isMarginal) {
//O            rhs[k0] += dtTempAge * (Sigma[0] - UpTu - UpTv - UpTw) / 2;
            rhs[k0] += dtTempAge * (Sigma[0] / rho_c_I - UpTu - UpTv) / 2;
          }
        } else { 
          // there is *grounded* ice; ice/bedrock interface; from FV across interface
          const PetscScalar rho_c_ratio = rho_c_I / rho_c_av;
          const PetscScalar dzav = 0.5 * (dzEQ + dzbEQ);
          rhs[k0] = T[0] + dtTempAge * (Rb[i][j] / (rho_c_av * dzav));
          if (!isMarginal) {
//O            rhs[k0] += dtTempAge * rho_c_ratio * 0.5 * Sigma[0];
            rhs[k0] += dtTempAge * rho_c_ratio * 0.5 * (Sigma[0] / rho_c_I);
            // WARNING: subtle consequences of finite volume argument across interface
//O            rhs[k0] -= dtTempAge * rho_c_ratio
//O                         * (0.5 * (UpTu + UpTv + UpTw) + T[0] * w[0] / dzEQ);
            rhs[k0] -= dtTempAge * rho_c_ratio
                         * (0.5 * (UpTu + UpTv));
          }
          const PetscScalar iceReff = ice->k * dtTempAge / (rho_c_av * dzEQ * dzEQ);
          const PetscScalar brReff = bed_thermal.k * dtTempAge / (rho_c_av * dzbEQ * dzbEQ);
          const PetscScalar AA = dtTempAge * rho_c_ratio * w[0] / (2.0 * dzEQ);  //NEW
          if (Mbz > 1) { // there is bedrock; apply upwinding if w[0]<0,
                         // otherwise ignor advection; note 
                         // jump in diffusivity coefficient
            L[k0] = - brReff;
//O            D[k0] = 1.0 + iceReff + brReff;
//O            U[k0] = - iceReff;
//O_052808:            D[k0] = 1.0 + iceReff + brReff + AA;
            if (w[0] >= 0.0) {  // velocity upward
              D[k0] = 1.0 + iceReff + brReff;
              U[k0] = - iceReff;
            } else { // velocity downward
              D[k0] = 1.0 + iceReff + brReff - AA;
              U[k0] = - iceReff + AA;
            }
          } else { // no bedrock; apply geothermal flux here
            // L[k0] = 0.0;  (note this is not an allocated location!) 
//O            D[k0] = 1.0 + 2.0 * iceR;
//O            U[k0] = - 2.0 * iceR;
//O_052808            D[k0] = 1.0 + 2.0 * iceR + AA;
            if (w[0] >= 0.0) {  // velocity upward
              D[k0] = 1.0 + 2.0 * iceR;
              U[k0] = - 2.0 * iceR;
            } else { // velocity downward
              D[k0] = 1.0 + 2.0 * iceR - AA;
              U[k0] = - 2.0 * iceR + AA;
            }
            rhs[k0] += 2.0 * dtTempAge * Ghf[i][j] / (rho_c_I * dzEQ);
          }
        }
      }

      // go through column and find appropriate lambda for BOMBPROOF
      PetscScalar lambda = 1.0;  // start with centered implicit for more accuracy
      for (PetscInt k = 1; k < ks; k++) {   
        const PetscScalar denom = (PetscAbs(w[k]) + 0.000001/secpera)
                                  * ice->rho * ice->c_p * dzEQ;  
        lambda = PetscMin(lambda, 2.0 * ice->k / denom);
      }
      if (lambda < 1.0)   *vertSacrCount += 1; // count columns in which lambda < 1 in BOMBPROOF


      // generic ice segment: build k0+1:k0+ks-1 eqns
      for (PetscInt k = 1; k < ks; k++) {
        planeStar ss;
        ierr = T3.getPlaneStarZ(i,j,k * dzEQ,&ss);
        const PetscScalar UpTu = (u[k] < 0) ? u[k] * (ss.ip1 -  ss.ij) / dx :
                                              u[k] * (ss.ij  - ss.im1) / dx;
        const PetscScalar UpTv = (v[k] < 0) ? v[k] * (ss.jp1 -  ss.ij) / dy :
                                              v[k] * (ss.ij  - ss.jm1) / dy;
//O        const PetscScalar UpTw = (w[k] < 0) ? w[k] * (T[k+1] -   T[k]) / dzEQ :
//O                                              w[k] * (T[k]   - T[k-1]) / dzEQ;
//O        L[k0+k] = -iceR; D[k0+k] = 1+2*iceR; U[k0+k] = -iceR;
//        const PetscScalar AA = 0.0; //REMOVEME
        const PetscScalar AA = nuEQ * w[k];      
        if (w[k] >= 0.0) {  // velocity upward
          L[k0+k] = - iceR - AA * (1.0 - lambda/2.0);
          D[k0+k] = 1.0 + 2.0 * iceR + AA * (1.0 - lambda);
          U[k0+k] = - iceR + AA * (lambda/2.0);
        } else {  // velocity downward ward
          L[k0+k] = - iceR - AA * (lambda/2.0);
          D[k0+k] = 1.0 + 2.0 * iceR - AA * (1.0 - lambda);
          U[k0+k] = - iceR + AA * (1.0 - lambda/2.0);
        }
/*O
        L[k0+k] = -iceR;
        D[k0+k] = 1.0 + 2.0 * iceR;
        U[k0+k] = -iceR;
*/
        rhs[k0+k] = T[k];
        if (!isMarginal) {
//          rhs[k0+k] += dtTempAge * (Sigma[k] - UpTu - UpTv - UpTw);
          rhs[k0+k] += dtTempAge * (Sigma[k] / rho_c_I - UpTu - UpTv);
        }
      }
      
      // surface b.c.
      if (ks>0) {
        L[k0+ks] = 0.0;
        D[k0+ks] = 1.0;
        // ignor U[k0+ks]
        rhs[k0+ks] = Ts[i][j];
      }

      // solve system; melting not addressed yet
      if (k0+ks>0) {
        ierr = solveTridiagonalSystem(L, D, U, x, rhs, work, k0+ks+1);
        // OLD:       ierr = solveTridiagonalSystem(L, D, U, x, rhs, work, k0+ks);
        if (ierr != 0) {
          SETERRQ3(1, "Tridiagonal solve failed at (%d,%d) with zero pivot in position %d.",
               i, j, ierr);
        }
      }

      // insert bedrock solution; check for too low below
      for (PetscInt k=0; k < k0; k++) {
        Tbnew[k] = x[k];
      }

      // prepare for melting/refreezing
      PetscScalar Hmeltnew = Hmelt[i][j];
      
      // insert solution for generic ice segments
      for (PetscInt k=1; k <= ks; k++) {
        if (allowAboveMelting == PETSC_TRUE) {
          Tnew[k] = x[k0 + k];
        } else {
          const PetscScalar depth = H[i][j] - zlevEQ[k];
          const PetscScalar Tpmp = ice->meltingTemp - ice->beta_CC_grad * depth;
          if (x[k0 + k] > Tpmp) {
            Tnew[k] = Tpmp;
            PetscScalar Texcess = x[k0 + k] - Tpmp; // always positive
            excessToFromBasalMeltLayer(rho_c_I, zlevEQ[k], dzEQ, &Texcess, &Hmeltnew);
            // Texcess  will always come back zero here; ignor it
          } else {
            Tnew[k] = x[k0 + k];
          }
        }
        if (Tnew[k] < globalMinAllowedTemp) {
           ierr = PetscPrintf(PETSC_COMM_SELF,
              "  [[too low (<200) ice segment temp T = %f at %d,%d,%d;"
              " proc %d; mask=%f; w=%f]]\n",
              Tnew[k],i,j,k,grid.rank,mask[i][j],w[k]*secpera); CHKERRQ(ierr);
           myLowTempCount++;
        }
      }
      
      // insert solution for ice/rock interface (or base of ice shelf) segment
      if (ks > 0) {
        if (allowAboveMelting == PETSC_TRUE) {
          Tnew[0] = x[k0];
        } else {  // compute diff between x[k0] and Tpmp; melt or refreeze as appropriate
          const PetscScalar Tpmp = ice->meltingTemp - ice->beta_CC_grad * H[i][j];
          PetscScalar Texcess = x[k0] - Tpmp; // positive or negative
          if (modMask(mask[i][j]) == MASK_FLOATING) {
             // when floating, only half a segment has had its temperature raised
             // above Tpmp
             excessToFromBasalMeltLayer(rho_c_I/2, 0.0, dzEQ, &Texcess, &Hmeltnew);
          } else {
             excessToFromBasalMeltLayer(rho_c_av, 0.0, dzEQ, &Texcess, &Hmeltnew);
          }
          Tnew[0] = Tpmp + Texcess;
          if (Tnew[0] > (Tpmp + 0.00001)) {
            SETERRQ(1,"updated temperature came out above Tpmp");
          }
        }
        if (Tnew[0] < globalMinAllowedTemp) {
           ierr = PetscPrintf(PETSC_COMM_SELF,
              "  [[too low (<200) ice/bedrock segment temp T = %f at %d,%d;"
              " proc %d; mask=%f; w=%f]]\n",
              Tnew[0],i,j,grid.rank,mask[i][j],w[0]*secpera); CHKERRQ(ierr);
           myLowTempCount++;
        }
      } else {
        Hmeltnew = 0.0;
      }
      
      // we must agree on redundant values T(z=0) at top of bedrock and at bottom of ice
      if (ks > 0) {
        Tbnew[k0] = Tnew[0];
      } else {
        // if floating then top of bedrock sees ocean
        if (modMask(mask[i][j]) == MASK_FLOATING) {
          Tbnew[k0] = ice->meltingTemp;
        } else { // top of bedrock sees atmosphere
          Tbnew[k0] = Ts[i][j];
        }
      }
      // check bedrock solution        
      for (PetscInt k=0; k <= k0; k++) {
        if (Tbnew[k] < globalMinAllowedTemp) {
           ierr = PetscPrintf(PETSC_COMM_SELF,
              "  [[too low (<200) bedrock segment temp T = %f at %d,%d,%d;"
              " proc %d; mask=%f]]\n",
              Tbnew[k],i,j,k,grid.rank,mask[i][j]); CHKERRQ(ierr);
           myLowTempCount++;
        }
      }

      // transfer column into Tb3; neighboring columns will not reference!
      ierr = Tb3.setValColumn(i,j,Mbz,zblevEQ,Tbnew); CHKERRQ(ierr);

      // set to air temp above ice
      for (PetscInt k=ks; k<Mz; k++) {
        Tnew[k] = Ts[i][j];
      }

      // transfer column into Tnew3; communication later
      ierr = Tnew3.setValColumnPL(i,j,Mz,zlevEQ,Tnew); CHKERRQ(ierr);

      // basaMeltRate[][] is rate of change of Hmelt[][]; thus it can be negative
      basalMeltRate[i][j] = (Hmeltnew - Hmelt[i][j]) / dtTempAge;

      // limit Hmelt by default max
      Hmeltnew = PetscMin(Hmelt_max, Hmeltnew);

      // eliminate basal water if floating
      if (modMask(mask[i][j]) == MASK_FLOATING) {
        Hmelt[i][j] = 0.0;
      } else {
        Hmelt[i][j] = Hmeltnew;
      }

    } 
  }
  
  if (myLowTempCount > maxLowTempCount) { SETERRQ(1,"too many low temps"); }

  ierr = DAVecRestoreArray(grid.da2, vTs, &Ts); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vbed, &b); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vGhf, &Ghf); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vMask, &mask); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vHmelt, &Hmelt); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vRb, &Rb); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vbasalMeltRate, &basalMeltRate); CHKERRQ(ierr);

  ierr = Tb3.doneAccessToVals(); CHKERRQ(ierr);

  ierr = u3.doneAccessToVals(); CHKERRQ(ierr);
  ierr = v3.doneAccessToVals(); CHKERRQ(ierr);
  ierr = w3.doneAccessToVals(); CHKERRQ(ierr);
  ierr = Sigma3.doneAccessToVals(); CHKERRQ(ierr);
  ierr = T3.doneAccessToVals(); CHKERRQ(ierr);
  ierr = Tnew3.doneAccessToVals(); CHKERRQ(ierr);
  
  delete [] Lp; delete [] D; delete [] U; delete [] x; delete [] rhs; delete [] work;

  delete [] u;  delete [] v;  delete [] w;  delete [] Sigma;
  delete [] T;  delete [] Tb;  delete [] Tbnew;  delete [] Tnew;

  delete [] zlevEQ;   delete [] zblevEQ;
 
  return 0;
}


//! Compute the melt water which should go to the base if \f$T\f$ is above pressure-melting.
PetscErrorCode IceModel::excessToFromBasalMeltLayer(
                const PetscScalar rho_c, const PetscScalar z, const PetscScalar dz,
                PetscScalar *Texcess, PetscScalar *Hmelt) {

  const PetscScalar darea = grid.dx * grid.dy;
  const PetscScalar dvol = darea * dz;
  const PetscScalar dE = rho_c * (*Texcess) * dvol;
  const PetscScalar massmelted = dE / ice->latentHeat;

  if (allowAboveMelting == PETSC_TRUE) {
    SETERRQ(1,"excessToBasalMeltLayer() called but allowAboveMelting==TRUE");
  }
  if (*Texcess >= 0.0) {
    if (updateHmelt == PETSC_TRUE) {
      // T is at or above pressure-melting temp, so temp needs to be set to 
      // pressure-melting, and a fraction of excess energy
      // needs to be turned into melt water at base
      // note massmelted is POSITIVE!
      const PetscScalar FRACTION_TO_BASE
                           = (z < 100.0) ? 0.2 * (100.0 - z) / 100.0 : 0.0;
      // note: ice-equiv thickness:
      *Hmelt += (FRACTION_TO_BASE * massmelted) / (ice->rho * darea);  
    }
    *Texcess = 0.0;
  } else if (updateHmelt == PETSC_TRUE) {  // neither Texcess nor Hmelt need to change 
                                           // if Texcess < 0.0
    // Texcess negative; only refreeze (i.e. reduce Hmelt) if at base and Hmelt > 0.0
    // note ONLY CALLED IF AT BASE!   note massmelted is NEGATIVE!
    if (z > 0.00001) {
      SETERRQ(1, "excessToBasalMeltLayer() called with z not at base and negative Texcess");
    }
    if (*Hmelt > 0.0) {
      const PetscScalar thicknessToFreezeOn = - massmelted / (ice->rho * darea);
      if (thicknessToFreezeOn <= *Hmelt) { // the water *is* available to freeze on
        *Hmelt -= thicknessToFreezeOn;
        *Texcess = 0.0;
      } else { // only refreeze Hmelt thickness of water; update Texcess
        *Hmelt = 0.0;
        const PetscScalar dTemp = ice->latentHeat * ice->rho * (*Hmelt) / (rho_c * dz);
        *Texcess += dTemp;
      }
    } 
    // note: if *Hmelt == 0 and Texcess < 0.0 then Texcess unmolested; temp will go down
  }
  return 0;
}                           


//! Take a semi-implicit time-step for the age equation.  Also check the horizontal CFL for advection.
/*!
The age equation is\f$d\tau/dt = 1\f$, that is,
    \f[ \frac{\partial \tau}{\partial t} + u \frac{\partial \tau}{\partial x}
        + v \frac{\partial \tau}{\partial y} + w \frac{\partial \tau}{\partial z} = 1\f]
where \f$\tau(t,x,y,z)\f$ is the age of the ice and \f$(u,v,w)\f$  is the three dimensional
velocity field.  This equation is hyperbolic (purely advective).  
The boundary condition is that when the ice fell as snow it had age zero.  
That is, \f$\tau(t,x,y,h(t,x,y)) = 0\f$ in accumulation areas, while there is no 
boundary condition elsewhere (as the characteristics go outward elsewhere).

If the velocity in the bottom cell of ice is upward (w[i][j][0]>0) then we apply
an age=0 boundary condition.  This is the case where ice freezes on at the base,
either grounded basal ice or marine basal ice.

A related matter:  By default, when computing the grain size for the 
Goldsby-Kohlstedt flow law, the age \f$\tau\f$ is not used.  Instead a pseudo age 
is computed by updateGrainSizeNow().  If you want the age computed by this routine 
to be used for the grain size estimation, 
from the Vostok core relation as in grainSizeVostok(), add option 
<tt>-real_age_grainsize</tt>.

\latexonly\index{BOMBPROOF!implementation for age equation}\endlatexonly
The numerical method is first-order upwind but the vertical advection term is computed
implicitly.  Thus there is no CFL-type stability condition for that part.

We use equally-spaced vertical grid in the calculation.  Note that the IceModelVec3 
methods getValColumn() and setValColumn() interpolate back and forth between the grid 
on which calculation is done and the storage grid.  Thus the storage grid can be either 
equally spaced or not.
 */
PetscErrorCode IceModel::ageStep(PetscScalar* CFLviol) {
  PetscErrorCode  ierr;

  PetscInt    Mz, dummyM;
  PetscScalar dzEQ, dummydz, *zlevEQ, *dummylev;

  ierr = getMzMbzForTempAge(Mz, dummyM); CHKERRQ(ierr);

  zlevEQ = new PetscScalar[Mz];
  dummylev = new PetscScalar[dummyM];

  ierr = getVertLevsForTempAge(Mz, dummyM, dzEQ, dummydz, zlevEQ, dummylev);
     CHKERRQ(ierr);

  const PetscScalar dx = grid.dx,
                    dy = grid.dy,
                    cflx = dx / dtTempAge,
                    cfly = dy / dtTempAge,
                    nuEQ = dtTempAge / dzEQ;

  PetscScalar **H, *tau, *u, *v, *w;

  tau = new PetscScalar[Mz];
  u = new PetscScalar[Mz];
  v = new PetscScalar[Mz];
  w = new PetscScalar[Mz];

  PetscScalar *Lp, *L, *D, *U, *x, *rhs, *work;  
  Lp = new PetscScalar[Mz-1]; L = Lp-1; // ptr arith.; note L[0]=Lp[-1] not alloc
  D = new PetscScalar[Mz];
  U = new PetscScalar[Mz-1];
  x = new PetscScalar[Mz];
  rhs = new PetscScalar[Mz];
  work = new PetscScalar[Mz];
  
  ierr = DAVecGetArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = tau3.needAccessToVals(); CHKERRQ(ierr);
  ierr = u3.needAccessToVals(); CHKERRQ(ierr);
  ierr = v3.needAccessToVals(); CHKERRQ(ierr);
  ierr = w3.needAccessToVals(); CHKERRQ(ierr);
  ierr = taunew3.needAccessToVals(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      // this should *not* be replaced by call to grid.kBelowHeightEQ():
      const PetscInt  ks = static_cast<PetscInt>(floor(H[i][j]/dzEQ));
      if (ks > Mz-1) {
        SETERRQ3(1,
           "ageStep() ERROR: ks = %d too high in ice column;\n"
           "  H[i][j] = %5.4f exceeds Lz = %5.4f\n",
           ks, H[i][j], grid.Lz);
      }

      if (ks == 0) { // if no ice, set the entire column to zero age
                     // and ignor the velocities in that column
        ierr = taunew3.setToConstantColumn(i,j,0.0); CHKERRQ(ierr);
      } else { // general case
        ierr = tau3.getValColumnQUAD(i,j,Mz,zlevEQ,tau); CHKERRQ(ierr);
        ierr = u3.getValColumnQUAD(i,j,Mz,zlevEQ,u); CHKERRQ(ierr);
        ierr = v3.getValColumnQUAD(i,j,Mz,zlevEQ,v); CHKERRQ(ierr);
        ierr = w3.getValColumnQUAD(i,j,Mz,zlevEQ,w); CHKERRQ(ierr);

        // age evolution is pure advection (so provides check on temp calculation):
        //   check horizontal CFL conditions at each point
        for (PetscInt k=0; k<ks; k++) {
          if (PetscAbs(u[k]) > cflx)  *CFLviol += 1.0;
          if (PetscAbs(v[k]) > cfly)  *CFLviol += 1.0;
        }

        // set up system: 0 <= k < ks
        for (PetscInt k=0; k<ks; k++) {
          planeStar ss;  // note ss.ij = tau[k]
          ierr = tau3.getPlaneStarZ(i,j,zlevEQ[k],&ss);
          // do lowest-order upwinding, explicitly for horizontal
          rhs[k] =  (u[k] < 0) ? u[k] * (ss.ip1 -  ss.ij) / dx
                               : u[k] * (ss.ij  - ss.im1) / dx;
          rhs[k] += (v[k] < 0) ? v[k] * (ss.jp1 -  ss.ij) / dy
                               : v[k] * (ss.ij  - ss.jm1) / dy;
          // note it is the age eqn: dage/dt = 1.0 and we have moved the hor.
          //   advection terms over to right:
          rhs[k] = ss.ij + dtTempAge * (1.0 - rhs[k]);

          // do lowest-order upwinding, *implicitly* for vertical
          PetscScalar AA = nuEQ * w[k];
          if (k > 0) {
            if (AA >= 0) { // upward velocity
              L[k] = - AA;
              D[k] = 1.0 + AA;
              U[k] = 0.0;
            } else { // downward velocity; note  -AA >= 0
              L[k] = 0.0;
              D[k] = 1.0 - AA;
              U[k] = + AA;
            }
          } else { // k == 0 case
            // note L[0] not an allocated location
            if (AA > 0) { // if strictly upward velocity apply boundary condition:
                          // age = 0 because ice is being added to base
              D[0] = 1.0;
              U[0] = 0.0;
              rhs[0] = 0.0;
            } else { // downward velocity; note  -AA >= 0
              D[0] = 1.0 - AA;
              U[0] = + AA;
              // keep rhs[0] as is
            }
          }

        }  // done "set up system: 0 <= k < ks"
      
        // surface b.c. at ks
        if (ks>0) {
          L[ks] = 0;
          D[ks] = 1.0;   // ignor U[ks]
          rhs[ks] = 0.0;  // age zero at surface
        }
        // done setting up system

        ierr = solveTridiagonalSystem(L, D, U, x, rhs, work, ks+1);
        if (ierr != 0) {
          SETERRQ3(2, "Tridiagonal solve failed at (%d,%d) with zero pivot in position %d.",
                   i, j, ierr);
        }
        // x[k] contains age for k=0,...,ks
        for (PetscInt k=ks+1; k<Mz; k++) {
          x[k] = 0.0;  // age of ice above (and at) surface is zero years
        }
        
        ierr = taunew3.setValColumnPL(i,j,Mz,zlevEQ,x); CHKERRQ(ierr);
      }
    }
  }

  ierr = DAVecRestoreArray(grid.da2, vH, &H); CHKERRQ(ierr);
  ierr = tau3.doneAccessToVals();  CHKERRQ(ierr);
  ierr = u3.doneAccessToVals();  CHKERRQ(ierr);
  ierr = v3.doneAccessToVals();  CHKERRQ(ierr);
  ierr = w3.doneAccessToVals();  CHKERRQ(ierr);
  ierr = taunew3.doneAccessToVals();  CHKERRQ(ierr);

  delete [] Lp; delete [] D; delete [] U; delete [] x; delete [] rhs; delete [] work;

  delete [] tau;  delete [] u;  delete [] v;  delete [] w;

  delete [] zlevEQ;  delete [] dummylev;

  return 0;
}


PetscErrorCode IceModel::diffuseHmelt() {
  PetscErrorCode  ierr;

  // diffusion constant K in u_t = K \nabla^2 u is chosen so that fundmental
  //   solution has standard deviation \sigma = 20 km at time t = 1000 yrs;
  //   2 \sigma^2 = 4 K t
  const PetscScalar K = 2.0e4 * 2.0e4 / (2.0 * 1000.0 * secpera),
                    Rx = K * dtTempAge / (grid.dx * grid.dx),
                    Ry = K * dtTempAge / (grid.dy * grid.dy);

  // NOTE: restriction that
  //    1 - 2 R_x - 2 R_y \ge 0
  // is a maximum principle restriction; therefore new Hmelt will be between
  // zero and Hmelt_max if old Hmelt has that property
  const PetscScalar oneM4R = 1.0 - 2.0 * Rx - 2.0 * Ry;
  if (oneM4R <= 0.0) {
    SETERRQ(1,
       "diffuseHmelt() has 1 - 2Rx - 2Ry <= 0 so explicit method for diffusion unstable\n"
       "  (timestep restriction believed so rare that is not part of adaptive scheme)");
  }

  PetscScalar **Hmelt, **Hmeltnew; 
  ierr = DAVecGetArray(grid.da2, vHmelt, &Hmelt); CHKERRQ(ierr);
  ierr = DAVecGetArray(grid.da2, vWork2d[0], &Hmeltnew); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      Hmeltnew[i][j] = oneM4R * Hmelt[i][j]
                       + Rx * (Hmelt[i+1][j] + Hmelt[i-1][j])
                       + Ry * (Hmelt[i][j+1] + Hmelt[i][j-1]);
    }
  }
  ierr = DAVecRestoreArray(grid.da2, vHmelt, &Hmelt); CHKERRQ(ierr);
  ierr = DAVecRestoreArray(grid.da2, vWork2d[0], &Hmeltnew); CHKERRQ(ierr);

  // finally copy new into vHmelt (and communicate ghosted values at same time)
  ierr = DALocalToLocalBegin(grid.da2, vWork2d[0], INSERT_VALUES, vHmelt); CHKERRQ(ierr);
  ierr = DALocalToLocalEnd(grid.da2, vWork2d[0], INSERT_VALUES, vHmelt); CHKERRQ(ierr);
  return 0;
}


bool IceModel::checkThinNeigh(PetscScalar E, PetscScalar NE, PetscScalar N, PetscScalar NW, 
                              PetscScalar W, PetscScalar SW, PetscScalar S, PetscScalar SE) {
  const PetscScalar THIN = 100.0;  // thin = (at most 100m thick)
  return (   (E < THIN) || (NE < THIN) || (N < THIN) || (NW < THIN)
          || (W < THIN) || (SW < THIN) || (S < THIN) || (SE < THIN) );
}


PetscErrorCode IceModel::solveTridiagonalSystem(
         const PetscScalar* L, const PetscScalar* D, const PetscScalar* U,
         PetscScalar* x, const PetscScalar* r, PetscScalar* a, const int n) const {
  // modified slightly from Numerical Recipes version

  PetscScalar b;
  b = D[0];
  if (b == 0.0) { return 1; }
  x[0] = r[0]/b;
  for (int i=1; i<n; ++i) {
    a[i] = U[i-1]/b;
    b = D[i] - L[i]*a[i];
    if (b == 0) { return i+1; }
    x[i] = (r[i] - L[i]*x[i-1]) / b;
  }
  for (int i=n-2; i>=0; --i) {
    x[i] -= a[i+1] * x[i+1];
  }

  return 0;
}


/*!
If the storage grid (defined by IceGrid) has equally-spaced vertical, then
the computation in temperatureStep() and ageStep() is done on that grid.  

If IceGrid defines a not equally spaced grid, however, then, internally in temperatureStep()
and ageStep(), we do computation on a fine and equally-spaced grid.  

This method determines the number of levels in the equally-spaced grid used within 
temperatureStep() and ageStep() in either case.  The method getVertLevsForTempAge() sets 
the spacing and the actual levels.

The storage grid may have quite different levels.  The mapping to the storage grid occurs in 
getValColumn(), setValColumn() for the IceModelVec3 or IceModelVec3Bedrock.
 */
PetscErrorCode IceModel::getMzMbzForTempAge(PetscInt &ta_Mz, PetscInt &ta_Mbz) {

#define min_to_equal_factor 1.0

  if (grid.isEqualVertSpacing()) {
    ta_Mbz = grid.Mbz;
    ta_Mz = grid.Mz;
  } else {
    const PetscScalar dz = min_to_equal_factor * grid.dzMIN;
    ta_Mz = 1 + static_cast<PetscInt>(ceil(grid.Lz / dz));
    ta_Mbz = 1 + static_cast<PetscInt>(ceil(grid.Lbz / dz));
  }
  return 0;
}


/*!
See comments for getMzMbzForTempAge().  The arrays ta_zlevEQ and ta_zblevEQ must 
already be allocated arrays of length ta_Mz, ta_Mbz, respectively.
 */
PetscErrorCode IceModel::getVertLevsForTempAge(const PetscInt ta_Mz, const PetscInt ta_Mbz,
                            PetscScalar &ta_dzEQ, PetscScalar &ta_dzbEQ, 
                            PetscScalar *ta_zlevEQ, PetscScalar *ta_zblevEQ) {

  if (grid.isEqualVertSpacing()) {
    ta_dzEQ = grid.dzMIN;
    ta_dzbEQ = grid.dzMIN;
    for (PetscInt k = 0; k < ta_Mz; k++) {
      ta_zlevEQ[k] = grid.zlevels[k];
    }
    for (PetscInt k = 0; k < ta_Mbz; k++) {
      ta_zblevEQ[k] = grid.zblevels[k];
    }
  } else {
    // exactly Mz-1 steps for [0,Lz]:
    ta_dzEQ = grid.Lz / ((PetscScalar) (ta_Mz - 1));  
    for (PetscInt k = 0; k < ta_Mz-1; k++) {
      ta_zlevEQ[k] = ((PetscScalar) k) * ta_dzEQ;
    }
    ta_zlevEQ[ta_Mz-1] = grid.Lz;  // make sure it is right on
    if (ta_Mbz > 1) {
      // exactly Mbz-1 steps for [-Lbz,0]:
      ta_dzbEQ = grid.Lbz / ((PetscScalar) (ta_Mbz - 1));  
      for (PetscInt kb = 0; kb < ta_Mbz-1; kb++) {
        ta_zblevEQ[kb] = - grid.Lbz + ta_dzbEQ * ((PetscScalar) kb);
      }
    } else {
      ta_dzbEQ = ta_dzEQ;
    }
    ta_zblevEQ[ta_Mbz-1] = 0.0;  // make sure it is right on
  }  
  return 0;
}

