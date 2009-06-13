// Copyright (C) 2009 Andreas Aschwanden and Ed Bueler
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

#include "iceEnthalpyModel.hh"
#include "enthColumnSystem.hh"
#include "enthalpyHelper.hh"

#define DEBUGVERB 2

/*!
This constructor just sets flow law factor for nonzero water content, from
\ref AschwandenBlatter2009 and \ref LliboutryDuval1985.
 */
PolyThermalGPBLDIce::PolyThermalGPBLDIce(MPI_Comm c,const char pre[]) : ThermoGlenIce(c,pre) {
  config = NULL;
  water_frac_coeff = 184.0;   // FIXME:  should also come through config interface
}


PetscErrorCode PolyThermalGPBLDIce::setFromOptions() {
  PetscErrorCode ierr;

  ierr = ThermoGlenIce::setFromOptions(); CHKERRQ(ierr);
  
  ierr = PetscOptionsBegin(comm,prefix,"PolyThermalGPBLDIce options",NULL);CHKERRQ(ierr);
  {
    ierr = PetscOptionsReal("-ice_gpbld_water_frac_coeff",
      "coefficient of softness factor in temperate ice, as function of liquid water fraction (no units)",
      "",water_frac_coeff,&water_frac_coeff,NULL);CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PolyThermalGPBLDIce::view(PetscViewer viewer) const {
  PetscErrorCode ierr;

  ierr = ThermoGlenIce::view(viewer); CHKERRQ(ierr);
  
  PetscTruth iascii;
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(comm,&viewer);CHKERRQ(ierr);
  }
  ierr = PetscTypeCompare((PetscObject)viewer,PETSC_VIEWER_ASCII,&iascii);CHKERRQ(ierr);
  if (iascii) {
    ierr = PetscViewerASCIIPrintf(viewer,"<\nderived PolyThermalGPBLDIce object (%s)\n",prefix);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  water_frac_coeff=%4f\n>\n",water_frac_coeff);CHKERRQ(ierr);
  } else {
    SETERRQ(1,"No binary viewer for this object\n");
  }
  return 0;
}


//! The softness factor in the Paterson-Budd-Lliboutry-Duval flow law.  For constitutive law form.
PetscScalar PolyThermalGPBLDIce::softnessParameterFromEnth(PetscScalar enthalpy, PetscScalar pressure) const {
  if (config == NULL) {
    PetscPrintf(PETSC_COMM_WORLD,"config ptr is NULL in PolyThermalGPBLDIce::flowFromEnth()... ending\n");
    PetscEnd();
  }
  PetscScalar T_m, H_l, H_s;
  getEnthalpyInterval(*config, pressure, T_m, H_l, H_s);
  if (enthalpy <= H_s) {       // cold ice
    return softnessParameter( getPATemp(*config,enthalpy,pressure) ); // uses ThermoGlenIce formula
  } else if (enthalpy < H_l) { // temperate ice
    const PetscScalar T_0  = config->get("water_melting_temperature"),    // K
                      omega = getWaterFraction(*config,enthalpy,pressure);
    // next line implements eqn (23) in \ref AschwandenBlatter2009
    return softnessParameter(T_0) * (1.0 + water_frac_coeff * omega);  // uses ThermoGlenIce formula
  } else { // liquid water not allowed
    PetscPrintf(PETSC_COMM_WORLD,
      "\n\n\n  PISM ERROR in PolyThermalGlenPBLDIce::flow(): liquid water not allowed; ending ... \n\n");
    PetscEnd();
    return 0.0;
  }
}


//! The factor in the Paterson-Budd-Lliboutry-Duval flow law.  For viscosity form.
PetscScalar PolyThermalGPBLDIce::hardnessParameterFromEnth(PetscScalar enthalpy, PetscScalar pressure) const {
  return pow(softnessParameterFromEnth(enthalpy,pressure), -1.0/n);
}


//! Glen-Paterson-Budd-Lliboutry-Duval flow law itself.
PetscScalar PolyThermalGPBLDIce::flowFromEnth(
                PetscScalar stress, PetscScalar enthalpy, PetscScalar pressure, PetscScalar /* gs */) const {
  return softnessParameterFromEnth(enthalpy,pressure) * pow(stress,n-1);
}


PetscScalar PolyThermalGPBLDIce::effectiveViscosityColumnFromEnth(
                PetscScalar thickness,  PetscInt kbelowH, const PetscScalar *zlevels,
                PetscScalar u_x,  PetscScalar u_y, PetscScalar v_x,  PetscScalar v_y,
                const PetscScalar *enthalpy1, const PetscScalar *enthalpy2) const {
  if (config == NULL) {
    PetscPrintf(PETSC_COMM_WORLD,
       "config ptr is NULL in PolyThermalGPBLDIce::effectiveViscosityColumnFromEnth()... ending\n");
    PetscEnd();
  }

  // DESPITE NAME, does *not* return effective viscosity.
  // The result is \nu_e H, i.e. viscosity times thickness.
  // B is really hardness times thickness.

  // Integrate the hardness parameter using the trapezoid rule.
  PetscScalar B = 0;
  if (kbelowH > 0) {
    PetscScalar dz = zlevels[1] - zlevels[0];
    B += 0.5 * dz * hardnessParameterFromEnth( 0.5 * (enthalpy1[0] + enthalpy2[0]),
                                               getPressureFromDepth(*config, thickness) );
    for (PetscInt m=1; m < kbelowH; m++) {
      const PetscScalar dzNEXT = zlevels[m+1] - zlevels[m],
                        depth  = thickness - 0.5 * (zlevels[m+1] + zlevels[m]);
      B += 0.5 * (dz + dzNEXT) * hardnessParameterFromEnth( 0.5 * (enthalpy1[m] + enthalpy2[m]),
                                                            getPressureFromDepth(*config, depth) );
      dz = dzNEXT;
    }
    // use last dz from for loop
    const PetscScalar depth  = 0.5 * (thickness - zlevels[kbelowH]);
    B += 0.5 * dz * hardnessParameterFromEnth( 0.5 * (enthalpy1[kbelowH] + enthalpy2[kbelowH]),
                                               getPressureFromDepth(*config, depth) );
  }
  const PetscScalar alpha = secondInvariant(u_x, u_y, v_x, v_y);
  return 0.5 * B * pow(schoofReg + alpha, (1-n)/(2*n));
}



/*********** for registering new kind of ice with IceFactory ****************/

#define ICE_GPBLD      "gpbld"

static PetscErrorCode create_gpbld(MPI_Comm comm,const char pre[],IceType **i) {
  *i = new (PolyThermalGPBLDIce)(comm,pre);  return 0;
}


/*********** procedures for init ****************/

IceEnthalpyModel::IceEnthalpyModel(IceGrid &g) : IceModel(g) {
  doColdIceMethods = true;   // initial debugging will default to no actual enthalpy computation;
                             // we just read and write additional enthalpy field to and from file
}


PetscErrorCode IceEnthalpyModel::createVecs() {
  PetscErrorCode ierr;

  ierr = Enth3.create(grid, "enthalpy", true); CHKERRQ(ierr);
  // PROPOSED standard name = land_ice_enthalpy
  ierr = Enth3.set_attrs(
     "model_state",
     "ice enthalpy (sensible heat plus latent heat of liquid fraction)",
     "J kg-1",
     ""); CHKERRQ(ierr);

  ierr = IceModel::createVecs(); CHKERRQ(ierr);

  // see IceModel::allocate_internal_objects(), which is where this should go
  ierr = EnthNew3.create(grid,"enthalpy_new",false); CHKERRQ(ierr); // global
  ierr = EnthNew3.set_attrs(
     "internal",
     "ice enthalpy; temporary space during timestep",
     "J kg-1",
     ""); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceEnthalpyModel::init_physics() {
  PetscErrorCode ierr;

  // let the base class create the ice and process its options:
  ierr = IceModel::init_physics(); CHKERRQ(ierr);

  ierr = verbPrintf(DEBUGVERB, grid.com,
      "  setting flow law to Glen-Paterson-Budd-Lliboutry-Duval type ...\n");
      CHKERRQ(ierr);
  ierr = iceFactory.registerType(ICE_GPBLD, &create_gpbld);
  if (ierr != 0) {
    PetscPrintf(grid.com,
       "FAILURE OF iceFactory.registerType() ... return value %d ... ending ....\n",ierr);
    PetscEnd();
  }
  CHKERRQ(ierr);
  if (ice != NULL)  delete ice;  // kill choice already made!
  iceFactory.setType(ICE_GPBLD); // new flowlaw which has dependence on enthalpy not temperature
  iceFactory.create(&ice);

  PolyThermalGPBLDIce *gpbldi = dynamic_cast<PolyThermalGPBLDIce*>(ice);
  if (gpbldi) {
    gpbldi->config = &config;
  } else {
    ThermoGlenIce *tgi = dynamic_cast<ThermoGlenIce*>(ice);
    if (tgi) {
      ierr = verbPrintf(DEBUGVERB, grid.com,
        "  [flow law was actually set to ThermoGlenIce by IceEnthalpyModel ...]\n"); CHKERRQ(ierr);
    } else {
      ierr = verbPrintf(DEBUGVERB, grid.com,
        "  [WARNING: flow law unclear in IceEnthalpyModel ...]\n"); CHKERRQ(ierr);
    }
  }
  
  ierr = ice->printInfo(4);CHKERRQ(ierr); // DEBUG

  ierr = ice->setFromOptions();CHKERRQ(ierr);

  return 0;
}



/*********** procedures for read/write ****************/

PetscErrorCode IceEnthalpyModel::write_extra_fields(const char filename[]) {
  PetscErrorCode ierr;

  if (doColdIceMethods) { // in this case, just update Enth3 to reflect
                                  // temperature in ice at final time
    ierr = verbPrintf(DEBUGVERB, grid.com,
      "  using temperature to set enthalpy for writing (as cold ice) ...\n");
      CHKERRQ(ierr);
    ierr = setEnth3FromT3_ColdIce(); CHKERRQ(ierr);
  }
  ierr = Enth3.write(filename, NC_DOUBLE); CHKERRQ(ierr);

  // also write omega = liquid water fraction
  //   we use EnthNew3 (global) as temporary, allocated space for this purpose
  ierr = verbPrintf(DEBUGVERB, grid.com,
      "  writing liquid water fraction 'liquid_frac' from enthalpy ...\n"); CHKERRQ(ierr);
  ierr = setLiquidFracFromEnthalpy(EnthNew3); CHKERRQ(ierr);
  ierr = EnthNew3.write(filename, NC_DOUBLE); CHKERRQ(ierr);
  // reset attributes; probaly not needed
  ierr = EnthNew3.set_name("enthalpy_new"); CHKERRQ(ierr);
  ierr = EnthNew3.set_attrs(
     "internal",
     "ice enthalpy; temporary space during timestep",
     "J kg-1",
     ""); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceEnthalpyModel::initFromFile(const char *fname) {
  PetscErrorCode  ierr;

  ierr = IceModel::initFromFile(fname); CHKERRQ(ierr);

  ierr = verbPrintf(DEBUGVERB, grid.com,
     "entering IceEnthalpyModel::initFromFile() after base class version;\n"
     "  looking in '%s' for variable 'enthalpy' ... \n",fname);
     CHKERRQ(ierr);

  NCTool nc(&grid);
  ierr = nc.open_for_reading(fname); CHKERRQ(ierr);

/* if we were to require "enthalpy" to be present then the code would be simpler:
  ierr = Enth3.read(fname, last_record); CHKERRQ(ierr);
*/

  grid_info g;
  ierr = nc.get_grid_info(g); CHKERRQ(ierr);
  bool enthExists=false;
  ierr = nc.find_variable("enthalpy", NULL, enthExists); CHKERRQ(ierr);

  if (enthExists) {
    // act like we are regridding the variable
    double *zlevs = NULL, *zblevs = NULL; // NULLs correspond to 2D-only regridding
    if ((g.z_len != 0) && (g.zb_len != 0)) {
      ierr = nc.get_vertical_dims(zlevs, zblevs); CHKERRQ(ierr);
    } else {
      ierr = verbPrintf(1, grid.com,
         "PISM ERROR: -i file does not look right; at least one of 'z' and 'zb' is absent in '%s'.\n",
         fname); CHKERRQ(ierr);
      PetscEnd();
    }
    ierr = nc.close(); CHKERRQ(ierr);
    LocalInterpCtx lic(g, zlevs, zblevs, grid);
    ierr = Enth3.regrid(fname, lic, true); CHKERRQ(ierr);  // at this point, it is critical
  } else {
    ierr = verbPrintf(DEBUGVERB, grid.com,
      "  variable 'enthalpy' not found so setting it as cold ice, from temperature ...\n");
      CHKERRQ(ierr);
    ierr = setEnth3FromT3_ColdIce(); CHKERRQ(ierr);
  }

  return 0;
}



/*********** setting fields ****************/

//! Compute Enth3 from temperature T3 by assuming the ice has zero liquid fraction.
PetscErrorCode IceEnthalpyModel::setEnth3FromT3_ColdIce() {
  PetscErrorCode ierr;

  PetscScalar **H;
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = Enth3.begin_access(); CHKERRQ(ierr);
  ierr = vH.get_array(H); CHKERRQ(ierr);

  PetscScalar *Tij, *Enthij; // columns of these values

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      ierr = T3.getInternalColumn(i,j,&Tij); CHKERRQ(ierr);
      ierr = Enth3.getInternalColumn(i,j,&Enthij); CHKERRQ(ierr);
      for (PetscInt k=0; k<grid.Mz; ++k) {
        const PetscScalar depth = H[i][j] - grid.zlevels[k];
        if (depth > 0.0) { // in ice
          Enthij[k] = getEnth(config,Tij[k],0.0,getPressureFromDepth(config,depth));
        } else {
          Enthij[k] = 0.0;  // set enthalpy in air to zero
        }
      }
    }
  }

  ierr = Enth3.end_access(); CHKERRQ(ierr);
  ierr = T3.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);

  ierr = Enth3.beginGhostComm(); CHKERRQ(ierr);
  ierr = Enth3.endGhostComm(); CHKERRQ(ierr);
  return 0;
}


