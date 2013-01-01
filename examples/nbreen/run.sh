#!/bin/bash

NN=4

#dx=125
#myMx=264
#myMy=207

#dx=250
#myMx=133
#myMy=104

dx=500
myMx=67
myMy=52

grid="-Mx $myMx -My $myMy -Mz 11 -z_spacing equal -Lz 600"

physics="-config_override nbreen_config.nc -no_mass -no_energy"

hydro="-hydrology distributed -report_mass_accounting"

pismexec="pismo -no_model_strip 1.0"

YY=1

mpiexec -n $NN $pismexec -boot_file pismnbreen.nc $physics $hydro \
  $grid -max_dt 0.1 -y $YY -o nbreen_y${YY}_${dx}m.nc

