// Copyright (C) 2011, 2012, 2013, 2014 PISM Authors
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

#ifndef _PISM_OPTIONS_H_
#define _PISM_OPTIONS_H_

#include "pism_const.hh"

#include "options.hh"

namespace pism {

class Config;

namespace options {

typedef enum {ALLOW_EMPTY, DONT_ALLOW_EMPTY} ArgumentFlag;

class String : public Option<std::string> {
public:
  // there is no reasonable default; if the option is set, it has to
  // have a non-empty argument
  String(const std::string& option,
         const std::string& description);
  // there is a reasonable default
  String(const std::string& option,
         const std::string& description,
         const std::string& default_value,
         ArgumentFlag flag = DONT_ALLOW_EMPTY);
  const char* c_str();
private:
  void process(const std::string& option,
               const std::string& description,
               const std::string& default_value,
               ArgumentFlag flag);
};

class StringList : public Option<std::vector<std::string> > {
public:
  StringList(const std::string& option,
             const std::string& description,
             const std::string& default_value);
  std::string to_string();
};

class StringSet : public Option<std::set<std::string> > {
public:
  StringSet(const std::string& option,
            const std::string& description,
            const std::string& default_value);
  std::string to_string();
};

class Keyword : public Option<std::string> {
public:
  Keyword(const std::string& option,
          const std::string& description,
          const std::string& choices,
          const std::string& default_value);
  const char* c_str();
};

class Integer : public Option<int> {
public:
  Integer(const std::string& option,
          const std::string& description,
          int default_value);
};

class IntegerList : public Option<std::vector<int> > {
public:
  IntegerList(const std::string& option,
              const std::string& description);
};


class Real : public Option<double> {
public:
  Real(const std::string& option,
       const std::string& description,
       double default_value);
};

class RealList : public Option<std::vector<double> > {
public:
  RealList(const std::string& option,
           const std::string& description);
};

bool Bool(const std::string& option,
          const std::string& description);

} // end of namespace options

PetscErrorCode verbosityLevelFromOptions();

// handy functions for processing options:
PetscErrorCode OptionsList(std::string opt, std::string text, std::set<std::string> choices,
                           std::string default_value, std::string &result, bool &flag);

PetscErrorCode OptionsString(std::string option, std::string text,
                             std::string &result, bool &flag, bool allow_empty_arg = false);
PetscErrorCode OptionsStringArray(std::string opt, std::string text, std::string default_value,
                                      std::vector<std::string>& result, bool &flag);
PetscErrorCode OptionsStringSet(std::string opt, std::string text, std::string default_value,
                                std::set<std::string>& result, bool &flag);

PetscErrorCode OptionsInt(std::string option, std::string text,
                              int &result, bool &is_set);
PetscErrorCode OptionsIntArray(std::string option, std::string text,
                                   std::vector<int> &result, bool &is_set);

PetscErrorCode OptionsReal(std::string option, std::string text,
                               double &result, bool &is_set);
PetscErrorCode OptionsRealArray(std::string option, std::string text,
                                    std::vector<double> &result, bool &is_set);

bool OptionsIsSet(std::string option);
bool OptionsIsSet(std::string option, std::string descr);

PetscErrorCode OptionsHasArgument(std::string option, bool &result);

PetscErrorCode ignore_option(MPI_Comm com, std::string name);
PetscErrorCode check_old_option_and_stop(std::string old_name, std::string new_name);
PetscErrorCode stop_if_set(std::string name);

// usage message and required options; drivers use these
PetscErrorCode stop_on_version_option();

PetscErrorCode show_usage_and_quit(MPI_Comm com, std::string execname, std::string usage);

PetscErrorCode show_usage_check_req_opts(MPI_Comm com, std::string execname,
                                         std::vector<std::string> required_options,
                                         std::string usage);

// config file initialization:
PetscErrorCode init_config(MPI_Comm com,
                           Config &config, Config &overrides,
                           bool process_options = false);

PetscErrorCode set_config_from_options(Config &config);


} // end of namespace pism

#endif /* _PISM_OPTIONS_H_ */
