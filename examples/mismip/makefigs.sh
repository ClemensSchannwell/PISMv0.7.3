#!/bin/bash
 
# Simply creates useful .png figures from MISMIP steady state output files,
# i.e. ABC1_1a_M1_A1_ss and ABC1_1a_M1_A1_f.  Applies python scripts 
# figsMISMIP.py and showflux.py to all such files.

shopt -s nullglob  # don't return list of files if none found

set -x

for file in *_ss
do
  prefix=`echo $file | sed 's/_ss//g'`  # strip "_ss" from file name
  #./figsMISMIP.py -p $prefix  # if _extras not present
  ./figsMISMIP.py -p $prefix --extras
  ./showflux.py -f ${prefix}.nc -o flux_${prefix}.png
done