//! Compute the liquid fraction corresponding to Enth3, and put in a global IceModelVec3.
PetscErrorCode IceEnthalpyModel::setLiquidFracFromEnthalpy(IceModelVec3 &useForLiquidFrac) {
  PetscErrorCode ierr;

  ierr = useForLiquidFrac.set_name("liquid_frac"); CHKERRQ(ierr);
  ierr = useForLiquidFrac.set_attrs(
     "diagnostic",
     "liquid water fraction in ice; 0 <= omega <= 1",
     "",
     ""); CHKERRQ(ierr);

  PetscScalar **thickness;
  PetscScalar *omegaij, *Enthij; // columns of these values
  ierr = useForLiquidFrac.begin_access(); CHKERRQ(ierr);
  ierr = Enth3.begin_access(); CHKERRQ(ierr);
  ierr = vH.get_array(thickness); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      ierr = useForLiquidFrac.getInternalColumn(i,j,&omegaij); CHKERRQ(ierr);
      ierr = Enth3.getInternalColumn(i,j,&Enthij); CHKERRQ(ierr);
      for (PetscInt k=0; k<grid.Mz; ++k) {
        const PetscScalar depth = thickness[i][j] - grid.zlevels[k];
        omegaij[k] = getWaterFraction(config, Enthij[k], getPressureFromDepth(config, depth));
      }
    }
  }
  ierr = Enth3.end_access(); CHKERRQ(ierr);
  ierr = useForLiquidFrac.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);

  // communication not done; we allow globals for useForLiquidFrac

  return 0;
}


