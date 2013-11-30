#!/bin/bash
NN=4

export REGRIDFILE=g20km_10ka_hy.nc
export EXSTEP=100
./spinup.sh $NN const 1000  10 hybrid g10km_gridseq.nc

export REGRIDFILE=g10km_gridseq.nc
export EXSTEP=10
./spinup.sh $NN const 100    5 hybrid  g5km_gridseq.nc
