#!/bin/bash

# format:
#   run.sh PROCS GRID DURATION TYPE
# here TYPE is in {dist, lakes}

if [ $# -lt 4 ] ; then
  echo "run.sh ERROR: needs four arguments"
  echo "  example usage: 'run.sh 4 500 5 dist' for"
  echo "  4 processors, 500 m grid, 5 model year run, and '-hydrology distributed'"
  exit
fi

NN="$1"

if [ "$2" -eq "500" ]; then
  dx=500
  dtmax=0.1
  myMx=67
  myMy=52
elif [ "$2" -eq "250" ]; then
  dx=250
  dtmax=0.1
  myMx=133
  myMy=104
elif [ "$2" -eq "125" ]; then
  dx=125
  dtmax=0.01  # more frequent just because so many hydrology substeps occur
  myMx=264
  myMy=207
else
  echo "invalid second argument: must be in {125,250,500}"
  exit
fi

YY="$3"

if [ "$4" = "dist" ]; then
  # distributed run
  oname=nbreen_y${YY}_${dx}m.nc
  hydro="-hydrology distributed -hydrology_null_strip 1.0 -report_mass_accounting -ssa_sliding -ssa_dirichlet_bc"
  evarlist="cbase,bmelt,bwat,bwp,bwatvel,bwprel,effbwp"
elif [ "$4" = "lakes" ]; then
  # lakes run: very fast
  oname=nbreen_y${YY}_${dx}m_lakes.nc
  hydro="-hydrology lakes -hydrology_null_strip 1.0 -report_mass_accounting"
  evarlist="bmelt,bwat,bwp,bwatvel"
else
  echo "invalid fourth argument; must be in {dist,lakes}"
  exit
fi

pismexec="pismr"

data=pismnbreen.nc

grid="-Mx $myMx -My $myMy -Mz 11 -z_spacing equal -Lz 600"

climate="-surface given -surface_given_file $data"

physics="-config_override nbreen_config.nc -no_mass -no_energy"

diagnostics="-extra_file extras_$oname -extra_times 0:0.1:$YY -extra_vars $evarlist"

mpiexec -n $NN $pismexec -boot_file $data $climate $physics $hydro \
    $grid -max_dt $dtmax -y $YY $diagnostics -o $oname

hydroinput="-input_to_bed_file fakesummerevent.nc"
onameinput=nbreen_summerevent_${dx}m_$4.nc
morediagnostics="-extra_file extras_$onameinput -extra_times 2012.0:daily:2013.0 -extra_vars $evarlist"

mpiexec -n $NN $pismexec -i $oname $climate $physics $hydro $hydroinput \
    -max_dt $dtmax -ys 2012.0 -ye 2013.0 $morediagnostics -o $onameinput
