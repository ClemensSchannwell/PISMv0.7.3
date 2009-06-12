#!/usr/bin/env python

from netCDF3 import *

input = "../lib/pism_config.nc"

nc = Dataset(input, 'r')

var = nc.variables['pism_config']

print """
/*!
\page config Configuration flags and parameters

\par Notes
- These flags and parameters are from pism_config.cdl, which is
  converted to lib/pism_config.nc in the build process.
- An alternate config .nc file can be specified by option "-config foo.nc".
  The alternate file must generally contain values for all of the flags and parameters,
- Values are asked-for when needed, so if there is no request for it then a flag or
  parameter could be missing and things could still run.
- Valid boolean flag values are "yes", "true", "on" and "no",
  "false", "off" (lowercase only).  They have to be enclosed in quotes
  in pism_config.cdl.
"""

print """
\section flags Boolean flags
<table style="width: 100%">
<tr> <td> <b> Flag name </b> </td> <td> <b> Default value </b> </td> <td> <b> Description </b> </td> </tr>"""

for attr in var.ncattrs():
    if attr.endswith("_doc"):
        continue

    value = getattr(var, attr)
    try:
      docstring = getattr(var, attr + "_doc", "[missing]")
    except:
      docstring = "[missing]"

    if type(value) != str:
        continue

    print "<tr><td>%s</td><td>\"%s\"</td><td>%s</td></tr>" % (attr, value, docstring)

print "</table>"

print """
\section params Scalar parameters
<table style="width: 100%">
<tr> <td> <b> Flag name </b> </td> <td> <b> Default value </b> </td> <td> <b> Description </b> </td> </tr>"""

for attr in var.ncattrs():
    if attr.endswith("_doc"):
        continue

    value = getattr(var, attr)
    try:
      docstring = getattr(var, attr + "_doc", "[missing]")
    except:
      docstring = "[missing]"

    if type(value) == str:
        continue

    print "<tr><td>%s</td>" % attr
    
    if (abs(value) >= 1e7):
        print "<td>%e</td>" % value, # use engineering notation if a number is big
    elif (int(value) == value):
        print "<td>%d</td>" % int(value), # remove zeros after the decimal point
    elif (abs(value) <= 1e-5):
        print "<td>%e</td>" % value, # use engineering notation if small (and not zero; prev case)
    else:
        print "<td>%f</td>" % value,
    
    print "<td>%s</td></tr>" % docstring

print "</table> */"
