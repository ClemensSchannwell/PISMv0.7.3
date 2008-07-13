#! /usr/bin/env python

import sys
import getopt
import time
from numpy import *
from pycdf import *

# set constants
SECPERA = 3.1556926e7
IN_FILE = 'riggs_clean.dat'
OUT_FILE = 'riggs.nc'

##### command line arguments #####
try:
  opts, args = getopt.getopt(sys.argv[1:], "p:o:", ["prefix=", "out="])
  for opt, arg in opts:
    if opt in ("-p", "--prefix"):
      IN_FILE = arg + IN_FILE
    if opt in ("-o", "--out"):
      OUT_FILE = arg
except getopt.GetoptError:
  print 'Incorrect command line arguments'
  sys.exit(2)
  
##### read riggs_clean.dat #####
print "reading RIGGS measurement data from ",IN_FILE
riggsin=open(IN_FILE, 'r')
numlines = 148
riggscount = zeros((numlines,),int16)
truelat = zeros((numlines,),float32)
truelon = zeros((numlines,),float32)
riggslat = zeros((numlines,),float32)
riggslon = zeros((numlines,),float32)
riggsmag = zeros((numlines,),float32)
riggsazi = zeros((numlines,),float32)
riggsu = zeros((numlines,),float32)
riggsv = zeros((numlines,),float32)
# = zeros((numlines,),float32)
count = 0
while 1:
  line = riggsin.readline().split()
  if len(line) != 14:
    #print "line " + str(count) + " length not equal to 14"
    break
  riggscount[count] = int(line[0])
  truelat[count] = float(line[1])
  truelon[count] = float(line[2])
  #RIGGSlat = -(RIGGS(:,4) + RIGGS(:,5)/60 + RIGGS(:,6)/(60*60));
  #RIGGSlon = RIGGS(:,7) + RIGGS(:,8)/60 + RIGGS(:,9)/(60*60);
  #RIGGSlon = - RIGGSlon .* RIGGS(:,10);  % RIGGS(:,10) is +1 if W, -1 if E
  riggslat[count] = - ( float(line[3]) + float(line[4])/60.0 + float(line[5])/3600.0 )
  riggslon[count] = float(line[6]) + float(line[7])/60.0 + float(line[8])/3600.0
  riggslon[count] = - float(line[9]) * riggslon[count]
  #rigu = sin((pi/180)*rig(:,13)) .* rig(:,11);
  #rigv = cos((pi/180)*rig(:,13)) .* rig(:,11);
  #ignor line[11] and line[13]
  riggsmag[count] = float(line[10])
  riggsazi[count] = float(line[12])
  riggsu[count] = riggsmag[count] * sin( (pi/180.0) * riggsazi[count] )
  riggsv[count] = riggsmag[count] * cos( (pi/180.0) * riggsazi[count] )
  count = count + 1
print "lines read from " + IN_FILE + "= " + str(count)
riggsin.close()

##### create and define dimensions and variables in NetCDF file #####
ncfile = CDF(OUT_FILE, NC.WRITE|NC.CREATE|NC.TRUNC)
ncfile.automode()
# define the dimensions
xdim = ncfile.def_dim('count', count)
# define the variables
xvar = ncfile.def_var('count', NC.INT, (xdim,))
truelatvar = ncfile.def_var('truelat', NC.FLOAT, (xdim,))
truelonvar = ncfile.def_var('truelon', NC.FLOAT, (xdim,))
riggslatvar = ncfile.def_var('riggslat', NC.FLOAT, (xdim,))
riggslonvar = ncfile.def_var('riggslon', NC.FLOAT, (xdim,))
riggsmagvar = ncfile.def_var('riggsmag', NC.FLOAT, (xdim,))
riggsazivar = ncfile.def_var('riggsazi', NC.FLOAT, (xdim,))
riggsuvar = ncfile.def_var('riggsu', NC.FLOAT, (xdim,))
riggsvvar = ncfile.def_var('riggsv', NC.FLOAT, (xdim,))

##### attributes in NetCDF file #####
# set global attributes
setattr(ncfile, 'Conventions', 'CF-1.0')
historysep = ' '
historystr = time.asctime() + ': ' + historysep.join(sys.argv) + '\n'
setattr(ncfile, 'history', historystr)
# set the attributes of the variables
setattr(xvar, 'axis', 'X')
setattr(xvar, 'long_name', 'index in RIGGS data')
setattr(truelatvar, 'long_name', 'latitude')
setattr(truelatvar, 'standard_name', 'latitude')
setattr(truelatvar, 'units', 'degrees_north')
setattr(truelonvar, 'long_name', 'longitude')
setattr(truelonvar, 'standard_name', 'longitude')
setattr(truelonvar, 'units', 'degrees_east')
setattr(riggslatvar, 'long_name', 'RIGGS grid south latitude')
setattr(riggslonvar, 'long_name', 'RIGGS grid west latitude')
setattr(riggsmagvar, 'long_name', 'RIGGS observed ice velocity magnitude')
setattr(riggsmagvar, 'units', 'm year-1')
setattr(riggsazivar, 'long_name', 'RIGGS observed ice velocity grid bearing at point')
setattr(riggsuvar, 'long_name', 'RIGGS observed ice velocity x component')
setattr(riggsuvar, 'units', 'm year-1')
setattr(riggsvvar, 'long_name', 'RIGGS observed ice velocity y component')
setattr(riggsvvar, 'units', 'm year-1')

##### write data into and close NetCDF file #####
for i in range(count):
	xvar[i] = int(riggscount[i])
truelatvar[:] = truelat
truelonvar[:] = truelon
riggslatvar[:] = riggslat
riggslonvar[:] = riggslon
riggsmagvar[:] = riggsmag
riggsazivar[:] = riggsazi
riggsuvar[:] = riggsu
riggsvvar[:] = riggsv

# finish up
ncfile.close()
print "NetCDF file ",OUT_FILE," created"