/*********** velocity routines in which new flow law gets used ****************/

//! Total code duplication with IceModel version, but checks flag doColdIceMethods and uses correct flow law.
PetscErrorCode IceEnthalpyModel::velocitySIAStaggered() {
  PetscErrorCode  ierr;

  PetscScalar *delta, *I, *J, *K, *Sigma;
  delta = new PetscScalar[grid.Mz];
  I = new PetscScalar[grid.Mz];
  J = new PetscScalar[grid.Mz];
  K = new PetscScalar[grid.Mz];
  Sigma = new PetscScalar[grid.Mz];

  PetscScalar **h_x[2], **h_y[2], **H, **uvbar[2];

  PetscScalar *Tij, *Toffset, *ageij, *ageoffset;

  const bool usetau3 = (IceTypeUsesGrainSize(ice) && (realAgeForGrainSize == PETSC_TRUE));

  const PetscTruth usesGrainSize = IceTypeUsesGrainSize(ice);

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = vWork2d[0].get_array(h_x[0]); CHKERRQ(ierr);
  ierr = vWork2d[1].get_array(h_x[1]); CHKERRQ(ierr);
  ierr = vWork2d[2].get_array(h_y[0]); CHKERRQ(ierr);
  ierr = vWork2d[3].get_array(h_y[1]); CHKERRQ(ierr);
  ierr = vuvbar[0].get_array(uvbar[0]); CHKERRQ(ierr);
  ierr = vuvbar[1].get_array(uvbar[1]); CHKERRQ(ierr);

  ierr = T3.begin_access(); CHKERRQ(ierr);
  if (usetau3) {
    ierr = tau3.begin_access(); CHKERRQ(ierr);
  }
  ierr = w3.begin_access(); CHKERRQ(ierr);
  ierr = Istag3[0].begin_access(); CHKERRQ(ierr);
  ierr = Istag3[1].begin_access(); CHKERRQ(ierr);
  ierr = Sigmastag3[0].begin_access(); CHKERRQ(ierr);
  ierr = Sigmastag3[1].begin_access(); CHKERRQ(ierr);

  PetscScalar *Enthij, *Enthoffset;
  PolyThermalGPBLDIce *gpbldi = NULL;
  if (!doColdIceMethods) {
    gpbldi = dynamic_cast<PolyThermalGPBLDIce*>(ice);
    if (gpbldi) {
      gpbldi->config = &config;
    } else {
      PetscPrintf(grid.com,
        "doColdIceMethods==false in IceEnthalpyMethod::velocitySIAStaggered()\n"
        "   but not using PolyThermalGPBLDIce ... ending ....\n");
      PetscEnd();
    }
    ierr = Enth3.begin_access(); CHKERRQ(ierr);
  }

  // staggered grid computation of: I, J, Sigma
  for (PetscInt o=0; o<2; o++) {
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; i++) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; j++) {
        // staggered point: o=0 is i+1/2, o=1 is j+1/2,
        //   (i,j) and (i+oi,j+oj) are reg grid neighbors of staggered pt:
        const PetscInt     oi = 1-o, oj=o;
        const PetscScalar  slope = (o==0) ? h_x[o][i][j] : h_y[o][i][j];
        const PetscScalar  thickness = 0.5 * (H[i][j] + H[i+oi][j+oj]);

        if (thickness > 0) {
          ierr = T3.getInternalColumn(i,j,&Tij); CHKERRQ(ierr);
          ierr = T3.getInternalColumn(i+oi,j+oj,&Toffset); CHKERRQ(ierr);
          if (usetau3) {
            ierr = tau3.getInternalColumn(i,j,&ageij); CHKERRQ(ierr);
            ierr = tau3.getInternalColumn(i+oi,j+oj,&ageoffset); CHKERRQ(ierr);
          }

          if (!doColdIceMethods) {
            ierr = Enth3.getInternalColumn(i,j,&Enthij); CHKERRQ(ierr);
            ierr = Enth3.getInternalColumn(i+oi,j+oj,&Enthoffset); CHKERRQ(ierr);
          }

          // does validity check for thickness:
          const PetscInt      ks = grid.kBelowHeight(thickness);
          const PetscScalar   alpha =
                  sqrt(PetscSqr(h_x[o][i][j]) + PetscSqr(h_y[o][i][j]));

          I[0] = 0;   J[0] = 0;   K[0] = 0;
          for (PetscInt k=0; k<=ks; ++k) {
            const PetscScalar   pressure = ice->rho * earth_grav * (thickness - grid.zlevels[k]);
            PetscScalar flow,grainsize = constantGrainSize;
            if (usesGrainSize && realAgeForGrainSize) {
              grainsize = grainSizeVostok(0.5 * (ageij[k] + ageoffset[k]));
            }
            // If the flow law does not use grain size, it will just ignore it, no harm there
            if (doColdIceMethods) {
              flow = ice->flow(alpha * pressure, 0.5 * (Tij[k] + Toffset[k]), pressure, grainsize);
            } else {
              flow = gpbldi->flowFromEnth(alpha * pressure, 0.5 * (Enthij[k] + Enthoffset[k]), pressure, grainsize);
            }

            delta[k] = 2.0 * pressure * enhancementFactor * flow;

            // for Sigma, ignore mask value and assume SHEET; will be overwritten
            // by correctSigma() in iMssa.cc
            Sigma[k] = delta[k] * PetscSqr(alpha) * pressure;

            if (k>0) { // trapezoid rule for I[k] and K[k]
              const PetscScalar dz = grid.zlevels[k] - grid.zlevels[k-1];
              I[k] = I[k-1] + 0.5 * dz * (delta[k-1] + delta[k]);
              K[k] = K[k-1] + 0.5 * dz * (grid.zlevels[k-1] * delta[k-1]
                                          + grid.zlevels[k] * delta[k]);
              J[k] = grid.zlevels[k] * I[k] - K[k];
            }
          }
          for (PetscInt k=ks+1; k<grid.Mz; ++k) { // above the ice
            Sigma[k] = 0.0;
            I[k] = I[ks];
            J[k] = grid.zlevels[k] * I[ks];
          }

          // diffusivity for deformational flow (vs basal diffusivity, incorporated in ub,vb)
          const PetscScalar  Dfoffset = J[ks] + (thickness - grid.zlevels[ks]) * I[ks];

          // vertically-averaged SIA-only velocity, sans sliding;
          //   note uvbar[0][i][j] is  u  at right staggered point (i+1/2,j)
          //   but uvbar[1][i][j] is  v  at up staggered point (i,j+1/2)
          uvbar[o][i][j] = - Dfoffset * slope / thickness;

          ierr = Istag3[o].setValColumnPL(i,j,grid.Mz,grid.zlevels,I); CHKERRQ(ierr);
          ierr = Sigmastag3[o].setValColumnPL(i,j,grid.Mz,grid.zlevels,Sigma); CHKERRQ(ierr);
        } else {  // zero thickness case
          uvbar[o][i][j] = 0;
          ierr = Istag3[o].setColumn(i,j,0.0); CHKERRQ(ierr);
          ierr = Sigmastag3[o].setColumn(i,j,0.0); CHKERRQ(ierr);
        }
      } // o
    } // j
  } // i

  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vuvbar[0].end_access(); CHKERRQ(ierr);
  ierr = vuvbar[1].end_access(); CHKERRQ(ierr);
  ierr = vWork2d[0].end_access(); CHKERRQ(ierr);
  ierr = vWork2d[1].end_access(); CHKERRQ(ierr);
  ierr = vWork2d[2].end_access(); CHKERRQ(ierr);
  ierr = vWork2d[3].end_access(); CHKERRQ(ierr);

  ierr = T3.end_access(); CHKERRQ(ierr);
  if (usetau3) {
    ierr = tau3.end_access(); CHKERRQ(ierr);
  }
  ierr = w3.end_access(); CHKERRQ(ierr);
  ierr = Sigmastag3[0].end_access(); CHKERRQ(ierr);
  ierr = Sigmastag3[1].end_access(); CHKERRQ(ierr);
  ierr = Istag3[0].end_access(); CHKERRQ(ierr);
  ierr = Istag3[1].end_access(); CHKERRQ(ierr);

  if (!doColdIceMethods) {
    ierr = Enth3.end_access(); CHKERRQ(ierr);
  }

  delete [] delta;   delete [] I;   delete [] J;   delete [] K;   delete [] Sigma;

  return 0;
}


