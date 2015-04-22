// Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include <cassert>
#include <stdexcept>

#include "flowlaw_factory.hh"
#include "base/util/pism_const.hh"
#include "base/util/pism_options.hh"
#include "base/util/PISMUnits.hh"
#include "base/util/error_handling.hh"

namespace pism {
namespace rheology {

FlowLawFactory::FlowLawFactory(const std::string &pre,
                               const Config &conf,
                               const EnthalpyConverter *my_EC)
  : config(conf), EC(my_EC) {

  prefix = pre;

  assert(prefix.empty() == false);

  registerAll();

  setType(ICE_PB);    
}

FlowLawFactory::~FlowLawFactory()
{
}

void FlowLawFactory::registerType(const std::string &name, FlowLawCreator icreate)
{
  flow_laws[name] = icreate;
}

void FlowLawFactory::removeType(const std::string &name) {
  flow_laws.erase(name);
}


FlowLaw* create_isothermal_glen(const std::string &pre,
                                const Config &config, const EnthalpyConverter *EC) {
  return new (IsothermalGlen)(pre, config, EC);
}

FlowLaw* create_pb(const std::string &pre,
                   const Config &config, const EnthalpyConverter *EC) {
  return new (PatersonBudd)(pre, config, EC);
}

FlowLaw* create_gpbld(const std::string &pre,
                      const Config &config, const EnthalpyConverter *EC) {
  return new (GPBLD)(pre, config, EC);
}

FlowLaw* create_hooke(const std::string &pre,
                      const Config &config, const EnthalpyConverter *EC) {
  return new (Hooke)(pre, config, EC);
}

FlowLaw* create_arr(const std::string &pre,
                    const Config &config, const EnthalpyConverter *EC) {
  return new (PatersonBuddCold)(pre, config, EC);
}

FlowLaw* create_arrwarm(const std::string &pre,
                        const Config &config, const EnthalpyConverter *EC) {
  return new (PatersonBuddWarm)(pre, config, EC);
}

FlowLaw* create_goldsby_kohlstedt(const std::string &pre,
                                  const Config &config, const EnthalpyConverter *EC) {
  return new (GoldsbyKohlstedt)(pre, config, EC);
}

void FlowLawFactory::registerAll()
{
  flow_laws.clear();
  registerType(ICE_ISOTHERMAL_GLEN, &create_isothermal_glen);
  registerType(ICE_PB, &create_pb);
  registerType(ICE_GPBLD, &create_gpbld);
  registerType(ICE_HOOKE, &create_hooke);
  registerType(ICE_ARR, &create_arr);
  registerType(ICE_ARRWARM, &create_arrwarm);
  registerType(ICE_GOLDSBY_KOHLSTEDT, &create_goldsby_kohlstedt);

}

void FlowLawFactory::setType(const std::string &type)
{
  FlowLawCreator r = flow_laws[type];
  if (not r) {
    throw RuntimeError::formatted("Selected ice type \"%s\" is not available.\n",
                                  type.c_str());
  }

  type_name = type;

}

void FlowLawFactory::setFromOptions()
{
  {
    // build the list of choices
    std::map<std::string,FlowLawCreator>::iterator j = flow_laws.begin();
    std::vector<std::string> choices;
    while (j != flow_laws.end()) {
      choices.push_back(j->first);
      ++j;
    }

    options::Keyword type("-" + prefix + "flow_law", "flow law type",
                          join(choices, ","), type_name);

    if (type.is_set()) {
      setType(type);
    }
  }
}

FlowLaw* FlowLawFactory::create()
{
  // find the function that can create selected ice type:
  FlowLawCreator r = flow_laws[type_name];
  if (r == NULL) {
    throw RuntimeError::formatted("Selected ice type %s is not available,\n"
                                  "but we shouldn't be able to get here anyway",
                                  type_name.c_str());
  }

  // create an FlowLaw instance:
  return (*r)(prefix, config, EC);
}

} // end of namespace rheology
} // end of namespace pism
