#!/bin/bash
# script to play with EISMINT II experiment I and plastic till; for rev 195 and later

NN=8

mpiexec -n $NN pisms -eisII I -Mx 61 -My 61 -Mz 201 -y 100000 -o eis2I100k

mpiexec -n $NN pisms -eisII I -if eis2I100k.nc -y 80000 -o eis2I180k

mpiexec -n $NN pisms -eisII I -Mx 121 -My 121 -Mz 301 -regrid eis2I180k.nc -regrid_vars TBHh \
   -y 20000 -track_Hmelt -f3d -o eis2I_fine_wmelt

mpiexec -n $NN pisms -eisII I -if eis2I_fine_wmelt.nc -ssa -plastic -super -verbose \
   -till_phi 20.0,5.0 -y 5 -f3d -o eis2I_plastic5

mpiexec -n $NN pisms -eisII I -if eis2I_plastic5.nc -ssa -plastic -super -verbose \
   -till_phi 20.0,5.0 -y 5 -f3d -o eis2I_plastic10

mpiexec -n $NN pisms -eisII I -if eis2I_plastic10.nc -ssa -plastic -super -verbose \
   -till_phi 20.0,5.0 -y 10 -f3d -o eis2I_plastic20

mpiexec -n $NN pisms -eisII I -if eis2I_plastic20.nc -ssa -plastic -super -verbose \
   -till_phi 20.0,5.0 -y 80 -f3d -o eis2I_plastic100

