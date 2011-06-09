#!/bin/bash

# Copyright (C) 2009-2011 Andy Aschwanden and Ed Bueler
# PISM SeaRISE Greenland worked example

# Before using this script, run preprocess.sh and then spinup.sh
# recommended way to run with N processors is "./forecast.sh N >& out.forecast &"


INITIALS=UAF${2}  # output file names will be ...UAF1_G_D3_C1_E0..., etc.
SPINUPRESULT=$3  # spinup 

echo
echo "# ============================================================================="
echo "# PISM SeaRISE Greenland: forecast runs"
echo "# ============================================================================="
echo

set -e  # exit on error

if [ -n "${SCRIPTNAME:+1}" ] ; then
  echo "[SCRIPTNAME=$SCRIPTNAME (already set)]"
  echo ""
else
  SCRIPTNAME="#(forecast.sh)"
fi

NN=2  # default number of processors
if [ $# -gt 0 ] ; then  # if user says "control.sh 8" then NN = 8
  NN="$1"
fi

PISM_CONFIG=searise_config.nc

for INPUT in $SPINUPRESULT $PISM_CONFIG; do
  if [ -e "$INPUT" ] ; then  # check if file exist
    echo "$SCRIPTNAME INPUT   $INPUT FOUND"
  else
    echo "$SCRIPTNAME INPUT   $INPUT MISSING!!!"
    echo
    echo "$SCRIPTNAME !!!!   RUN  spinup.sh  TO GENERATE  $INPUT   !!!!"
    echo
  fi
done

echo "$SCRIPTNAME              NN = $NN"

# set MPIDO if using different MPI execution command, for example:
#  $ export PISM_MPIDO="aprun -n "
if [ -n "${PISM_MPIDO:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME      PISM_MPIDO = $PISM_MPIDO  (already set)"
else
  PISM_MPIDO="mpiexec -n "
  echo "$SCRIPTNAME      PISM_MPIDO = $PISM_MPIDO"
fi

# check if env var PISM_DO was set (i.e. PISM_DO=echo for a 'dry' run)
if [ -n "${PISM_DO:+1}" ] ; then  # check if env var DO is already set
  echo "$SCRIPTNAME         PISM_DO = $PISM_DO  (already set)"
else
  PISM_DO="" 
fi

# prefix to pism (not to executables)
if [ -n "${PISM_PREFIX:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME     PISM_PREFIX = $PISM_PREFIX  (already set)"
else
  PISM_PREFIX=""    # just a guess
  echo "$SCRIPTNAME     PISM_PREFIX = $PISM_PREFIX"
fi

# set PISM_EXEC if using different executables, for example:
#  $ export PISM_EXEC="pismr -cold"
if [ -n "${PISM_EXEC:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME       PISM_EXEC = $PISM_EXEC  (already set)"
else
  PISM_EXEC="pismr"
  echo "$SCRIPTNAME       PISM_EXEC = $PISM_EXEC"
fi

echo


# default choices in parameter study; see Bueler & Brown (2009) re "tillphi"
TILLPHI="-topg_to_phi 5.0,20.0,-300.0,700.0,10.0"

FULLPHYS="-ssa_sliding -thk_eff $TILLPHI"

TITLE="SeaRISE Greenland Forecast"

# cat prefix and exec together
PISM="${PISM_PREFIX}${PISM_EXEC} -ocean_kill -config_override $PISM_CONFIG -title \"$TITLE\" $FULLPHYS"


# coupler settings
COUPLER_CTRL="-ocean constant -atmosphere searise_greenland -surface pdd"
# coupler settings for spin-up (i.e. with forcing)
COUPLER_AR4="-ocean constant -atmosphere searise_greenland,anomaly -surface pdd"

echo "$SCRIPTNAME      executable = '$PISM'"
echo "$SCRIPTNAME         tillphi = '$TILLPHI'"
echo "$SCRIPTNAME    full physics = '$FULLPHYS'"
echo "$SCRIPTNAME control coupler = '$COUPLER_CTRL'"
echo "$SCRIPTNAME     AR4 coupler = '$COUPLER_AR4'"

expackage="-extra_vars usurf,topg,thk,bmelt,bwat,bwp,mask,uvelsurf,vvelsurf,wvelsurf,uvelbase,vvelbase,wvelbase,tempsurf,tempbase,diffusivity"
tspackage="-ts_vars ivol,iareag,iareaf"

SKIP=200


STARTTIME=0
ENDTIME=500

TIMES=0:5:${ENDTIME}
TSTIMES=0:1:${ENDTIME}

INNAME=${SPINUPRESULT}

CLIMATE=1
SRGEXPERCATEGORY=E0

PISM_SRPREFIX2=${INITIALS}_G_D3_C${CLIMATE}_${SRGEXPERCATEGORY}
OUTNAME=out_y${ENDTIME}_${PISM_SRPREFIX2}.nc
EXNAME=${PISM_SRPREFIX2}_raw_y${ENDTIME}.nc
TSNAME=ts_y${ENDTIME}_${PISM_SRPREFIX2}.nc
echo
echo "$SCRIPTNAME  5km grid: $SRGNAME run with constant climate from $STARTTIME to $ENDTIME years w save every 5 years:"
echo
cmd="$PISM_MPIDO $NN $PISM -skip SKIP -i $INNAME $COUPLER_CTRL -ys $STARTTIME -ye $ENDTIME -o $OUTNAME \
  -extra_file $EXNAME -extra_times $TIMES $expackage \
  -ts_file $TSNAME -ts_times $TSTIMES $tspackage"
$PISM_DO $cmd
echo
echo "$SCRIPTNAME  $SRGNAME run with constant climate done; runs ...$PISM_SRPREFIX2... will need post-processing"
echo

SLIDING=1

for sliding_scale_factor in 2 2.5 3; do


  SRGEXPERCATEGORY="S$SLIDING"

  PISM_SRPREFIX1=${INITIALS}_G_D3_C${CLIMATE}_${SRGEXPERCATEGORY}
  OUTNAME=out_y${ENDTIME}_${PISM_SRPREFIX1}.nc
  EXNAME=${PISM_SRPREFIX1}_raw_y${ENDTIME}.nc
  TSNAME=ts_y${ENDTIME}_${PISM_SRPREFIX1}.nc
  echo
  echo "$SCRIPTNAME  5km grid: $SRGNAME run with steady climate from $STARTTIME to $ENDTIME years w save every 5 years:"
  echo
  cmd="$PISM_MPIDO $NN $PISM -skip SKIP -i $INNAME $COUPLER_CTRL -ys $STARTTIME -ye $ENDTIME -o $OUTNAME \
  -sliding_scale $sliding_scale_factor \
  -extra_file $EXNAME -extra_times $TIMES $expackage \
  -ts_file $TSNAME -ts_times $TSTIMES $tspackage "
  $PISM_DO $cmd
  echo
  echo "$SCRIPTNAME  $SRGNAME run with steady climate done; results ...$PISM_SRPREFIX1... will need post-processing"
  echo

  SLIDING=$(($SLIDING + 1))

done


CLIMATE=2

SRGEXPERCATEGORY=E0

for climate_scale_factor in 1.0 1.5 2.0; do


    # anomaly files
    AR4PRECIP=ar4_precip_anomaly_scalefactor_${climate_scale_factor}.nc
    AR4TEMP=ar4_temp_anomaly_scalefactor_${climate_scale_factor}.nc
    for INPUT in $AR4PRECIP $AR4TEMP; do
        if [ -e "$INPUT" ] ; then  # check if file exist
        echo "$SCRIPTNAME INPUT   $INPUT FOUND"
        else
            echo "$SCRIPTNAME INPUT   $INPUT MISSING!!!"
            echo
            echo "$SCRIPTNAME !!!!     please run preprocess.sh    !!!!"
            echo
        fi
    done
    
    PISM_SRPREFIX2=${INITIALS}_G_D3_C${CLIMATE}_${SRGEXPERCATEGORY}
    OUTNAME=out_y${ENDTIME}_${PISM_SRPREFIX2}.nc
    EXNAME=${PISM_SRPREFIX2}_raw_y${ENDTIME}.nc
    TSNAME=ts_y${ENDTIME}_${PISM_SRPREFIX2}.nc
    echo
    echo "$SCRIPTNAME  5km grid: $SRGNAME run with AR4 climate from $STARTTIME to $ENDTIME years w save every 5 years:"
    echo
    cmd="$PISM_MPIDO $NN $PISM -skip SKIP -i $INNAME $COUPLER_AR4 -ys $STARTTIME -ye $ENDTIME -o $OUTNAME \
       -anomaly_temp $AR4TEMP -anomaly_precip $AR4PRECIP \
       -extra_file $EXNAME -extra_times $TIMES $expackage \
       -ts_file $TSNAME -ts_times $TSTIMES $tspackage"
    $PISM_DO $cmd
    echo
    echo "$SCRIPTNAME  $SRGNAME run with AR4 climate done; runs ...$PISM_SRPREFIX2... will need post-processing"
    echo

    CLIMATE=$(($CLIMATE + 1))

done


