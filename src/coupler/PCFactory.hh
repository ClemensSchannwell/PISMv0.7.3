#include "PISMAtmosphere.hh"
#include "PISMSurface.hh"
#include "PISMOcean.hh"
#include <map>

template <class Model, class Modifier>
class PCFactory {
public:
  PCFactory<Model,Modifier>(IceGrid& g, const NCConfigVariable& conf)
  : grid(g), config(conf) {}
  virtual ~PCFactory<Model,Modifier>() {}

  //! Sets the default type name.
  virtual PetscErrorCode set_default(string name) {
    void (*func) (IceGrid&, const NCConfigVariable&, Model*&);

    func = models[name];
    if (!func) {
      SETERRQ1(1,"ERROR: type %s is not registered", name.c_str());
    } else {
      default_type = name;
    }
    return 0;
  }

  //! Creates a boundary model. Processes command-line options.
  virtual PetscErrorCode create(Model* &result) {
    void (*F) (IceGrid&, const NCConfigVariable&, Model*&);
    PetscErrorCode ierr;
    vector<string> choices;
    string model_list, modifier_list, descr;
    bool flag = false;

    // build a list of available models:
    typename map<string,void(*)(IceGrid&, const NCConfigVariable&, Model*&)>::iterator k;
    k = models.begin();
    model_list = "[" + (k++)->first;
    for(; k != models.end(); k++) {
      model_list += ", " + k->first;
    }
    model_list += "]";

    // build a list of available modifiers:
    typename map<string,void(*)(IceGrid&, const NCConfigVariable&, Modifier*&)>::iterator p;
    p = modifiers.begin();
    modifier_list = "[" + (p++)->first;
    for(; p != modifiers.end(); p++) {
      modifier_list += ", " + p->first;
    }
    modifier_list += "]";

    descr =  "Sets up the PISM " + option + " model. Available models: " + model_list +
      " Available modifiers: " + modifier_list;
  
    // Get the command-line option:
    ierr = PISMOptionsStrings("-" + option, descr, default_type.c_str(), choices, flag); CHKERRQ(ierr);

    if (choices.empty()) {
      if (flag) {
	PetscPrintf(grid.com, "ERROR: option -%s requires an argument.\n", option.c_str());
	PetscEnd();
      }
      choices.push_back(default_type);
    }

    // the first element has to be an *actual* model (not a modifier), so we
    // create it:
    vector<string>::iterator j = choices.begin();
  
    F = models[*j];
    if (!F) {
      PetscPrintf(grid.com, "ERROR: %s model \"%s\" is not available.\n",
		  option.c_str(), j->c_str());
      PetscEnd();
    }

    (*F)(grid, config, result);

    ++j;

    // process remaining arguments:
    while (j != choices.end()) {
      void (*M) (IceGrid&, const NCConfigVariable&, Modifier*&);
      Modifier *mod;

      M = modifiers[*j];
      if (!M) {
	PetscPrintf(grid.com, "ERROR: %s modifier \"%s\" is not available.\n",
		    option.c_str(), j->c_str());
	PetscEnd();
      }

      (*M)(grid, config, mod);

      mod->attach_input(result);
      result = mod;

      ++j;
    }

    return 0;
  }

  //! Adds a boundary model to the dictionary.
  virtual void add_model(string name, void(*func)(IceGrid&, const NCConfigVariable&, Model*&)) {
    models[name] = func;
  }

  virtual void add_modifier(string name, void(*func)(IceGrid&, const NCConfigVariable&, Modifier*&)) {
    modifiers[name] = func;
  }

  //! Removes a boundary model from the dictionary.
  virtual void remove_model(string name) {
    models.erase(name);
  }

  virtual void remove_modifier(string name) {
    modifiers.erase(name);
  }

  //! Clears the dictionary.
  virtual void clear_models() {
    models.clear();
  }

  virtual void clear_modifiers() {
    modifiers.clear();
  }
protected:
  virtual void add_standard_types() {}
  string default_type, option;
  map<string,void(*)(IceGrid&, const NCConfigVariable&, Model*&)> models;
  map<string,void(*)(IceGrid&, const NCConfigVariable&, Modifier*&)> modifiers;
  IceGrid& grid;
  const NCConfigVariable& config;
};

class PAFactory : public PCFactory<PISMAtmosphereModel,PAModifier> {
public:
  PAFactory(IceGrid& g, const NCConfigVariable& conf)
    : PCFactory<PISMAtmosphereModel,PAModifier>(g, conf)
  {
    add_standard_types();
    option = "atmosphere";
  }
  virtual ~PAFactory() {}
  virtual void add_standard_types();
};

class PSFactory : public PCFactory<PISMSurfaceModel,PSModifier> {
public:
  PSFactory(IceGrid& g, const NCConfigVariable& conf)
    : PCFactory<PISMSurfaceModel,PSModifier>(g, conf)
  {
    add_standard_types();
    option = "surface";
  }

  virtual ~PSFactory() {}
  virtual void add_standard_types();
};

class POFactory : public PCFactory<PISMOceanModel,POModifier> {
public:
  POFactory(IceGrid& g, const NCConfigVariable& conf)
    : PCFactory<PISMOceanModel,POModifier>(g, conf)
  {
    add_standard_types();
    option = "ocean";
  }
  virtual ~POFactory() {}
  virtual void add_standard_types();
};
