import os
import tempfile
import string

petsc_dir = os.environ.get('PETSC_DIR', '/usr/lib/petsc')
petsc_arch = os.environ.get('PETSC_ARCH', 'linux-gnu-c-opt')
mpicc = os.environ.get('MPICC', 'mpicc')
mpicxx = os.environ.get('MPICXX', 'mpicxx')
home = os.environ.get('HOME')
my_ccflags = "-Wall -Wextra -Wshadow -Wwrite-strings -Wno-unused-parameter"
my_ccflags += " -Wno-strict-aliasing -Wpointer-arith -Wconversion -Winline"
#my_ccflags += " -Wcast-qual -Wpadded -Wunreachable-code" # These are excessive
my_ccflags += " -g3 -pipe"

defines = " -DWITH_FFTW=1 -DWITH_GSL=1"

makefile_contents = ('include ' + os.path.join(petsc_dir, 'bmake/common/base') + '\n'
            + 'ALL : \n'
            + '\t@echo ${PETSC_COMPILE_SINGLE}\n'
            + '\t@echo ${CLINKER}\n'
            + '\t@echo ${PETSC_LIB}')

#print makefile_contents

makefile = tempfile.mkstemp()
os.write(makefile[0], makefile_contents)
outputfile = tempfile.mkstemp()
os.system('make ALL -f ' + makefile[1] + ' > ' + outputfile[1])
os.remove(makefile[1])
output = open(outputfile[1])
petsc_compile_single = output.readline()
petsc_clinker = output.readline()
petsc_lib = output.readline()[:-1]
os.remove(outputfile[1])

#print petsc_compile_single, petsc_clinker, petsc_lib
petsc_cc = petsc_compile_single.split(' ')[0]
petsc_cxx = petsc_cc.replace('mpicc', 'mpicxx')
petsc_ccflags = [w for w in petsc_compile_single.split(' ')[2:]
                if w[:2] in ('-f', '-W', '-g', '-p')]
petsc_cpppath = [w[2:] for w in petsc_compile_single.split(' ')[2:]
                 if w[:2] == '-I']
petsc_libpath = [w[2:] for w in petsc_lib.split(' ')
                 if w[:2] == '-L']
petsc_rpath = [w[11:] for w in petsc_lib.split(' ')
               if w[:11] == '-Wl,-rpath,']
petsc_libs = [w[2:] for w in petsc_lib.split(' ')
              if w[:2] == '-l']

petsc_env = Environment(ENV = {'PATH' : os.environ['PATH']},
                        CC=petsc_cc, CXX=petsc_cxx,
                        CPPPATH=petsc_cpppath,
                        LIBPATH=petsc_libpath,
                        RPATH=petsc_rpath)

my_env = Environment(ENV = {'PATH' : os.environ['PATH']},
                     CC=mpicc, CXX=mpicxx,
                     CCFLAGS=my_ccflags,
                     CPPPATH=[os.path.join(petsc_dir, 'include'),
                              os.path.join(petsc_dir, 'bmake', petsc_arch)],
                     LIBPATH=[os.path.join(petsc_dir, 'lib', petsc_arch),
                              '/usr/X11R6/lib', '/usr/lib/atlas/sse2'],
                     RPATH=[''])

if True:
    env = petsc_env
    ccflags = string.join(petsc_ccflags)

conf = Configure(env)

if conf.CheckCHeader('netcdf.h'):
    defines += ' -DWITH_NETCDF=1'
else:
    print 'No netCDF support.'
    defines += ' -DWITH_NETCDF=0'

env = conf.Finish()

libpism_dir = os.path.join(os.getcwd(), 'obj')
#print libpism_dir
env.Append(CCFLAGS = ccflags + defines)
env.Append(LIBPATH = [libpism_dir])
env.Append(RPATH = [libpism_dir])

#petsc_libs = ['petsc' + mod for mod in Split('ksp dm mat vec') + [""]]
pism_libs = petsc_libs + Split('stdc++ netcdf fftw3 gsl')

if False:
    print 'CC        = ', env['CC']
    print 'CXX       = ', env['CXX']
    print 'CCFLAGS   = ', env['CCFLAGS']
    print 'CPPPATH   = ', env['CPPPATH']
    print 'LIBPATH   = ', env['LIBPATH']
    print 'RPATH     = ', env['RPATH']
    print 'pism_libs = ', pism_libs

Export('env pism_libs')

SConscript('src/SConscript', build_dir='obj', duplicate=0)
SConscript('doc/SConscript', duplicate=0)

