#!/bin/bash
#   Downloads data and runs an EISMINT-Ross example in PISM.  See User's Manual.

NN=1  # default number of processors
if [ $# -gt 0 ] ; then  # if user says "./quickstart.sh 8" then NN = 8
  NN="$1"
fi

set -e  # exit on error

echo "-----  Download the ASCII files from the EISMINT web site:"
for fname in "111by147Grid.dat" "kbc.dat" "inlets.dat"
do
  wget -nc http://homepages.vub.ac.be/~phuybrec/eismint/$fname
done

echo "-----  Run eisross.py to turn ascii data into NetCDF file ross.nc:"
./eisross.py -o ross.nc

echo "-----  Run riggs.py to create NetCDF version riggs.nc of RIGGS data:"
./riggs.py -o riggs.nc

echo "-----  Running pismd to compute velocity in Ross ice shelf,"
echo "       with comparison to RIGGS data:"
mpiexec -n $NN pismd -ross -boot_from ross.nc -Mx 147 -My 147 -Mz 3 -Lz 1e3 -ssa -ssaBC ross.nc -riggs riggs.nc -o_size big -o rossComputed.nc

echo "----- Generating figure comparing model vs observed velocity:"
./rossplot.py --pism-output=rossComputed.nc --riggs=riggs_clean.dat

echo "-----  Done."

