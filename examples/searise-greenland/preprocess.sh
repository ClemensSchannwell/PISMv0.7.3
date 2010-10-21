#!/bin/bash

# Copyright (C) 2009-2010 Ed Bueler and Andy Aschwanden

# PISM SeaRISE Greenland
#
# downloads SeaRISE "Present Day Greenland" master dataset NetCDF file, adjusts metadata,
# saves under new name, ready for PISM

# depends on wget and NCO (ncrename, ncap, ncatted, ncpdq, ncks)

set -e  # exit on error

echo
echo "# =================================================================================="
echo "# PISM SeaRISE Greenland: preprocessing"
echo "# =================================================================================="
echo


# get file; see page http://websrv.cs.umt.edu/isis/index.php/Present_Day_Greenland
DATAVERSION=0.93
DATANAME=Greenland_5km_v$DATAVERSION.nc
PISMVERSION=pism_$DATANAME
wget -nc http://websrv.cs.umt.edu/isis/images/8/86/$DATANAME

ncks -O $DATANAME $PISMVERSION  # just copies over, but preserves history and global attrs

# adjust metadata; uses NCO (http://nco.sourceforge.net/)
ncrename -O -v x1,x -v y1,y -d x1,x -d y1,y $PISMVERSION
ncrename -O -v time,t -d time,t $PISMVERSION
ncrename -O -v usrf,usurf $PISMVERSION
# we will use present surface temps from Fausto et al 2009 (J. Glaciol. vol 55 no 189)
ncap -O -s "temp_ma=presartm+273.15" $PISMVERSION $PISMVERSION
ncatted -O -a units,temp_ma,a,c,"K" $PISMVERSION
# convert from water equiv to ice thickness change rate; assumes ice density 910.0 kg m-3
ncap -O -s "precip=presprcp*(1000.0/910.0)" $PISMVERSION $PISMVERSION
ncatted -O -a units,precip,a,c,"m a-1" $PISMVERSION
# delete incorrect standard_name attribute from bheatflx; there is no known standard_name
ncatted -a standard_name,bheatflx,d,, $PISMVERSION
ncks -O -v x,y,lat,lon,bheatflx,topg,thk,snowprecip,temp_ma,mapping \
  $PISMVERSION $PISMVERSION
echo "  PISM-readable file $PISMVERSION created from $DATANAME"
echo "    (contains only fields used in bootstrapping ...)"

# extract time series into files suitable for -dTforcing and -dSLforcing;
# compare resulting files to grip_dT.nc and specmap_dSL.nc in pism0.2/examples/eisgreen/
TEMPSERIES=pism_dT.nc
SLSERIES=pism_dSL.nc
ncks -O -v oisotopestimes,temp_time_series $DATANAME $TEMPSERIES
ncrename -O -d oisotopestimes,t -v oisotopestimes,t -v temp_time_series,delta_T $TEMPSERIES
ncpdq -O --rdr=-t $TEMPSERIES $TEMPSERIES  # reverse time dimension
ncap -O -s "t=-t" $TEMPSERIES $TEMPSERIES  # make times follow same convention as PISM
ncatted -O -a units,t,a,c,"years since 1-1-1" $TEMPSERIES
echo "  PISM-readable paleo-temperature file $TEMPSERIES created from $DATANAME"
echo "    (for option -dTforcing)"
ncks -O -v sealeveltimes,sealevel_time_series $DATANAME $SLSERIES
ncrename -O -d sealeveltimes,t -v sealeveltimes,t -v sealevel_time_series,delta_sea_level $SLSERIES
ncpdq -O --rdr=-t $SLSERIES $SLSERIES  # reverse time dimension
ncap -O -s "t=-t" $SLSERIES $SLSERIES  # make times follow same convention as PISM
ncatted -O -a units,t,a,c,"years since 1-1-1" $SLSERIES
echo "  PISM-readable paleo-sea-level file $SLSERIES created from $DATANAME"
echo "    (for option -dSLforcing)"
echo
