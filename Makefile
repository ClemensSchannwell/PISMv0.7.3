SHELL = /bin/sh
VPATH = src
ALL : all

#FLAGS:

WITH_NETCDF?=1
WITH_FFTW?=1
WITH_GSL?=1
CFLAGS+= -DWITH_NETCDF=${WITH_NETCDF} -DWITH_FFTW=${WITH_FFTW}\
	-DWITH_GSL=${WITH_GSL} -pipe

ICE_LIB_FLAGS= -L`pwd`/obj -Wl,-rpath,`pwd`/obj -lpism -ltests ${PETSC_LIB}
ifeq (${WITH_NETCDF}, 1)
	ICE_LIB_FLAGS+= -lnetcdf_c++ -lnetcdf
endif
ifeq (${WITH_FFTW}, 1)
	ICE_LIB_FLAGS+= -lfftw3
endif
ifeq (${WITH_GSL}, 1)
	ICE_LIB_FLAGS+= -lgsl -lgslcblas
endif

#VARIABLES:

executables= flowTable pismr pismv pisms shelf simpleISO simpleFG

ice_sources= extrasGSL.cc grid.cc iMbasal.cc iMbeddef.cc iMdefaults.cc\
	iMgrainsize.cc iMIO.cc iMIOnetcdf.cc iMmacayeal.cc iMoptions.cc\
	iMtemp.cc iMutil.cc iMvelocity.cc iMviewers.cc iceCompModel.cc\
	iceModel.cc materials.cc 
ice_csources= cubature.c
tests_sources= exactTestsABCDE.c exactTestsFG.c exactTestH.c
exec_sources= flowTable.cc simplify.cc run.cc verify.cc get_drag.cc shelf.cc
exec_csources= simpleISO.c simpleFG.c

depfiles= $(ice_sources:.cc=.d) $(ice_csources:.c=.d) $(tests_sources:.c=.d)\
	$(exec_sources:.cc=.d) $(exec_csources:.c=.d)

ICE_OBJS= $(patsubst %.cc, %.o, ${ice_sources}) cubature.o
TESTS_OBJS= exactTestsABCDE.o exactTestsFG.o exactTestH.o

include ${PETSC_DIR}/bmake/common/base

#TARGETS:

all : depend libpism libtests $(executables)

libpism : ${ICE_OBJS}
	${CLINKER} -shared -o obj/libpism.so ${ICE_OBJS}
#	ar cru -s obj/libpism.a ${ICE_OBJS}

libtests : ${TESTS_OBJS}
	${CLINKER} -shared -o obj/libtests.so ${TESTS_OBJS}

flowTable : obj/libpism.so flowTable.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/flowTable

get_drag : obj/libpism.so get_drag.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/get_drag

pismr : obj/libpism.so run.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/pismr

pisms : obj/libpism.so simplify.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/pisms

pismv : obj/libpism.so obj/libtests.so iceCompModel.o verify.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/pismv

shelf : obj/libpism.so shelf.o
	${CLINKER} $^ ${ICE_LIB_FLAGS} -o obj/shelf

simpleISO : obj/libtests.so simpleISO.o
	${CLINKER} $^ -lm -L`pwd`/obj -Wl,-rpath,`pwd`/obj -ltests \
	 -o obj/simpleISO

simpleFG : obj/libtests.so simpleFG.o
	${CLINKER} $^ -lm -L`pwd`/obj -Wl,-rpath,`pwd`/obj -ltests \
	 -o obj/simpleFG

# Cancel the implicit rules
% : %.cc
% : %.c

# Emacs style tags
.PHONY: tags TAGS
tags TAGS :
	etags *.cc *.hh *.c

# The GNU recommended proceedure for automatically generating dependencies.
# This rule updates the `*.d' to reflect changes in `*.cc' files
%.d : %.cc
	@echo "Dependencies from" $< "-->" $@
	@set -e; rm -f $@; \
	 $(PETSC_COMPILE_SINGLE) -MM $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

# This rule updates the `*.d' to reflect changes in `*.c' files
%.d : %.c
	@echo "Dependencies from" $< "-->" $@
	@set -e; rm -f $@; \
	 $(PETSC_COMPILE_SINGLE) -MM $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

depend : $(depfiles)

depclean :
	@rm -f *.d

clean : depclean

distclean : clean
	rm -f TAGS obj/libpism.so obj/libtests.so \
	 $(patsubst %, obj/%, ${executables})

.PHONY: clean

include $(depfiles)
