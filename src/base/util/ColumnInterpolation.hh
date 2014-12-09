/* Copyright (C) 2014 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _COLUMNINTERPOLATION_H_
#define _COLUMNINTERPOLATION_H_

#include <vector>

namespace pism {

class ColumnInterpolation {
public:
  ColumnInterpolation(std::vector<double> coarse);

  void coarse_to_fine(double *coarse, unsigned int ks, double *fine) const;
  void fine_to_coarse(double *fine, double *coarse) const;

  unsigned int Mz() const;

  unsigned int Mz_fine() const;
  double dz_fine() const;
private:
  std::vector<double> m_z_fine, m_z_coarse;

  // Array m_coarse2fine contains indices of the ice coarse vertical grid
  // that are just below a level of the fine grid. I.e. m_coarse2fine[k] is
  // the coarse grid level just below fine-grid level k (zlevels_fine[k]).
  // Similarly for other arrays below.
  std::vector<unsigned int> m_coarse2fine, m_fine2coarse;
  bool m_use_linear_interpolation;

  void init_fine_grid();
  void init_interpolation();
  void coarse_to_fine_linear(double *coarse, unsigned int ks, double *fine) const;
  void coarse_to_fine_quadratic(double *coarse, unsigned int ks, double *fine) const;
};

} // end of namespace pism

#endif /* _COLUMNINTERPOLATION_H_ */
