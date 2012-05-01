#!/bin/bash

# Copyright (C) 2011-2012 the PISM authors

# downloads 5km data, including a precomputed PISM state for the whole
# ice sheet and the SeaRISE 5km Greenland data set, from which we will only
# grab the "smb" = Surface Mass Balance field, which was produced by
# the regional atmosphere model RACMO

# depends on wget and NCO (ncrename, ncap, ncwa, ncks)

set -e  # exit on error

echo "fetching pre-computed whole ice-sheet result on 5km grid"
URL=http://www.pism-docs.org/download
WHOLE=g5km_0_ftt.nc
wget -nc ${URL}/$WHOLE
echo "... done"
echo

# get file; see page http://websrv.cs.umt.edu/isis/index.php/Present_Day_Greenland
DATAVERSION=1.1
DATAURL=http://websrv.cs.umt.edu/isis/images/a/a5/
DATANAME=Greenland_5km_v$DATAVERSION.nc
echo "fetching 5km SeaRISE data file which contains surface mass balance ... "
wget -nc ${DATAURL}${DATANAME}
echo "  ... done"
echo

BCFILE=g5km_bc.nc
echo "creating PISM-readable boundary conditions file $BCFILE"
echo "   from whole ice sheet model result ..."
ncks -O -v u_ssa,v_ssa,bmelt,enthalpy $WHOLE $BCFILE
# rename u_ssa and v_ssa so that they are specified as b.c.
ncrename -O -v u_ssa,u_ssa_bc -v v_ssa,v_ssa_bc $BCFILE
echo "... done with creating bc file $BCFILE"
echo

CLIMATEFILE=g5km_climate.nc
echo "creating PISM-readable climate file $CLIMATEFILE from airtemp2m and smb in data file ..."
ncks -O -v mapping,smb,airtemp2m $DATANAME $CLIMATEFILE
ncrename -O -v airtemp2m,artm $CLIMATEFILE
ncatted -O -a units,artm,a,c,"Celsius" $CLIMATEFILE
ncap -O -s "acab=(1000.0/910.0)*smb" $CLIMATEFILE $CLIMATEFILE
ncatted -O -a standard_name,acab,a,c,"land_ice_surface_specific_mass_balance" $CLIMATEFILE
ncatted -O -a units,acab,a,c,"meters/year" $CLIMATEFILE
ncks -O -x -v smb $CLIMATEFILE $CLIMATEFILE
echo "... done with creating climate file $CLIMATEFILE"
echo


