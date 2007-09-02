#! /usr/bin/env python
## VERIFYNOW.PY is a script to verify several components of PISM.  Uses tests C, I and G.  
## It is intended to do roughly the minimal amount of computation to show convergence to continuum results.

## ELB 1/31/07; 2/3/07: -ksp_rtol 1e-6 added; 5/29/07 verifynow.sh --> verifynow.py; 7/20/07 used getopt

import sys
import getopt
import time
import commands
import string

# run a chosen verification   
def verify(test):
   print ' ++++ verifying ' + test[2] + ' using test ' + test[0] + ' ++++'
   print ' ' + test[5]
   for myMx in test[1][:levs]:
      if test[3] == 0:
     	   gridopts = ' -Mx ' + str(myMx) + ' -My ' + str(myMx)
      elif test[3] == 1:
         gridopts = ' -My ' + str(myMx)
      elif test[3] == 2:
         gridopts = ' -Mx ' + str(myMx) + ' -My ' + str(myMx) + ' -Mz ' + str(myMx)
      if nproc > 1:
         predo = mpi + ' -np ' + str(nproc) + ' '
      else:
         predo = ''
      testdo = predo + pref + 'pismv -test ' + test[0] + gridopts + test[4]
      print ' trying \"' + testdo + '\"'
      testdo = testdo + ' -verbose 1'  # only need final errors anyway
      try:
         lasttime = time.time()
         (status,output) = commands.getstatusoutput(testdo)
         elapsetime = time.time() - lasttime      
      except KeyboardInterrupt:
         sys.exit(2)
      if status:
         sys.exit(status)
      print ' finished in %7.4f seconds; reported numerical errors as follows:' % elapsetime
      errpos = output.find('NUMERICAL ERRORS')
      if errpos >= 0:
         errreport = output[errpos:output.rfind('Writing')-1]
         print '  |' + string.replace(errreport,'\n','\n  |')
      else:
         print ' ERROR: can\'t find reported numerical error'
         sys.exit(99)

## default settings
NP = 1
LEVELS = 3
PREFIX = ''
MPIDO = 'mpiexec'
TESTS = 'CGI'
KSPRTOL = 1e-12 # for test I
SSARTOL = 5e-7   # ditto

## tests and additional info for verification
## order here is for convenience and speed: generally do faster tests first
alltests = [
   ['B',[31,41,61,81,121],
        'isothermal SIA w moving margin',0,' -Mz 31 -ys 422.45 -y 25000.0',
        '(Mx=My=31,41,61,81,121 corresponds to dx=dy=80,60,40,30,20 km)'],
   ['C',[41,61,81,101,121],
        'isothermal SIA w moving margin',0,' -Mz 31 -y 15208.0',
        '(Mx=My=41,61,81,101,121 corresponds to dx=dy=50,33.3,25,20,16 km)'],
   ['D',[41,61,81,101,121],
        'isothermal SIA w variable accumulation',0,' -Mz 31 -y 25000.0',
        '(Mx=My=41,61,81,101,121 corresponds to dx=dy=50,33.3,25,20,16 km)'],
   ['A',[31,41,61,81,121],
        'isothermal SIA w marine margin',0,' -Mz 31 -y 25000.0',
        '(Mx=My=31,41,61,81,121 corresponds to dx=dy=53.3,40,26.7,20,13.3 km)'],
   ['E',[31,41,61,81,121],
        'isothermal SIA w sliding',0,' -Mz 31 -y 25000.0',
        '(Mx=My=31,41,61,81,121 corresponds to dx=dy=53.3,40,26.7,20,13.3 km)'],
   ['I',[49,193,769,3073,12289],'plastic till ice stream',1,
        ' -Mx 5 -ssa_rtol ' + str(SSARTOL) + ' -ksp_rtol ' + str(KSPRTOL),
        '(My=49,193,769,3073,12289 corresponds to dy=5000,1250,312.5,78.13,19.53 m)'],
   ['J',[30,60,120,180,240],'linearized, periodic ice shelf',0,
        ' -Mz 11 -ksp_rtol ' + str(KSPRTOL),
        '(My=30,60,120,180,240 corresponds to dx=dy=20,10,5,3.333,2.5 km)'],
   ['L',[31,61,91,121,181],
        'isothermal SIA w non-flat bed',0,' -Mz 31 -y 25000.0',
        '(Mx=My=31,61,91,121,181 corresponds to dx=dy=60,30,20,15,10 km)'],
   ['F',[61,91,121,181,241],'thermocoupled SIA',2,' -y 25000.0',
        '(Mx=My=Mz=61,91,121,181,241 corresponds to dx=dy=30,20,15,10,7.5 km\n'
        + ' and dz=66.7,44.4,33.3,22.2,16.7 m)'],
   ['G',[61,91,121,181,241],'thermocoupled SIA w variable accum',2,' -y 25000.0',
        '(Mx=My=Mz=61,91,121,181,241 corresponds to dx=dy=30,20,15,10,7.5 km\n'
        + ' and dz=66.7,44.4,33.3,22.2,16.7 m)']
]

## get options: -n for number of processors, -l for number of levels
nproc = NP  ## default; will not use 'mpiexec' if equal to one
levs = LEVELS
mpi = MPIDO
pref = PREFIX
letters = TESTS
try:
  opts, args = getopt.getopt(sys.argv[1:], "p:m:n:l:t:",
                             ["prefix=", "mpido=", "nproc=", "levels=", "tests="])
  for opt, arg in opts:
    if opt in ("-p", "--prefix"):
      pref = arg
    elif opt in ("-m", "--mpido"):
      mpi = arg
    elif opt in ("-n", "--nproc"):
      nproc = string.atoi(arg)
    elif opt in ("-l", "--levels"):
      levs = string.atoi(arg)
    elif opt in ("-t", "--tests"):
      letters = arg
except getopt.GetoptError:
  print 'Incorrect command line arguments'
  sys.exit(2)

print ' VERIFYNOW using %d processor(s) and %d level(s) of refinement' % (nproc, levs)
## go through verification tests
for test in alltests:
   if letters.find(test[0]) > -1:
       verify(test)
   
