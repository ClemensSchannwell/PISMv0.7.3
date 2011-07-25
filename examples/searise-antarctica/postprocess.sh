#!/bin/bash

# Copyright (C) 2009-2011 The PISM Authors
#
# This script should produce a file which conforms to SeaRISE output format at
#    http://websrv.cs.umt.edu/isis/index.php/Output_Format
#
# Before using this script:
#    1. run preprocess.sh to download SeaRISE "Present Day Antarctica" master
#       dataset and adjust metadata
#    2. run antspinCC.sh to do spinup with Constant Climate
#    3. run experiments.sh to do
#       experiment runs into these files:
#           extra_PIK1_A_D3_C?_E?.nc
#           ts_PIK1_A_D3_C?_E?.nc
#    4. run this current script 
#         ./postprocess.sh
#       (or "./postprocess.sh ABC9" if runs are ABC9_...)
#       to produce
#           PIK1_A_D3_C?_E?.nc
#
# This script
#    -- requires NCO:   http://nco.sourceforge.net/
#    -- depends on postprocess_mask.py, and thus 
#    -- requires python and netcdf4-python

set -e  # exit on error
set -x  # uncomment to see commands as they are issued

#Prefix="PIK1_A_D3_"

MODEL=PIK1  # default initials and model number
if [ $# -gt 0 ] ; then
  MODEL="$1"
fi

INSTITUTION="Potsdam Institute for Climate Impact Research (PIK), Germany"  # will appear as global attribute in final file
if [ $# -gt 1 ] ; then
  INSTITUTION="$2"
fi

experiment=seaRISE
scriptname=experiments
laufname=ConstantClimate # this relates to the type of the spinup, namely antspinCC.sh
output_path=./results/$experiment/$scriptname/$laufname
RESDIR=$output_path/results

# process files to remove unneeded fields, combine spatial and scalar series, and fix metadata
for NAME in "${MODEL}_A_D3_C1_E0" \
            "${MODEL}_A_D3_C2_E0" "${MODEL}_A_D3_C3_E0" "${MODEL}_A_D3_C4_E0" \
            "${MODEL}_A_D3_C1_S1" "${MODEL}_A_D3_C1_S2" "${MODEL}_A_D3_C1_S3" \
            "${MODEL}_A_D3_C1_M1" "${MODEL}_A_D3_C1_M2" "${MODEL}_A_D3_C1_M3"; do

  echo "(postprocess.sh)  working on deliverable $NAME.nc ..."

  echo "(postprocess.sh)    copying from name extra_${NAME}.nc and removing unreported fields ..."
  # create draft of deliverable file:
  ncks -O ${RESDIR}/extra_${NAME}.nc -o ${NAME}_full.nc
  # calculate yearly-averages of acab and dHdt using ncap2 sleight of hand.
  ncap2 -O -s '*sz_idt=time.size();  acab[$time,$x,$y]= 0.f; dHdt[$time,$x,$y]= 0.f; for(*idt=1 ; idt<sz_idt ; idt++) {acab(idt,:,:)=(acab_cumulative(idt,:,:)-acab_cumulative(idt-1,:,:))/(time(idt)-time(idt-1)); dHdt(idt,:,:)=(thk(idt,:,:)-thk(idt-1,:,:))/(time(idt)-time(idt-1));}' ${NAME}_full.nc ${NAME}_full.nc
  # adjust meta data for new fields
  ncatted -a units,acab,o,c,"m year-1" -a units,dHdt,o,c,"m year-1" \
      -a long_name,acab,o,c,"surface mass balance" \
      -a long_name,dHdt,o,c,"rate of change of ice thickness" \
      -a grid_mapping,acab,o,c,"mapping" \
      -a grid_mapping,dHdt,o,c,"mapping" \
      -a cell_methods,acab,o,c,"time: mean (interval: 1 year)" \
      -a cell_methods,dHdt,o,c,"time: mean (interval: 1 year)" ${NAME}_full.nc
  # We keep the "full" files for record
  # select every fifth year, don't copy acab_cumulative,tempicethk_basal,tauc,cbase,csurf,diffusivity,pism_overrides
  ncks -O -x -v acab_cumulative,tempicethk_basal,tauc,cbase,csurf,diffusivity,pism_overrides \
      -d time,,,5 ${NAME}_full.nc ${NAME}.nc
  echo "(postprocess.sh)    combining annual scalar time series ts_${NAME}.nc with spatial file ..."
  cp ${RESDIR}/ts_${NAME}.nc tmp.nc
  ncrename -d time,tseries -v time,tseries tmp.nc    # SeaRISE name choice
  ncecat -O tmp.nc tmp.nc                            # convert time to non-record dimension
  ncwa -O -a record tmp.nc tmp.nc                    # remove just-added record dimension
  ncks -A -v ivol,iareag,iareaf tmp.nc -o ${NAME}.nc # actually combine
  rm tmp.nc                                          # clean up

  echo "(postprocess.sh)    fixing metadata and names ..."
  ncpdq -O -a time,y,x ${NAME}.nc ${NAME}.nc         # change dimension order
  ncrename -v bwat,bwa ${NAME}.nc                    # fix "bwa" name
  ncatted -a units,time,m,c,"years since 2004-1-1 0:0:0" ${NAME}.nc
  ncatted -a units,tseries,m,c,"years since 2004-1-1 0:0:0" ${NAME}.nc
  ncatted -a bounds,,d,c, ${NAME}.nc                 # remove time bounds; no one cares ...
  ncatted -a coordinates,,d,c, ${NAME}.nc            # remove all "coordinates = "lat long"",
                                                     #   because lat,lon are not present
  ncatted -a pism_intent,,d,c, ${NAME}.nc            # irrelevant to SeaRISE purpose
  # we do not report gline_flx
  ncatted -a institution,global,c,c,"${INSTITUTION}" ${NAME}.nc
  ncatted -a title,global,m,c,"${NAME} SeaRISE Experiment (Antarctica)" ${NAME}.nc

  echo "(postprocess.sh)    fixing mask to conform to spec, using postprocess_mask.py ..."
  ./postprocess_mask.py ${NAME}.nc
#  /iplex/01/sys/applications/python/ibin/cpy ./postprocess_mask.py ${NAME}.nc 		# This is to use python at PIK cluster...

  echo "(postprocess.sh)    file $NAME.nc done "
  echo
  echo "(postprocess.sh) done "
done

