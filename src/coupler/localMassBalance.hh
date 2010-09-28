// Copyright (C) 2009, 2010 Ed Bueler and Constantine Khroulev
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

#ifndef __localMassBalance_hh
#define __localMassBalance_hh

#include <petsc.h>
#include <gsl/gsl_rng.h>
#include "../base/NCVariable.hh"

//! Base class for a model which computes surface mass flux rate (ice thickness per time) from a precipitation (scalar) and a time series for temperature.
/*!
This is a process model.  It uses a 1D array, with a time dimension, for snow 
temperatures.  This process model does not know its location on the ice sheet, but
simply computes the surface mass balance from three quantities
  - the time interval \f$[t,t+(N-1)\Delta t]\f$,
  - time series of \f$N\f$ values of temperature in the snow at equally-spaced times
    \f$t,t+\Delta t,\dots,t+(N-1)\Delta t]\f$, and
  - a scalar precipation rate which is taken to apply in the whole time interval.

FIXME:  This base class should be more general.  For instance, to allow as
input a time series for precipation rate.  Furthermore it implicitly implies
a temperature index model (i.e. from temperature and precipitation we get surface
mass balance), which is too inflexible.

\note Please avoid using config.get("...") and config.get_flag("...") calls in
methods to reduce computational costs. (Looking up configuration flags and
parameters in constructors is OK.)
 */
class LocalMassBalance {
public:
  LocalMassBalance(const NCConfigVariable &myconfig)
    : config(myconfig) {}
  virtual ~LocalMassBalance() {}
  virtual PetscErrorCode init() = 0;

  /*! Call before getMassFluxFromTemperatureTimeSeries() so that mass balance method can
      decide how to cut up the time interval.  Most implementations will ignore
      t and just use dt.  Input t,dt in seconds.  */
  virtual PetscErrorCode getNForTemperatureSeries(
                PetscScalar t, PetscScalar dt, PetscInt &N) = 0;

  /*! Inputs T[0],...,T[N-1] are temperatures (K) at times t, t+dt, ..., t+(N-1)dt 
      Input t,dt in seconds.  Input precip_rate, and returned surface mass balance (smb) , are in 
      ice-equivalent thickness per time (m s-1).  Input precip is (ice-equivalent)
      snow at low temperatures and becomes rain at higher; the rain is "thrown
      away" and does not add to surface balance.  If input precip_rate is negative
      then it is treated directly as ablation and positive degree days are ignored.  */
  virtual PetscErrorCode getMassFluxFromTemperatureTimeSeries(PetscScalar t, PetscScalar dt_series,
                                                              PetscScalar *T, PetscInt N,
                                                              PetscScalar precip_rate,
                                                              PetscScalar &accumulation_rate,
                                                              PetscScalar &melt_rate,
                                                              PetscScalar &runoff_rate,
                                                              PetscScalar &smb) = 0;

protected:
  const NCConfigVariable& config;
};


//! A PDD implementation which computes the local mass balance based on an expectation integral.
/*!
The expected number of positive degree days is computed by an integral in \ref CalovGreve05 .
 */
class PDDMassBalance : public LocalMassBalance {

public:
  PDDMassBalance(const NCConfigVariable& myconfig);
  virtual ~PDDMassBalance() {}

  virtual PetscErrorCode init();

  virtual PetscErrorCode getNForTemperatureSeries(
             PetscScalar t, PetscScalar dt, PetscInt &N);

  //! Formula (6) in [\ref Faustoetal2009] requires knowledge of latitude and mean July temp.
  virtual PetscErrorCode setDegreeDayFactorsFromSpecialInfo(
             PetscScalar latitude, PetscScalar T_mj);

  virtual PetscErrorCode getMassFluxFromTemperatureTimeSeries(PetscScalar t, PetscScalar dt_series,
                                                              PetscScalar *T, PetscInt N,
                                                              PetscScalar precip_rate,
                                                              PetscScalar &accumulation_rate,
                                                              PetscScalar &melt_rate,
                                                              PetscScalar &runoff_rate,
                                                              PetscScalar &smb);
protected:
  /*! Return value is number of positive degree days (units: K day)  */
  virtual PetscScalar getPDDSumFromTemperatureTimeSeries(
                 PetscScalar t, PetscScalar dt_series, PetscScalar *T, PetscInt N);
  PetscScalar CalovGreveIntegrand(PetscScalar sigma, PetscScalar Tac);

  PetscScalar beta_ice_w, beta_snow_w, T_c, T_w, beta_ice_c, beta_snow_c,
    fresh_water_density, ice_density, pdd_fausto_latitude_beta_w;


  PetscScalar  pddStdDev,        // K; daily amount of randomness
               pddFactorSnow,    // m day^-1 K^-1; amount of snow melted,
                                 //    as ice equivalent, per positive degree day
               pddFactorIce,     // m day^-1 K^-1; amount of ice melted
                                 //    per positive degree day
               pddRefreezeFrac;  // [pure fraction]; amount of melted snow which refreezes
                                 //    as ice

  bool precip_as_snow;          //!< interpret all the precipitation as snow (no rain)
  PetscScalar Tmin,             //!< the temperature below which all precipitation is snow
    Tmax;                       //!< the temperature above which all precipitation is rain
};


//! An alternative PDD implementation which computes the local mass balance based on simulating a random process to get the number of PDDs.
/*!
Uses a GSL random number generator.  Significantly slower because new random numbers are
generated for each grid point.

The way the number of positive degree-days are used to produce a surface mass balance
is identical to the more basic class PDDMassBalance.

A more realistic pattern for the variability of surface melting might have correlation 
with appropriate spatial and temporal ranges, but this can not be easily implemented in this
framework because the model uses only local information.
 */
class PDDrandMassBalance : public PDDMassBalance {

public:
  PDDrandMassBalance(const NCConfigVariable& myconfig, bool repeatable); //! repeatable==true to seed with zero every time.
  virtual ~PDDrandMassBalance();

  virtual PetscErrorCode getNForTemperatureSeries(
                PetscScalar t, PetscScalar dt, PetscInt &N);

protected:
  virtual PetscScalar getPDDSumFromTemperatureTimeSeries(
                 PetscScalar t, PetscScalar dt_series, PetscScalar *T, PetscInt N);
  gsl_rng     *pddRandGen;
};

#endif