PetscErrorCode IceEnthalpyModel::computeEffectiveViscosity(IceModelVec2 vNuH[2], PetscReal epsilon) {
  PetscErrorCode ierr;

  if (leaveNuHAloneSSA == PETSC_TRUE) {
    return 0;
  }

  //CHECK_NOT_SSA_EXTERNAL(ssa);
  if (ssa) {SETERRQ(1,"This should not be called when the external SSA solver is active");}

  if (useConstantNuHForSSA == PETSC_TRUE) {
    // Intended only for debugging, this treats the entire domain as though it was the strength extension
    // (i.e. strength does not even depend on thickness)
    PetscReal nuH = ssaStrengthExtend.notional_strength();
    ierr = vNuH[0].set(nuH); CHKERRQ(ierr);
    ierr = vNuH[1].set(nuH); CHKERRQ(ierr);
    return 0;
  }

  // We need to compute integrated effective viscosity (\bar\nu * H).
  // It is locally determined by the strain rates and temperature field.
  PetscScalar *Tij, *Toffset, **H, **nuH[2], **u, **v;
  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = T3.begin_access(); CHKERRQ(ierr);
  ierr = vNuH[0].get_array(nuH[0]); CHKERRQ(ierr);
  ierr = vNuH[1].get_array(nuH[1]); CHKERRQ(ierr);

  ierr = vubarSSA.get_array(u); CHKERRQ(ierr);
  ierr = vvbarSSA.get_array(v); CHKERRQ(ierr);

  PetscScalar *Enthij, *Enthoffset;
  PolyThermalGPBLDIce *gpbldi = NULL;
  if (!doColdIceMethods) {
    gpbldi = dynamic_cast<PolyThermalGPBLDIce*>(ice);
    if (gpbldi) {
      gpbldi->config = &config;
    } else {
      PetscPrintf(grid.com,
        "doColdIceMethods==false in IceEnthalpyMethod::computeEffectiveViscosity()\n"
        "   but not using PolyThermalGPBLDIce ... ending ....\n");
      PetscEnd();
    }
    ierr = Enth3.begin_access(); CHKERRQ(ierr);
  }

  for (PetscInt o=0; o<2; ++o) {
    for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
        if (H[i][j] < ssaStrengthExtend.min_thickness_for_extension()) {
          // Extends strength of SSA (i.e. nuH coeff) into the ice free region.  Does not add or subtract ice mass.
          nuH[o][i][j] = ssaStrengthExtend.notional_strength();
        } else {
          const PetscInt      oi = 1-o, oj=o;
          const PetscScalar   dx = grid.dx,
                              dy = grid.dy;
          PetscScalar u_x, u_y, v_x, v_y;
          // Check the offset to determine how to differentiate velocity
          if (o == 0) {
            u_x = (u[i+1][j] - u[i][j]) / dx;
            u_y = (u[i][j+1] + u[i+1][j+1] - u[i][j-1] - u[i+1][j-1]) / (4*dy);
            v_x = (v[i+1][j] - v[i][j]) / dx;
            v_y = (v[i][j+1] + v[i+1][j+1] - v[i][j-1] - v[i+1][j-1]) / (4*dy);
          } else {
            u_x = (u[i+1][j] + u[i+1][j+1] - u[i-1][j] - u[i-1][j+1]) / (4*dx);
            u_y = (u[i][j+1] - u[i][j]) / dy;
            v_x = (v[i+1][j] + v[i+1][j+1] - v[i-1][j] - v[i-1][j+1]) / (4*dx);
            v_y = (v[i][j+1] - v[i][j]) / dy;
          }
          const PetscScalar myH = 0.5 * (H[i][j] + H[i+oi][j+oj]);

          if (doColdIceMethods) {
            ierr = T3.getInternalColumn(i,j,&Tij); CHKERRQ(ierr);
            ierr = T3.getInternalColumn(i+oi,j+oj,&Toffset); CHKERRQ(ierr);
            nuH[o][i][j] = ice->effectiveViscosityColumn(
                                myH, grid.kBelowHeight(myH), grid.zlevels,
                                u_x, u_y, v_x, v_y, Tij, Toffset);
          } else {
            ierr = Enth3.getInternalColumn(i,j,&Enthij); CHKERRQ(ierr);
            ierr = Enth3.getInternalColumn(i+oi,j+oj,&Enthoffset); CHKERRQ(ierr);
            nuH[o][i][j] = gpbldi->effectiveViscosityColumnFromEnth(
                                myH, grid.kBelowHeight(myH), grid.zlevels,
                                u_x, u_y, v_x, v_y, Enthij, Enthoffset);
          }

          if (! finite(nuH[o][i][j]) || false) {
            ierr = PetscPrintf(grid.com, "nuH[%d][%d][%d] = %e\n", o, i, j, nuH[o][i][j]);
              CHKERRQ(ierr);
            ierr = PetscPrintf(grid.com, "  u_x, u_y, v_x, v_y = %e, %e, %e, %e\n",
                               u_x, u_y, v_x, v_y);
              CHKERRQ(ierr);
          }

          // We ensure that nuH is bounded below by a positive constant.
          nuH[o][i][j] += epsilon;
        }
      }
    }
  }
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = T3.end_access(); CHKERRQ(ierr);
  ierr = vNuH[0].end_access(); CHKERRQ(ierr);
  ierr = vNuH[1].end_access(); CHKERRQ(ierr);
  ierr = vubarSSA.end_access(); CHKERRQ(ierr);
  ierr = vvbarSSA.end_access(); CHKERRQ(ierr);

  if (!doColdIceMethods) {
    ierr = Enth3.end_access(); CHKERRQ(ierr);
  }

  // Some communication
  ierr = vNuH[0].beginGhostComm(); CHKERRQ(ierr);
  ierr = vNuH[0].endGhostComm(); CHKERRQ(ierr);
  ierr = vNuH[1].beginGhostComm(); CHKERRQ(ierr);
  ierr = vNuH[1].endGhostComm(); CHKERRQ(ierr);
  return 0;
}


