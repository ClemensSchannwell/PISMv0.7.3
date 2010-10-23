#!/bin/bash

# Copyright (C) 2010 Ed Bueler

# Uses a SeaRISE-Greenland data set to illustrate the use of regional
# climate model (RCM) output to find PDD parameters.  The goal is for the
# surface mass balance from PISM's PDD model to closely fit the corresponding
# RACMO/GR regional climate model output, from Ettema et al.

# This is the top-level script, but see README!
# Suggested way to run:  $ ./dotune.sh >& out.dotune &

set -e  # exit on error

# do these first to generate data:
#./preprocess.sh # generates pism_Greenland_5km_v1.1.nc and base_config.nc
#./boot.sh  # creates start.nc, which contains 'thk' used in masking in objective.py

# harder: 5^5 = 3125 cases
THRESHOLDRANGE="268 269 270 271 273"           # default 273.15
DDFSNOWRANGE="0.001 0.002 0.003 0.005 0.009"   # default 0.003
DDFICERANGE="0.002 0.005 0.008 0.012 0.024"    # default 0.008
REFREEZERANGE="0.4 0.5 0.6 0.7 0.8"            # default 0.6
STDDEVRANGE="1.0 1.5 2.5 4.0 6.0"              # default 2.53

# simpler: 3^5 = 243 cases
THRESHOLDRANGE="268 270 273"       # default 273.15
DDFSNOWRANGE="0.001 0.003 0.009"   # default 0.003
DDFICERANGE="0.002 0.008 0.024"    # default 0.008
REFREEZERANGE="0.4 0.6 0.8"        # default 0.6
STDDEVRANGE="1.0 2.5  6.0"         # default 2.53

export DELETECLIMATE=1    # causes .nc produced by pclimate to be deleted

for THRESHOLD in $THRESHOLDRANGE
do
  for DDFSNOW in $DDFSNOWRANGE
  do
    for DDFICE in $DDFICERANGE
    do
      for REFREEZE in $REFREEZERANGE
      do
        for STDDEV in $STDDEVRANGE
        do

          ## if not deleted, output of runcase.sh is 
          ##   clim_${THRESHOLD}_${DDFSNOW}_${DDFICE}_${REFREEZE}_${STDDEV}.nc

          # run case adds case to diffs.txt:
          ./runcase.sh $THRESHOLD $DDFSNOW $DDFICE $REFREEZE $STDDEV diffs.txt

        done
      done
    done
  done
done

export DELETECLIMATE=

