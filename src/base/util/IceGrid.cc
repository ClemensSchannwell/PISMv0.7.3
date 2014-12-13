// Copyright (C) 2004-2014 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <petscfix.h>
#include "IceGrid.hh"
#include "pism_const.hh"
#include "PISMTime.hh"
#include "PISMTime_Calendar.hh"
#include "PISMConfig.hh"
#include "pism_options.hh"
#include <cstdlib>              // abort()
#include "error_handling.hh"

namespace pism {

Periodicity string_to_periodicity(const std::string &keyword) {
    if (keyword == "none") {
    return NOT_PERIODIC;
  } else if (keyword == "x") {
    return X_PERIODIC;
  } else if (keyword == "y") {
    return Y_PERIODIC;
  } else if (keyword == "xy") {
    return XY_PERIODIC;
  } else {
    throw RuntimeError::formatted("grid periodicity type '%s' is invalid.",
                                  keyword.c_str());
  }
}

PISMDM::PISMDM(DM dm) {
  m_dm = dm;
}

PISMDM::~PISMDM() {
  PetscErrorCode ierr = DMDestroy(&m_dm); CHKERRCONTINUE(ierr);
  if (ierr != 0) {
    // We can't do anything about this failure. We can't recover
    // from it, and it is almost certainly caused by a programming
    // error. So, we call abort().
    abort();
  }
}

DM PISMDM::get() const {
  return m_dm;
}

PISMDM::operator DM() const {
  return m_dm;
}

IceGrid::IceGrid(MPI_Comm c, const Config &conf)
  : config(conf), com(c), m_unit_system(config.get_unit_system()) {

  MPI_Comm_rank(com, &m_rank);
  MPI_Comm_size(com, &m_size);

  // The grid in symmetric with respect to zero by default.
  m_x0 = 0.0;
  m_y0 = 0.0;

  // initialize these data members to get rid of a valgrind warning;
  // correct values will be set in IceGrid::allocate()
  m_xs = 0;
  m_ys = 0;
  m_xm = 0;
  m_ym = 0;

  std::string word = config.get_string("grid_periodicity");
  m_periodicity = string_to_periodicity(word);

  unsigned int Mz = config.get("grid_Mz");
  double Lz = config.get("grid_Lz");
  SpacingType spacing = EQUAL;
  word = config.get_string("grid_ice_vertical_spacing");
  if (word == "quadratic") {
    spacing = QUADRATIC;
  } else if (word == "equal") {
    spacing = EQUAL;
  } else {
    throw RuntimeError::formatted("ice vertical spacing type '%s' is invalid.",
                                  word.c_str());
  }
  set_vertical_levels(Lz, Mz, spacing);

  m_Lx  = config.get("grid_Lx");
  m_Ly  = config.get("grid_Ly");

  m_Mx  = static_cast<int>(config.get("grid_Mx"));
  m_My  = static_cast<int>(config.get("grid_My"));

  m_Nx = 0;
  m_Ny = 0;                  // will be set to a correct value in allocate()

  std::string calendar;
  try {
    calendar = init_calendar();
  } catch (RuntimeError &e) {
    e.add_context("initializing the calendar");
    throw;
  }

  if (calendar == "360_day" || calendar == "365_day" || calendar == "noleap" || calendar == "none") {
    time = new Time(com, config, calendar, m_unit_system);
  } else {
    time = new Time_Calendar(com, config, calendar, m_unit_system);
  }
  // time->init() will be called later (in IceModel::set_grid_defaults() or
  // PIO::get_grid()).
}

/*! @brief Initialize a uniform, shallow (3 z-levels), doubly periodic grid
 * with half-widths (Lx,Ly) and Mx by My nodes.
 */
IceGrid::Ptr IceGrid::Shallow(MPI_Comm c, const Config &config,
                              double Lx, double Ly,
                              double x0, double y0,
                              unsigned int Mx, unsigned int My, Periodicity p) {

  std::vector<double> z(3, 0.0);
  z[1] = 0.5 * config.get("grid_Lz");
  z[2] = 1.0 * config.get("grid_Lz");

  return IceGrid::Create(c, config, Lx, Ly, x0, y0, z, Mx, My, p);
}

IceGrid::Ptr IceGrid::Create(MPI_Comm c, const Config &config,
                             double Lx, double Ly,
                             double x0, double y0,
                             const std::vector<double> &z,
                             unsigned int Mx, unsigned int My,
                             Periodicity p) {

  Ptr result(new IceGrid(c, config));

  result->set_size_and_extent(x0, y0, Lx, Ly, Mx, My, p);
  result->set_vertical_levels(z);

  result->allocate();

  return result;
}

IceGrid::Ptr IceGrid::Create(MPI_Comm c, const Config &config) {
  // use defaults from config

  Ptr result(new IceGrid(c, config));

  SpacingType spacing = EQUAL;
  std::string word = config.get_string("grid_ice_vertical_spacing");
  if (word == "quadratic") {
    spacing = QUADRATIC;
  } else if (word == "equal") {
    spacing = EQUAL;
  } else {
    throw RuntimeError::formatted("ice vertical spacing type '%s' is invalid.",
                                  word.c_str());
  }

  result->set_vertical_levels(config.get("grid_Lz"),
                              config.get("grid_Mz"),
                              spacing);

  result->compute_nprocs();
  result->compute_ownership_ranges();
  result->allocate();

  return result;
}

/**
 * Select a calendar using the "calendar" configuration parameter, the
 * "-calendar" command-line option, or the "calendar" attribute of the
 * "time" variable in the file specified using "-time_file".
 *
 * @param[out] result selected calendar string
 *
 * @return 0 on success
 */
std::string IceGrid::init_calendar() {
  // Set the default calendar using the config. parameter or the
  // "-calendar" option:
  std::string result = config.get_string("calendar");

  // Check if -time_file was set and override the setting above if the
  // "calendar" attribute is found.
  std::string time_file_name;
  bool time_file_set;
  OptionsString("-time_file", "name of the file specifying the run duration",
                time_file_name, time_file_set);
  if (time_file_set) {
    PIO nc(*this, "netcdf3");    // OK to use netcdf3

    nc.open(time_file_name, PISM_READONLY);
    {
      std::string time_name = config.get_string("time_dimension_name");
      bool time_exists = nc.inq_var(time_name);
      if (time_exists) {
        std::string tmp = nc.get_att_text(time_name, "calendar");
        if (tmp.empty() == false) {
          result = tmp;
        }
      }
    }
    nc.close();
  }
  return result;
}

IceGrid::~IceGrid() {
  delete time;
}

//! \brief Set the vertical levels in the ice according to values in Mz, Lz,
//! and the ice_vertical_spacing data member.
/*!
Uses `Mz`, `Lz`, and `ice_vertical_spacing`.  (Note that `ice_vertical_spacing`
cannot be UNKNOWN.)

This procedure is only called when a grid is determined from scratch, %e.g.
by a derived class or when bootstrapping from 2D data only, but not when
reading a model state input file (which will have its own grid,
which may not even be a grid created by this routine).
  - When `vertical_spacing` == EQUAL, the vertical grid in the ice is equally spaced:
    `zlevels[k] = k dz` where `dz = Lz / (Mz - 1)`.
  - When `vertical_spacing` == QUADRATIC, the spacing is a quadratic function.  The intent
    is that the spacing is smaller near the base than near the top.  In particular, if
    \f$\zeta_k = k / (\mathtt{Mz} - 1)\f$ then `zlevels[k] = Lz *
    ((\f$\zeta_k\f$ / \f$\lambda\f$) * (1.0 + (\f$\lambda\f$ - 1.0)
    * \f$\zeta_k\f$))` where \f$\lambda\f$ = 4.  The value \f$\lambda\f$
    indicates the slope of the quadratic function as it leaves the base.
    Thus a value of \f$\lambda\f$ = 4 makes the spacing about four times finer
    at the base than equal spacing would be.
 */
void IceGrid::set_vertical_levels(double Lz, unsigned int Mz,
                                  SpacingType spacing) {

  double lambda = config.get("grid_lambda");

  if (Mz < 2) {
    throw RuntimeError("IceGrid::set_vertical_levels(): Mz must be at least 2.");
  }

  if (Lz <= 0) {
    throw RuntimeError("IceGrid::set_vertical_levels(): Lz must be positive.");
  }

  if (spacing == QUADRATIC and lambda <= 0) {
    throw RuntimeError("IceGrid::set_vertical_levels(): lambda must be positive.");
  }

  m_z.resize(Mz);

  // Fill the levels in the ice:
  switch (spacing) {
  case EQUAL: {
    double dz = Lz / ((double) Mz - 1);

    // Equal spacing
    for (unsigned int k=0; k < Mz - 1; k++) {
      m_z[k] = dz * ((double) k);
    }
    m_z[Mz - 1] = Lz;  // make sure it is exactly equal
    break;
  }
  case QUADRATIC: {
    // this quadratic scheme is an attempt to be less extreme in the fineness near the base.
    for (unsigned int k=0; k < Mz - 1; k++) {
      const double zeta = ((double) k) / ((double) Mz - 1);
      m_z[k] = Lz * ((zeta / lambda) * (1.0 + (lambda - 1.0) * zeta));
    }
    m_z[Mz - 1] = Lz;  // make sure it is exactly equal
    break;
  }
  default:
    throw RuntimeError("IceGrid::set_vertical_levels(): spacing can not be UNKNOWN.");
  }
}


//! Return the index `k` into `zlevels[]` so that `zlevels[k] <= height < zlevels[k+1]` and `k < Mz`.
unsigned int IceGrid::kBelowHeight(double height) {
  if (height < 0.0 - 1.0e-6) {
    PetscPrintf(PETSC_COMM_SELF,
                "IceGrid kBelowHeight(), rank %d, height = %5.4f is below base of ice (height must be non-negative)\n", m_rank, height);
    MPI_Abort(PETSC_COMM_WORLD, 1);
  }
  if (height > Lz() + 1.0e-6) {
    PetscPrintf(PETSC_COMM_SELF,
                "IceGrid kBelowHeight(): rank %d, height = %5.4f is above top of computational grid Lz = %5.4f\n", m_rank, height, Lz());
    MPI_Abort(PETSC_COMM_WORLD, 1);
  }

  unsigned int mcurr = 0;
  while (m_z[mcurr+1] < height) {
    mcurr++;
  }
  return mcurr;
}

//! \brief Computes the number of processors in the X- and Y-directions.
void IceGrid::compute_nprocs() {

  if (m_My <= 0) {
    throw RuntimeError("'My' is invalid.");
  }

  m_Nx = (int)(0.5 + sqrt(((double)m_Mx)*((double)m_size)/((double)m_My)));

  if (m_Nx == 0) {
    m_Nx = 1;
  }

  while (m_Nx > 0) {
    m_Ny = m_size/m_Nx;
    if (m_Nx*m_Ny == (unsigned int)m_size) {
      break;
    }
    m_Nx--;
  }

  if (m_Mx > m_My && m_Nx < m_Ny) {int _Nx = m_Nx; m_Nx = m_Ny; m_Ny = _Nx;}

  if ((m_Mx / m_Nx) < 2) {          // note: integer division
    throw RuntimeError::formatted("Can't distribute a %d x %d grid across %d processors!",
                                  m_Mx, m_My, m_size);
  }

  if ((m_My / m_Ny) < 2) {          // note: integer division
    throw RuntimeError::formatted("Can't distribute a %d x %d grid across %d processors!",
                                  m_Mx, m_My, m_size);
  }
}

//! \brief Computes processor ownership ranges corresponding to equal area
//! distribution among processors.
/*!
 * Expects grid.Nx and grid.Ny to be valid.
 */
void IceGrid::compute_ownership_ranges() {

  m_procs_x.resize(m_Nx);
  m_procs_y.resize(m_Ny);

  for (unsigned int i=0; i < m_Nx; i++) {
    m_procs_x[i] = m_Mx/m_Nx + ((m_Mx % m_Nx) > i);
  }

  for (unsigned int i=0; i < m_Ny; i++) {
    m_procs_y[i] = m_My/m_Ny + ((m_My % m_Ny) > i);
  }
}

void IceGrid::ownership_ranges_from_options() {
  bool Nx_set, Ny_set;
  int Nx = m_Nx, Ny = m_Ny;
  OptionsInt("-Nx", "Number of processors in the x direction",
             Nx, Nx_set);
  OptionsInt("-Ny", "Number of processors in the y direction",
             Ny, Ny_set);
  m_Nx = Nx;
  m_Ny = Ny;

  if (Nx_set ^ Ny_set) {
    throw RuntimeError("Please set both -Nx and -Ny.");
  }

  if ((!Nx_set) && (!Ny_set)) {
    compute_nprocs();
    compute_ownership_ranges();
  } else {

    if ((Mx() / Nx) < 2) {
      throw RuntimeError::formatted("Can't split %d grid points between %d processors.",
                                    Mx(), Nx);
    }

    if ((My() / Ny) < 2) {
      throw RuntimeError::formatted("Can't split %d grid points between %d processors.",
                                    My(), Ny);
    }

    if (Nx * Ny != m_size) {
      throw RuntimeError::formatted("Nx * Ny has to be equal to %d.",
                                    m_size);
    }

    bool procs_x_set, procs_y_set;
    std::vector<int> tmp_x, tmp_y;
    OptionsIntArray("-procs_x", "Processor ownership ranges (x direction)",
                    tmp_x, procs_x_set);
    OptionsIntArray("-procs_y", "Processor ownership ranges (y direction)",
                    tmp_y, procs_y_set);

    if (procs_x_set ^ procs_y_set) {
      throw RuntimeError("Please set both -procs_x and -procs_y.");
    }

    if (procs_x_set && procs_y_set) {
      if (tmp_x.size() != (unsigned int)Nx) {
        throw RuntimeError("-Nx has to be equal to the -procs_x size.");
      }

      if (tmp_y.size() != (unsigned int)Ny) {
        throw RuntimeError("-Ny has to be equal to the -procs_y size.");
      }

      m_procs_x.resize(Nx);
      m_procs_y.resize(Ny);

      for (int j=0; j < Nx; j++) {
        m_procs_x[j] = tmp_x[j];
      }

      for (int j=0; j < Ny; j++) {
        m_procs_y[j] = tmp_y[j];
      }
    } else {
      compute_ownership_ranges();
    }
  } // -Nx and -Ny set
}

//! \brief Create the PETSc DM for the horizontal grid. Determine how
//! the horizontal grid is divided among processors.
/*!
  This procedure should only be called after the parameters describing the
  horizontal computational box (Lx,Ly) and the parameters for the horizontal
  grid (Mx,My) are already determined. In particular, the input file (either \c
  -i or `-boot_file`) and user options (like `-Mx`) must have already been
  read to determine the parameters, and any conflicts must have been resolved.

  This method contains the "fundamental" transpose: "My,Mx" instead of "Mx,My"
  in the DMDACreate2d call; this transpose allows us to index arrays by "[i][j]"
  (where 'i' corresponds to 'x' and 'j' to 'y') and be consistent about
  meanings of 'x', 'y', 'u' and 'v'.

  Unfortunately this means that PETSc viewers appear transposed.

  This choice should be virtually invisible, unless you're using DALocalInfo
  structures.

  \note PETSc order: x in columns, y in rows, indexing as array[y][x]. PISM
  order: x in rows, y in columns, indexing as array[x][y].
 */
void IceGrid::allocate() {

  check_parameters();

  compute_horizontal_spacing();

  ownership_ranges_from_options();

  unsigned int max_stencil_width = (unsigned int)config.get("grid_max_stencil_width");

  try {
    PISMDM::Ptr tmp = this->get_dm(1, max_stencil_width);
  } catch (RuntimeError) {
    throw RuntimeError::formatted("can't distribute the %d x %d grid across %d processors.",
                                  m_Mx, m_My, m_size);
  }

  // hold on to a DM corresponding to dof=1, stencil_width=0 (it will
  // be needed for I/O operations)
  m_dm_scalar_global = this->get_dm(1, 0);

  DMDALocalInfo info;
  DMDAGetLocalInfo(*m_dm_scalar_global, &info);
  // this continues the fundamental transpose
  m_xs = info.ys;
  m_xm = info.ym;
  m_ys = info.xs;
  m_ym = info.xm;
}

//! Sets grid vertical levels; sets Mz and Lz from input.  Checks input for consistency.
void IceGrid::set_vertical_levels(const std::vector<double> &new_zlevels) {

  if (new_zlevels.size() < 2) {
    throw RuntimeError("IceGrid::set_vertical_levels(): Mz has to be at least 2.");
  }

  if ((not is_increasing(new_zlevels)) || (fabs(new_zlevels[0]) > 1.0e-10)) {
    throw RuntimeError("IceGrid::set_vertical_levels(): invalid zlevels; must be strictly increasing and start with z=0.");
  }

  m_z = new_zlevels;
}

void IceGrid::set_size_and_extent(double x0, double y0, double Lx, double Ly,
                                  unsigned int Mx, unsigned int My, Periodicity p) {
  set_size(Mx, My);
  set_extent(x0, y0, Lx, Ly);
  set_periodicity(p);
}

void IceGrid::set_extent(double x0, double y0, double Lx, double Ly) {
  m_x0 = x0;
  m_y0 = y0;
  m_Lx = Lx;
  m_Ly = Ly;
}

void IceGrid::set_size(unsigned int Mx, unsigned int My) {
  m_Mx = Mx;
  m_My = My;
}

//! Compute horizontal spacing parameters `dx` and `dy` using `Mx`, `My`, `Lx`, `Ly` and periodicity.
/*!
The grid used in PISM, in particular the PETSc DAs used here, are periodic in x and y.
This means that the ghosted values ` foo[i+1][j], foo[i-1][j], foo[i][j+1], foo[i][j-1]`
for all 2D Vecs, and similarly in the x and y directions for 3D Vecs, are always available.
That is, they are available even if i,j is a point at the edge of the grid.  On the other
hand, by default, `dx`  is the full width  `2 * Lx`  divided by  `Mx - 1`.
This means that we conceive of the computational domain as starting at the `i = 0`
grid location and ending at the  `i = Mx - 1`  grid location, in particular.
This idea is not quite compatible with the periodic nature of the grid.

The upshot is that if one computes in a truly periodic way then the gap between the
`i = 0`  and  `i = Mx - 1`  grid points should \em also have width  `dx`.
Thus we compute  `dx = 2 * Lx / Mx`.
 */
void IceGrid::compute_horizontal_spacing() {

  if (m_periodicity & X_PERIODIC) {
    m_dx = 2.0 * m_Lx / m_Mx;
  } else {
    m_dx = 2.0 * m_Lx / (m_Mx - 1);
  }

  if (m_periodicity & Y_PERIODIC) {
    m_dy = 2.0 * m_Ly / m_My;
  } else {
    m_dy = 2.0 * m_Ly / (m_My - 1);
  }

  compute_horizontal_coordinates();
}

//! \brief Computes values of x and y corresponding to the computational grid,
//! with accounting for periodicity.
void IceGrid::compute_horizontal_coordinates() {

  m_x.resize(m_Mx);
  m_y.resize(m_My);

  // Here x_min, x_max define the extent of the computational domain,
  // which is not necessarily the same thing as the smallest and
  // largest values of x.
  double
    x_min = m_x0 - m_Lx,
    x_max = m_x0 + m_Lx;
  if (m_periodicity & X_PERIODIC) {
    for (unsigned int i = 0; i < m_Mx; ++i) {
      m_x[i] = x_min + (i + 0.5) * m_dx;
    }
    m_x[m_Mx - 1] = x_max - 0.5*m_dx;
  } else {
    for (unsigned int i = 0; i < m_Mx; ++i) {
      m_x[i] = x_min + i * m_dx;
    }
    m_x[m_Mx - 1] = x_max;
  }

  double
    y_min = m_y0 - m_Ly,
    y_max = m_y0 + m_Ly;
  if (m_periodicity & Y_PERIODIC) {
    for (unsigned int i = 0; i < m_My; ++i) {
      m_y[i] = y_min + (i + 0.5) * m_dy;
    }
    m_y[m_My - 1] = y_max - 0.5*m_dy;
  } else {
    for (unsigned int i = 0; i < m_My; ++i) {
      m_y[i] = y_min + i * m_dy;
    }
    m_y[m_My - 1] = y_max;
  }
}

bool IceGrid::is_equally_spaced() const {
  // decide if we're going to use linear or quadratic interpolation
  if (fabs(dz_max() - dz_min()) <= 1.0e-8) {
    return true;
  } else {
    return false;
  }
}

//! \brief Report grid parameters.
void IceGrid::report_parameters() const {

  verbPrintf(2, com, "computational domain and grid:\n");

  // report on grid
  verbPrintf(2, com,
             "                grid size   %d x %d x %d\n",
             m_Mx, m_My, Mz());

  // report on computational box
  verbPrintf(2, com,
             "           spatial domain   %.2f km x %.2f km x %.2f m\n",
             2*m_Lx/1000.0, 2*m_Ly/1000.0, Lz());

  // report on grid cell dims
  verbPrintf(2, com,
             "     horizontal grid cell   %.2f km x %.2f km\n",
             m_dx/1000.0, m_dy/1000.0);

  if (is_equally_spaced()) {
    verbPrintf(2, com,
               "  vertical spacing in ice   dz = %.3f m (equal spacing)\n",
               dz_min());
  } else {
    verbPrintf(2, com,
               "  vertical spacing in ice   uneven, %d levels, %.3f m < dz < %.3f m\n",
               Mz(), dz_min(), dz_max());
  }

  // report on time axis
  //   FIXME:  this could use pism_config:summary_time_unit_name instead of fixed "years"
  verbPrintf(2, com,
             "   time interval (length)   [%s, %s]  (%s years, using the '%s' calendar)\n",
             time->start_date().c_str(), time->end_date().c_str(),
             time->run_length().c_str(), time->calendar().c_str());

  // if -verbose (=-verbose 3) then (somewhat redundantly) list parameters of grid
  {
    verbPrintf(3, com,
               "  IceGrid parameters:\n");
    verbPrintf(3, com,
               "            Lx = %6.2f km, Ly = %6.2f km, Lz = %6.2f m, \n",
               m_Lx/1000.0, m_Ly/1000.0, Lz());
    verbPrintf(3, com,
               "            x0 = %6.2f km, y0 = %6.2f km, (coordinates of center)\n",
               m_x0/1000.0, m_y0/1000.0);
    verbPrintf(3, com,
               "            Mx = %d, My = %d, Mz = %d, \n",
               m_Mx, m_My, Mz());
    verbPrintf(3, com,
               "            dx = %6.3f km, dy = %6.3f km, year = %s, \n",
               m_dx/1000.0, m_dy/1000.0, time->date().c_str());
    verbPrintf(3, com,
               "            Nx = %d, Ny = %d]\n",
               m_Nx, m_Ny);

  }

  {
    verbPrintf(5, com,
               "  REALLY verbose output on IceGrid:\n");
    verbPrintf(5, com,
               "    vertical levels in ice (Mz=%d, Lz=%5.4f): ", Mz(), Lz());
    for (unsigned int k=0; k < Mz(); k++) {
      verbPrintf(5, com, " %5.4f, ", m_z[k]);
    }
    verbPrintf(5, com, "\n");
  }
}


//! \brief Computes indices of grid points to the lower left and upper right from (X,Y).
/*!
 * \code
 * 3       2
 * o-------o
 * |       |
 * |    +  |
 * o-------o
 * 0       1
 * \endcode
 *
 * If "+" is the point (X,Y), then (i_left, j_bottom) corresponds to
 * point "0" and (i_right, j_top) corresponds to point "2".
 *
 * Does not check if the resulting indexes are in the current
 * processor's domain. Ensures that computed indexes are within the
 * grid.
 */
void IceGrid::compute_point_neighbors(double X, double Y,
                                      int &i_left, int &i_right,
                                      int &j_bottom, int &j_top) {
  i_left = (int)floor((X - m_x[0])/m_dx);
  j_bottom = (int)floor((Y - m_y[0])/m_dy);

  i_right = i_left + 1;
  j_top = j_bottom + 1;

  if (i_left < 0) {
    i_left = i_right;
  }

  if (i_right > (int)m_Mx - 1) {
    i_right = i_left;
  }

  if (j_bottom < 0) {
    j_bottom = j_top;
  }

  if (j_top > (int)m_My - 1) {
    j_top = j_bottom;
  }
}

//! \brief Compute 4 interpolation weights necessary for linear interpolation
//! from the current grid. See compute_point_neighbors for the ordering of
//! neighbors.
std::vector<double> IceGrid::compute_interp_weights(double X, double Y) {
  int i_left = 0, i_right = 0, j_bottom = 0, j_top = 0;
  // these values (zeros) are used when interpolation is impossible
  double alpha = 0.0, beta = 0.0;

  compute_point_neighbors(X, Y, i_left, i_right, j_bottom, j_top);

  if (i_left != i_right) {
    assert(m_x[i_right] - m_x[i_left] != 0.0);
    alpha = (X - m_x[i_left]) / (m_x[i_right] - m_x[i_left]);
  }

  if (j_bottom != j_top) {
    assert(m_y[j_top] - m_y[j_bottom] != 0.0);
    beta  = (Y - m_x[j_bottom]) / (m_y[j_top] - m_y[j_bottom]);
  }

  std::vector<double> result(4);
  result[0] = alpha * beta;
  result[1] = (1 - alpha) * beta;
  result[2] = (1 - alpha) * (1 - beta);
  result[3] = alpha * (1 - beta);

  return result;
}

//! \brief Checks grid parameters usually set at bootstrapping for validity.
void IceGrid::check_parameters() {

  if (m_Mx < 3) {
    throw RuntimeError("Mx has to be at least 3.");
  }

  if (m_My < 3) {
    throw RuntimeError("My has to be at least 3.");
  }

  if (Mz() < 2) {
    throw RuntimeError("Mz must be at least 2.");
  }

  if (m_Lx <= 0) {
    throw RuntimeError("Lx has to be positive.");
  }

  if (m_Ly <= 0) {
    throw RuntimeError("Ly has to be positive.");
  }

  if (Lz() <= 0) {
    throw RuntimeError("Lz must be positive.");
  }

  // A single record of a time-dependent variable cannot exceed 2^32-4
  // bytes in size. See the NetCDF User's Guide
  // <http://www.unidata.ucar.edu/software/netcdf/docs/netcdf.html#g_t64-bit-Offset-Limitations>.
  // Here we use "long int" to avoid integer overflow.
  const long int two_to_thirty_two = 4294967296L;
  const long int Mx_long = m_Mx, My_long = m_My, Mz_long = Mz();
  if (Mx_long * My_long * Mz_long * sizeof(double) > two_to_thirty_two - 4 &&
      ((config.get_string("output_format") == "netcdf3") ||
       (config.get_string("output_format") == "pnetcdf"))) {
    throw RuntimeError::formatted("The computational grid is too big to fit in a NetCDF-3 file.\n"
                                  "Each 3D variable requires %lu Mb.\n"
                                  "Please use '-o_format quilt' or re-build PISM with parallel NetCDF-4 or HDF5\n"
                                  "and use '-o_format netcdf4_parallel' or '-o_format hdf5' to proceed.",
                                  Mx_long * My_long * Mz_long * sizeof(double) / (1024 * 1024));
  }

}

PISMDM::Ptr IceGrid::get_dm(int da_dof, int stencil_width) {
  PISMDM::Ptr result;

  if (da_dof < 0 || da_dof > 10000) {
    throw RuntimeError::formatted("Invalid da_dof argument: %d", da_dof);
  }

  if (stencil_width < 0 || stencil_width > 10000) {
    throw RuntimeError::formatted("Invalid stencil_width argument: %d", stencil_width);
  }

  int j = this->dm_key(da_dof, stencil_width);

  if (m_dms[j].expired() == true) {
    DM tmp = this->create_dm(da_dof, stencil_width);

    result = PISMDM::Ptr(new PISMDM(tmp));
    m_dms[j] = result;
  } else {
    result = m_dms[j].lock();
  }

  return result;
}

UnitSystem IceGrid::get_unit_system() const {
  return m_unit_system;
}

Periodicity IceGrid::periodicity() const {
  return m_periodicity;
}

void IceGrid::set_periodicity(Periodicity p) {
  m_periodicity = p;
}

double IceGrid::convert(double value, const std::string &unit1, const std::string &unit2) const {
  return m_unit_system.convert(value, unit1, unit2);
}

DM IceGrid::create_dm(int da_dof, int stencil_width) {
  DM result;

  verbPrintf(3, com,
             "* Creating a DM with dof=%d and stencil_width=%d...\n",
             da_dof, stencil_width);

  // PetscInt and int may have different sizes, so here we make copies
  // of m_procs_x and m_procs_y. We could store m_procs_[xy] using
  // PetscInt, but that leaks this implementation detail in the header
  // defining IceGrid.
  std::vector<PetscInt> procs_x(m_procs_x.size()), procs_y(m_procs_y.size());

  for (unsigned int k = 0; k < procs_x.size(); ++k) {
    procs_x[k] = m_procs_x[k];
  }

  for (unsigned int k = 0; k < procs_y.size(); ++k) {
    procs_y[k] = m_procs_y[k];
  }

  PetscErrorCode ierr = DMDACreate2d(com,
#if PETSC_VERSION_LT(3,5,0)
                      DMDA_BOUNDARY_PERIODIC, DMDA_BOUNDARY_PERIODIC,
#else
                      DM_BOUNDARY_PERIODIC, DM_BOUNDARY_PERIODIC,
#endif
                      DMDA_STENCIL_BOX,
                      m_My, m_Mx, // N, M
                      m_Ny, m_Nx, // n, m
                      da_dof, stencil_width,
                      &procs_y[0], &procs_x[0], // ly, lx
                      &result);
  PISM_PETSC_CHK(ierr,"DMDACreate2d");

  return result;
}

// Computes the key corresponding to the DM with given dof and stencil_width.
int IceGrid::dm_key(int da_dof, int stencil_width) {
  return 10000 * da_dof + stencil_width;
}

int IceGrid::rank() const {
  return m_rank;
}

unsigned int IceGrid::size() const {
  return m_size;
}

int IceGrid::xs() const {
  return m_xs;
}

int IceGrid::ys() const {
  return m_ys;
}

int IceGrid::xm() const {
  return m_xm;
}

int IceGrid::ym() const {
  return m_ym;
}

unsigned int IceGrid::Mx() const {
  return m_Mx;
}

unsigned int IceGrid::My() const {
  return m_My;
}

unsigned int IceGrid::Mz() const {
  return m_z.size();
}

const std::vector<double>& IceGrid::x() const {
  return m_x;
}

double IceGrid::x(size_t i) const {
  return m_x[i];
}

const std::vector<double>& IceGrid::y() const {
  return m_y;
}

double IceGrid::y(size_t i) const {
  return m_y[i];
}

const std::vector<double>& IceGrid::z() const {
  return m_z;
}

double IceGrid::z(size_t i) const {
  return m_z[i];
}

double IceGrid::dx() const {
  return m_dx;
}

double IceGrid::dy() const {
  return m_dy;
}

double IceGrid::dz_min() const {
  double dz_min = m_z.back();
  for (unsigned int k = 0; k < m_z.size() - 1; ++k) {
    const double dz = m_z[k + 1] - m_z[k];
    dz_min = std::min(dz, dz_min);
  }
  return dz_min;
}

double IceGrid::dz_max() const {
  double dz_max = 0.0;
  for (unsigned int k = 0; k < m_z.size() - 1; ++k) {
    const double dz = m_z[k + 1] - m_z[k];
    dz_max = std::max(dz, dz_max);
  }
  return dz_max;
}

double IceGrid::Lx() const {
  return m_Lx;
}

double IceGrid::Ly() const {
  return m_Ly;
}

double IceGrid::Lz() const {
  return m_z.back();
}

double IceGrid::x0() const {
  return m_x0;
}

double IceGrid::y0() const {
  return m_y0;
}

//! \brief Returns the distance from the point (i,j) to the origin.
double radius(const IceGrid &grid, int i, int j) {
  return sqrt(grid.x(i) * grid.x(i) + grid.y(j) * grid.y(j));
}

} // end of namespace pism