/*********** timestep routines ****************/

PetscErrorCode IceEnthalpyModel::temperatureAgeStep() {
  PetscErrorCode  ierr;

  ierr = verbPrintf(5,grid.com,
    "\n  [IceEnthalpyModel::temperatureAgeStep():  ENTERING; DOING IceModel::temperatureAgeStep() FIRST\n");
    CHKERRQ(ierr);

  ierr = IceModel::temperatureAgeStep(); CHKERRQ(ierr);

  if (doColdIceMethods) {
    ierr = verbPrintf(5,grid.com,
      "   IceEnthalpyModel::temperatureAgeStep(): ENTHALPY IS OFF.  DONE.]\n"); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(5,grid.com,
      "   IceEnthalpyModel::temperatureAgeStep(): ENTHALPY IS ON.  COMMUNICATING ENTHALPY]\n"); CHKERRQ(ierr);

    // start & complete communication
    ierr = Enth3.beginGhostCommTransfer(EnthNew3); CHKERRQ(ierr);
    ierr = Enth3.endGhostCommTransfer(EnthNew3); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode IceEnthalpyModel::temperatureStep(
     PetscScalar* vertSacrCount, PetscScalar* bulgeCount) {
  PetscErrorCode ierr;
  if (doColdIceMethods) {
    ierr = verbPrintf(DEBUGVERB,grid.com,
      "    [IceEnthalpyModel::temperatureStep(): ENTHALPY IS OFF. CALLING IceModel::temperatureStep()]\n");
      CHKERRQ(ierr);
    ierr = IceModel::temperatureStep(vertSacrCount,bulgeCount);  CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(DEBUGVERB,grid.com,
      "    [IceEnthalpyModel::temperatureStep(): ENTHALPY IS ON. CALLING IceEnthalpyModel::enthalpyStep()]\n");
      CHKERRQ(ierr);
    // new enthalpy values go in EnthNew3; also updates (and communicates) Hmelt
    // enthalpyStep() is in iceEnthalpyModel.cc
    ierr = enthalpyStep(vertSacrCount,bulgeCount);  CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode IceEnthalpyModel::enthalpyStep(PetscScalar* vertSacrCount, PetscScalar* bulgeCount) {

  if (doColdIceMethods) {
    PetscPrintf(grid.com,
      "\n\n    IceEnthalpyModel::enthalpyStep() called but doColdIceMethods==true ... ending\n");
    PetscEnd();
  }

  PetscErrorCode  ierr;

  // set up fine grid in ice and bedrock
  PetscInt    fMz, fMbz;
  PetscScalar fdz, *fzlev, fdzb, *fzblev;
  ierr = grid.getFineEqualVertCounts(fMz,fMbz); CHKERRQ(ierr);
  fzlev = new PetscScalar[fMz];
  fzblev = new PetscScalar[fMbz];
  ierr = grid.getFineEqualVertLevs(fMz,fMbz,fdz,fdzb,fzlev,fzblev); CHKERRQ(ierr);

  ierr = verbPrintf(5,grid.com,
    "\n  [entering enthalpyStep(); fMz = %d, fdz = %5.3f, fMbz = %d, fdzb = %5.3f]",
    fMz, fdz, fMbz, fdzb); CHKERRQ(ierr);

  enthSystemCtx system(fMz,fMbz);
  system.dx              = grid.dx;
  system.dy              = grid.dy;
  system.dtTemp          = dtTempAge; // same time step for temp and age, currently
  system.dzEQ            = fdz;
  system.dzbEQ           = fdzb;
  system.ice_rho         = config.get("ice_density"); // ice->rho;
  system.ice_c           = config.get("ice_specific_heat_capacity"); // ice->c_p;
  system.ice_k           = config.get("ice_thermal_conductivity"); // ice->k;
  system.bed_thermal_rho = config.get("bedrock_thermal_density"); // bed_thermal.rho;
  system.bed_thermal_c   = config.get("bedrock_thermal_specific_heat_capacity"); // bed_thermal.c_p;
  system.bed_thermal_k   = config.get("bedrock_thermal_conductivity"); // bed_thermal.k;

  // space for solution of system; length = fMz + fMbz - 1
  const PetscInt k0 = fMbz - 1;
  PetscScalar *x;
  x = new PetscScalar[fMz + k0];

  // constants needed after solution of system, in insertion phase
  const PetscScalar rho_c_I = system.ice_rho * system.ice_c;
  // const PetscScalar rho_c_br = system.bed_thermal_rho * system.bed_thermal_c,
  //                   rho_c_av = (fdz * rho_c_I + fdzb * rho_c_br) / (fdz + fdzb);

  // this is bulge limit constant in J kg-1; is max amount by which ice
  //   enthalpy can be lower than surface temperature (as an enthalpy);
  //   value is enthalpy change equivalent to change in cold ice temp by 15 K
  const PetscScalar bulgeMaxTemp = 15.0,
                    bulgeMaxEnth = system.ice_c * bulgeMaxTemp;

  PetscScalar *Enthnew, *Tbnew;
  // pointers to values in current column
  system.u     = new PetscScalar[fMz];
  system.v     = new PetscScalar[fMz];
  system.w     = new PetscScalar[fMz];
  system.Sigma = new PetscScalar[fMz];
  system.Enth  = new PetscScalar[fMz];
  system.Tb    = new PetscScalar[fMbz];

  Enthnew      = new PetscScalar[fMz];
  Tbnew        = new PetscScalar[fMbz];

  // system needs access to Enth3 for planeStar()
  system.Enth3 = &Enth3;

  // checks that all needed constants and pointers got set:
  ierr = system.initAllColumns(); CHKERRQ(ierr);
  //ierr = system.view(); CHKERRQ(ierr);
  
  // now get map-plane fields, starting with coupler fields
  PetscScalar  **Ts, **Tshelfbase, **H, **Ghf, **mask, **Hmelt, **Rb,
               **basalMeltRate, **bmr_float;

  IceModelVec2 *pccTs, *pccsbt, *pccsbmf;
  if (atmosPCC != PETSC_NULL) {
    // call sets pccTs to point to IceModelVec2 with current surface temps
    ierr = atmosPCC->updateSurfTempAndProvide(
              grid.year, dtTempAge / secpera, &info_coupler, pccTs);
              CHKERRQ(ierr);
  } else {
    SETERRQ(1,"PISM ERROR: atmosPCC == PETSC_NULL");
  }
  if (oceanPCC != PETSC_NULL) {
    ierr = oceanPCC->updateShelfBaseTempAndProvide(
              grid.year, dt / secpera, &info_coupler, pccsbt);
              CHKERRQ(ierr);
    ierr = oceanPCC->updateShelfBaseMassFluxAndProvide(
              grid.year, dt / secpera, &info_coupler, pccsbmf);
              CHKERRQ(ierr);
  } else {
    SETERRQ(1,"PISM ERROR: oceanPCC == PETSC_NULL");
  }
  ierr = pccTs->get_array(Ts);  CHKERRQ(ierr);
  ierr = pccsbt->get_array(Tshelfbase);  CHKERRQ(ierr);
  ierr = pccsbmf->get_array(bmr_float);  CHKERRQ(ierr);

  ierr = vH.get_array(H); CHKERRQ(ierr);
  ierr = vHmelt.get_array(Hmelt); CHKERRQ(ierr);
  ierr = vbasalMeltRate.get_array(basalMeltRate); CHKERRQ(ierr);
  ierr = vMask.get_array(mask); CHKERRQ(ierr);
  ierr = vRb.get_array(Rb); CHKERRQ(ierr);
  ierr = vGhf.get_array(Ghf); CHKERRQ(ierr);

  // these are accessed a column at a time
  ierr = u3.begin_access(); CHKERRQ(ierr);
  ierr = v3.begin_access(); CHKERRQ(ierr);
  ierr = w3.begin_access(); CHKERRQ(ierr);
  ierr = Sigma3.begin_access(); CHKERRQ(ierr);
  ierr = Enth3.begin_access(); CHKERRQ(ierr);
  ierr = EnthNew3.begin_access(); CHKERRQ(ierr);
  ierr = Tb3.begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {

      // for fine grid; this should *not* be replaced by call to grid.kBelowHeight()
      const PetscInt  ks = static_cast<PetscInt>(floor(H[i][j]/fdz));

      if (k0+ks>0) { // if there are enough points in bedrock&ice to bother ...
        ierr = system.setIndicesThisColumn(i,j,ks); CHKERRQ(ierr);
        ierr = Tb3.getValColumn(i,j,fMbz,fzblev,system.Tb); CHKERRQ(ierr);

        if (grid.vertical_spacing == EQUAL) {
          ierr = u3.getValColumnPL(i,j,fMz,fzlev,system.u); CHKERRQ(ierr);
          ierr = v3.getValColumnPL(i,j,fMz,fzlev,system.v); CHKERRQ(ierr);
          ierr = w3.getValColumnPL(i,j,fMz,fzlev,system.w); CHKERRQ(ierr);
          ierr = Sigma3.getValColumnPL(i,j,fMz,fzlev,system.Sigma); CHKERRQ(ierr);
          ierr = Enth3.getValColumnPL(i,j,fMz,fzlev,system.Enth); CHKERRQ(ierr);
        } else {
          // slower, but right for not-equal spaced
          ierr = u3.getValColumnQUAD(i,j,fMz,fzlev,system.u); CHKERRQ(ierr);
          ierr = v3.getValColumnQUAD(i,j,fMz,fzlev,system.v); CHKERRQ(ierr);
          ierr = w3.getValColumnQUAD(i,j,fMz,fzlev,system.w); CHKERRQ(ierr);
          ierr = Sigma3.getValColumnQUAD(i,j,fMz,fzlev,system.Sigma); CHKERRQ(ierr);
          ierr = Enth3.getValColumnQUAD(i,j,fMz,fzlev,system.Enth); CHKERRQ(ierr);
        }

        // FIXME: following mechanism only make sense for comparing cold ice diffusion
        //   to advection; temperate ice diffusion using moisture transport diffusion
        //   coefficient might give different result
        // SO: go through column and find appropriate lambda for BOMBPROOF
        PetscScalar lambda = 1.0;  // start with centered implicit for more accuracy
        for (PetscInt k = 1; k < ks; k++) {
          const PetscScalar denom = (PetscAbs(system.w[k]) + 0.000001/secpera)
                                      * rho_c_I * fdz;
          lambda = PetscMin(lambda, 2.0 * system.ice_k / denom);
        }
        if (lambda < 1.0)  *vertSacrCount += 1; // count columns with lambda < 1

        // if isMarginal then only do vertical conduction for ice;
        //   will ignore advection and strain heating if isMarginal
        const bool isMarginal = checkThinNeigh(H[i+1][j],H[i+1][j+1],H[i][j+1],H[i-1][j+1],
                                               H[i-1][j],H[i-1][j-1],H[i][j-1],H[i+1][j-1]);

        ierr = system.setSchemeParamsThisColumn(mask[i][j], isMarginal, lambda);
                 CHKERRQ(ierr);

        // set boundary values for tridiagonal system; in this form a conversions to enthalpy
        //   will be done internally by the enthColumnSystem instance (i.e. for Ts, Gh, Tshelfbase, Rb)
        ierr = system.setSurfaceBoundaryValuesThisColumn(Ts[i][j]); CHKERRQ(ierr);
        ierr = system.setBasalBoundaryValuesThisColumn(
                 Ghf[i][j],Tshelfbase[i][j],Rb[i][j]); CHKERRQ(ierr);

        // solve the system for this column: x will contain new enthalpy in ice and temp in bedrock
        ierr = system.solveThisColumn(&x); // no CHKERRQ(ierr) immediately because:
        if (ierr > 0) {
          SETERRQ3(2,
            "Tridiagonal solve failed at (%d,%d) with zero pivot position %d.\n",
            i, j, ierr);
        } else { CHKERRQ(ierr); }
      }

      // insert bedrock solution
      for (PetscInt k=0; k < k0; k++) {
        Tbnew[k] = x[k];  // FIXME: CAREFUL HERE: is system getting temperature in bedrock into x?
      }

      // prepare for melting/refreezing
      PetscScalar Hmeltnew = Hmelt[i][j];

      // insert solution for generic ice segments
      for (PetscInt k=1; k <= ks; k++) {
        Enthnew[k] = x[k0 + k];  // FIXME: CAREFUL HERE: is system getting enthalpy in ice into x?
        // modifies last two arguments, generally:
        ierr = drainageToBaseModelEnth(H[i][j],fzlev[k],fdz,Enthnew[k],Hmeltnew); CHKERRQ(ierr);
      }

      // insert solution for ice/rock interface (or base of ice shelf) segment
      if (ks > 0) {
        Enthnew[0] = x[k0 + 0];  // FIXME: CAREFUL HERE: is system getting enthalpy in ice into x?
        // modifies last two arguments, generally:
        // FIXME: CAREFUL HERE: should we split fdz in two, with one half ice and one half rock or
        //        ocean?
        ierr = drainageToBaseModelEnth(H[i][j],0.0,fdz,Enthnew[0],Hmeltnew); CHKERRQ(ierr);
      } else {
        Hmeltnew = 0.0; // no stored water if no ice present
        // in case of no ice, Enthnew[0] = Enthnew[ks] gets set below, from atmosphere
      }

      // bottom of ice is top of bedrock when grounded, so
      //   T(z=0) at top of bedrock should match enthalpy at z=0;
      //   when floating just match ocean temp provided by PISMOceanCoupler
      if (PismModMask(mask[i][j]) == MASK_FLOATING) { // top of bedrock sees ocean
          Tbnew[k0] = Tshelfbase[i][j];
      } else {
        if (ks > 0) { // grounded ice present
          Tbnew[k0] = getAbsTemp(config, Enthnew[0], getPressureFromDepth(config, H[i][j]) );
        } else {      // no significant ice; top of bedrock sees atmosphere
          Tbnew[k0] = Ts[i][j];
        }
      }

      // transfer column into Tb3; neighboring columns will not reference so no need for communication
      ierr = Tb3.setValColumn(i,j,fMbz,fzblev,Tbnew); CHKERRQ(ierr);

      // top ice level gets surface temperature; assumes water fraction zero at surface
      Enthnew[ks] = getEnth(config, Ts[i][j], 0.0,
                            getPressureFromDepth(config, H[i][j] - fzlev[ks]));

      // now that enthalpy is known in top layer, check for (and correct) any extreme advection bulges
      for (PetscInt k=0; k < ks; k++) {
        if (Enthnew[k] < Enthnew[ks] - bulgeMaxEnth) {
          Enthnew[k] = Enthnew[ks] - bulgeMaxEnth;  bulgeCount++;   }
      }

      // set to zero energy content above ice, in atmosphere
      for (PetscInt k=ks+1; k<fMz; k++) {
        Enthnew[k] = 0.0;
      }

      // transfer column into EnthNew3; communication later
      ierr = EnthNew3.setValColumnPL(i,j,fMz,fzlev,Enthnew); CHKERRQ(ierr);

      // basalMeltRate[][] is rate of mass loss from bottom of ice
      if (PismModMask(mask[i][j]) == MASK_FLOATING) {
        // rate of mass loss at bottom of ice shelf;  can be negative (marine freeze-on)
        basalMeltRate[i][j] = bmr_float[i][j]; // set by PISMOceanCoupler
      } else {
        // rate of change of Hmelt[][];  can be negative (till water freeze-on)
        basalMeltRate[i][j] = (Hmeltnew - Hmelt[i][j]) / dtTempAge;
      }

      if (PismModMask(mask[i][j]) == MASK_FLOATING) {
        // eliminate basal lubrication water if floating; UNACCOUNTED MASS LOSS
        Hmelt[i][j] = 0.0;
      } else {
        // limit Hmelt by default max
        Hmelt[i][j] = PetscMin(Hmelt_max, Hmeltnew);
      }

    }
  }

  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);
  ierr = vHmelt.end_access(); CHKERRQ(ierr);
  ierr = vRb.end_access(); CHKERRQ(ierr);
  ierr = vGhf.end_access(); CHKERRQ(ierr);
  ierr = vbasalMeltRate.end_access(); CHKERRQ(ierr);

  ierr = pccTs->end_access(); CHKERRQ(ierr);
  ierr = pccsbt->end_access();  CHKERRQ(ierr);
  ierr = pccsbmf->end_access();  CHKERRQ(ierr);

  ierr = Tb3.end_access(); CHKERRQ(ierr);
  ierr = u3.end_access(); CHKERRQ(ierr);
  ierr = v3.end_access(); CHKERRQ(ierr);
  ierr = w3.end_access(); CHKERRQ(ierr);
  ierr = Sigma3.end_access(); CHKERRQ(ierr);
  ierr = Enth3.end_access(); CHKERRQ(ierr);
  ierr = EnthNew3.end_access(); CHKERRQ(ierr);


  delete [] system.u;     delete [] system.v;     delete [] system.w;
  delete [] system.Sigma; delete [] system.Enth;  delete [] system.Tb;

  delete [] x;
  delete [] Tbnew;  delete [] Enthnew;
  delete [] fzlev;  delete [] fzblev;

  return 0;
}


//! Move some of the liquid water fraction in a column segment [z,z+dz] to the base according to heuristics.
/*!
Once liquid water fraction exceeds a cap, all of it goes to the base.  Drainage model
follows \ref Greve97Greenland and references therein.

If the basal ice is cold and there is available water (Hmelt > 0.0) then ice will freeze on,
causing a negative basal melt rate which can enter into the mass continuity equation,
and we bring the lowest ice layer (basal ice) up to temperate.
 */
PetscErrorCode IceEnthalpyModel::drainageToBaseModelEnth(
                const PetscScalar thickness, const PetscScalar z, const PetscScalar dz,
                PetscScalar &enthalpy, PetscScalar &Hmelt) {

  if (allowAboveMelting == PETSC_TRUE) {
    SETERRQ(1,"drainageToBaseModelEnth() called but allowAboveMelting==TRUE");
  }

  if (updateHmelt == PETSC_FALSE)  return 0;

  const PetscScalar p     = getPressureFromDepth(config, thickness - z),
                    omega = getWaterFraction(config, enthalpy, p);

  if (omega >= 0.0) {
    const PetscScalar abovecap = omega - config.get("liquid_water_fraction_max");
    if (abovecap > 0.0) {
      enthalpy -= abovecap * config.get("water_latent_heat_fusion");
      Hmelt    += abovecap * dz;   // ice-equivalent water thickness change
    }
  } else if ((z >= -0.00001) && (z <= 0.00001)) {
    // only consider freeze-on if column segment is at base of ice
    PetscScalar T_m, H_l, H_s;
    getEnthalpyInterval(config, p, T_m, H_l, H_s);
    const PetscScalar dEnth_to_reach_temperate = H_s - enthalpy;
    if (dEnth_to_reach_temperate > 0.0) {
      // if below H_s, then freeze on, and bring up enthalpy to H_s if enough water is available
      const PetscScalar L               = config.get("water_latent_heat_fusion"),
                        dEnth_available = (Hmelt / dz) * L, // = ((rho Hmelt dx dy) * L) / (rho dx dy dz)
                        dEnth_added     = PetscMin(dEnth_available, dEnth_to_reach_temperate);
      enthalpy += dEnth_added;
      Hmelt    -= (dEnth_added * dz) / L;
    }
  }

  return 0;
}

