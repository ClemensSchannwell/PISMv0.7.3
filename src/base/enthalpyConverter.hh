// Copyright (C) 2009-2011, 2013, 2014, 2015 Andreas Aschwanden, Ed Bueler and Constantine Khroulev
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

#ifndef __enthalpyConverter_hh
#define __enthalpyConverter_hh

namespace pism {

class Config;

//! Converts between specific enthalpy and temperature or liquid content.
/*!
  Use this way, for example within IceModel with Config config member:
  \code
  #include "enthalpyConverter.hh"

  EnthalpyConverter EC(&config);  // runs constructor; do after initialization of Config config
  ...
  for (...) {
  ...
  E_s = EC.getEnthalpyCTS(p);
  ... etc ...
  }   
  \endcode

  The three methods that get the enthalpy from temperatures and liquid fractions, 
  namely getEnth(), getEnthPermissive(), getEnthAtWaterFraction(), are more strict
  about error checking.  They throw RuntimeError if their arguments are invalid.

  This class is documented by [\ref AschwandenBuelerKhroulevBlatter].
*/
class EnthalpyConverter {
public:
  EnthalpyConverter(const Config &config);
  virtual ~EnthalpyConverter() {}

  virtual double getPressureFromDepth(double depth) const;
  virtual double getMeltingTemp(double p) const;
  virtual double getEnthalpyCTS(double p) const;
  virtual void getEnthalpyInterval(double p, double &E_s, double &E_l) const;
  virtual double getCTS(double E, double p) const;

  virtual bool isTemperate(double E, double p) const;
  virtual bool isLiquified(double E, double p) const;

  virtual double getAbsTemp(double E, double p) const;
  virtual double getPATemp(double E, double p) const;

  virtual double getWaterFraction(double E, double p) const;

  virtual double getEnth(double T, double omega, double p) const;
  virtual double getEnthPermissive(double T, double omega, double p) const;
  virtual double getEnthAtWaterFraction(double omega, double p) const;

  virtual double c_from_T(double /*T*/) const {
    return c_i;
  }

protected:
  double T_melting, L, c_i, rho_i, g, p_air, beta, T_tol;
  double T_0;
  bool   do_cold_ice_methods;
};


//! An EnthalpyConverter for use in verification tests.
/*!
  Treats ice at any temperature as cold (= zero liquid fraction).  Makes absolute
  temperature (in K) and enthalpy proportional:  \f$E = c_i (T - T_0)\f$. 

  The pressure dependence of the pressure-melting temperature is neglected.

  Note: Any instance of FlowLaw uses an EnthalpyConverter; this is
  the one used in verification mode.
*/
class ICMEnthalpyConverter : public EnthalpyConverter {
public:
  ICMEnthalpyConverter(const Config &config) : EnthalpyConverter(config) {
    do_cold_ice_methods = true;
  }

  virtual ~ICMEnthalpyConverter() {}

  /*! */
  virtual double getMeltingTemp(double /*p*/) const {
    return T_melting;
  }

  /*! */
  virtual double getAbsTemp(double E, double /*p*/) const {
    return (E / c_i) + T_0;
  }

  /*! */
  virtual double getWaterFraction(double /*E*/, double /*p*/) const {
    return 0.0;
  }

  /*! */
  virtual double getEnth(double T, double /*omega*/, double /*p*/) const {
    return c_i * (T - T_0);
  }

  /*! */
  virtual double getEnthPermissive(double T, double /*omega*/, double /*p*/) const {
    return c_i * (T - T_0);
  }

  /*! */
  virtual double getEnthAtWaterFraction(double /*omega*/, double p) const {
    return getEnthalpyCTS(p);
  }

  /*! */
  virtual bool isTemperate(double /*E*/, double /*p*/) const {
    return false;
  }
};

} // end of namespace pism

#endif // __enthalpyConverter_hh

