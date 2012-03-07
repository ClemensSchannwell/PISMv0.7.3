# This file contains CMake macros used in the root CMakeLists.txt

# Set CMake variables to enable rpath
macro(pism_use_rpath)
  ## Use full RPATH, with this setting Pism libraries cannot be moved after installation
  ## but the correct libraries will always be found regardless of LD_LIBRARY_PATH
  ## in use, i.e. don't skip the full RPATH for the build tree
  set (CMAKE_SKIP_BUILD_RPATH FALSE)
  # when building, don't use the install RPATH already
  # (but later on when installing)
  set (CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) 
  # the RPATH to be used when installing
  set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${Pism_LIB_DIR}")
  # add the automatically determined parts of the RPATH
  # which point to directories outside the build tree to the install RPATH
  set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

  # Mac OS X install_name fix:
  set (CMAKE_INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/${Pism_LIB_DIR}")
endmacro(pism_use_rpath)

# Set CMake variables to disable rpath
macro(pism_dont_use_rpath)
  set (CMAKE_SKIP_BUILD_RPATH TRUE)
  set (CMAKE_BUILD_WITH_INSTALL_RPATH TRUE) 
  set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${Pism_LIB_DIR}")
  set (CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
endmacro(pism_dont_use_rpath)

# Set CMake variables to ensure that everything is static
macro(pism_strictly_static)
  set (CMAKE_FIND_LIBRARY_SUFFIXES .a)
  set (BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared Pism libraries" FORCE)
  SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
  SET(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
  set_property(GLOBAL PROPERTY LINK_SEARCH_END_STATIC 1)

  pism_dont_use_rpath()
endmacro(pism_strictly_static)

# Set CMake variables appropriate for building a .deb package
macro(pism_build_debian_package)
  set (Pism_BUILD_TYPE "Release" CACHE STRING "PISM build type" FORCE)
  set (CMAKE_INSTALL_PREFIX "/usr" CACHE STRING "Install prefix" FORCE)
  set (Pism_BUILD_DOCS OFF CACHE BOOL "Build PISM documentation" FORCE)
  set (Pism_BUILD_BROWSER OFF CACHE BOOL "Build PISM source code browsers" FORCE)
  set (Pism_BUILD_EXTRA_EXECS OFF CACHE BOOL "Build extra executables (mostly testing/verification)" FORCE)

  # RPATH handling
  pism_dont_use_rpath()
endmacro(pism_build_debian_package)

# Set the PISM revision tag
macro(pism_set_revision_tag)
  # Git
  if (EXISTS ${Pism_SOURCE_DIR}/.git)
    find_program (GIT_EXECUTABLE git DOC "Git executable")
    mark_as_advanced(GIT_EXECUTABLE)
    execute_process (COMMAND ${GIT_EXECUTABLE} describe --always --match v?.?
      WORKING_DIRECTORY ${Pism_SOURCE_DIR}
      OUTPUT_VARIABLE Pism_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif (EXISTS ${Pism_SOURCE_DIR}/.git)

  # Otherwise...
  if (NOT Pism_VERSION)
    set (Pism_VERSION "unknown")
  endif (NOT Pism_VERSION)

  set (Pism_REVISION_TAG "${Pism_BRANCH} ${Pism_VERSION}")
endmacro(pism_set_revision_tag)

# Set pedantic compiler flags
macro(pism_set_pedantic_flags)
  set (DEFAULT_PEDANTIC_CFLAGS "-pedantic -Wall -Wextra -Wno-cast-qual -Wundef -Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings -Wconversion -Wsign-compare -Wno-redundant-decls -Winline -Wno-long-long -Wmissing-format-attribute -Wmissing-noreturn -Wpacked -Wdisabled-optimization -Wmultichar -Wformat-nonliteral -Wformat-security -Wformat-y2k -Wendif-labels -Winvalid-pch -Wmissing-field-initializers -Wvariadic-macros -Wstrict-aliasing -funit-at-a-time")
  set (DEFAULT_PEDANTIC_CXXFLAGS "${DEFAULT_PEDANTIC_CFLAGS} -Woverloaded-virtual")
  set (PEDANTIC_CFLAGS ${DEFAULT_PEDANTIC_CFLAGS} CACHE STRING "Compiler flags to enable pedantic warnings")
  set (PEDANTIC_CXXFLAGS ${DEFAULT_PEDANTIC_CXXFLAGS} CACHE STRING "Compiler flags to enable pedantic warnings for C++")
  mark_as_advanced (PEDANTIC_CFLAGS PEDANTIC_CXXFLAGS)
  set (CMAKE_C_FLAGS_DEBUG "-g ${PEDANTIC_CFLAGS}")
  set (CMAKE_CXX_FLAGS_DEBUG "-g ${PEDANTIC_CXXFLAGS}")
endmacro(pism_set_pedantic_flags)