#!/usr/bin/env python
from sys import argv, exit
from getopt import getopt, GetoptError

## @package climmask
## Uses ice thickness found in file to mask-out ice-free areas for identified
## variables.
##
## Option \c -v selects variable or variables (in comma-separated list):
## \code
## climmask.py -v acab,smelt foo.nc
## \endcode
## will mask-out variable smelt in file bar.nc, in areas where thk < 1.0 m.
## 
## This script is a modification of util/nccmp.py.
## 
## Run with --help to get a "usage" message.

usage="""climmask.py uses ice thickness to mask-out ice-free areas
usage:
  climmask.py -v acab,smelt foo.nc    # mask-out variables acab"""

def usagefailure(message):
    print message
    print
    print usage
    exit(2)

def maskout(nc, thk, name, thktol=1.0):
    from numpy import squeeze, isnan, ma

    try:
        var = squeeze(nc.variables[name][:])
    except:
        usagefailure("ERROR: VARIABLE '%s' NOT FOUND IN FILE" % name)

    var[thk<thktol] = -9999.0
    return var

if __name__ == "__main__":
    try:
      opts, args = getopt(argv[1:], "v:", ["help","usage"])
    except GetoptError:
      usagefailure('ERROR: INCORRECT COMMAND LINE ARGUMENTS FOR climmask.py')
    dovars = []
    for (opt, arg) in opts:
        if opt == "-v":
            dovars = arg.split(",")
        if opt in ("--help", "--usage"):
            print usage
            exit(0)

    if len(args) != 1:
      usagefailure('ERROR: WRONG NUMBER OF ARGUMENTS FOR climmask.py')

    try:
      from netCDF4 import Dataset as NC
    except:
      from netCDF3 import Dataset as NC

    try:
      nc = NC(args[0], 'a')
    except:
      usagefailure("ERROR: FILE '%s' CANNOT BE OPENED FOR READING" % args[0])

    from numpy import squeeze

    try:
      thk = squeeze(nc.variables["thk"][:])
    except:
      usagefailure("ERROR: ICE THICKNESS VARIABLE 'thk' NOT FOUND IN FILE %s" % args[0])

    for varname in dovars:
      print "  masking out variable '%s' ..." % varname
      var = nc.variables[varname]
      var[:] = maskout(nc, thk, varname)

    nc.close()

