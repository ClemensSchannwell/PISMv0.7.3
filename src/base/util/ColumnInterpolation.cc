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

#include "ColumnInterpolation.hh"

#include <cmath>

namespace pism {

ColumnInterpolation::ColumnInterpolation(std::vector<double> z_coarse)
  : m_z_coarse(z_coarse) {
  init_fine_grid();
  init_interpolation();
}

std::vector<double> ColumnInterpolation::coarse_to_fine(const std::vector<double> &input,
                                                        unsigned int ks) const {
  std::vector<double> result(Mz_fine());
  coarse_to_fine(&input[0], ks, &result[0]);
  return result;
}

void ColumnInterpolation::coarse_to_fine(const double *input, unsigned int ks, double *result) const {
  if (m_use_linear_interpolation) {
    coarse_to_fine_linear(input, ks, result);
  } else {
    coarse_to_fine_quadratic(input, ks, result);
  }
}

void ColumnInterpolation::coarse_to_fine_linear(const double *input, unsigned int ks,
                                                double *result) const {
  for (unsigned int k = 0; k < Mz_fine(); k++) {
    if (k > ks) {
      result[k] = input[m_coarse2fine[k]];
      continue;
    }

    unsigned int m = m_coarse2fine[k];

    // extrapolate (if necessary):
    if (m == Mz() - 1) {
      result[k] = input[Mz() - 1];
      continue;
    }

    const double incr = (m_z_fine[k] - m_z_coarse[m]) / (m_z_coarse[m + 1] - m_z_coarse[m]);
    result[k] = input[m] + incr * (input[m + 1] - input[m]);
  }
}

void ColumnInterpolation::coarse_to_fine_quadratic(const double *input, unsigned int ks,
                                                   double *result) const {
  unsigned int k = 0, m = 0;
  for (m = 0; m < Mz() - 2; m++) {
    if (k > ks) {
      break;
    }

    const double
      z0 = m_z_coarse[m],
      z1 = m_z_coarse[m+1],
      z2 = m_z_coarse[m+2],
      f0 = input[m],
      f1 = input[m+1],
      f2 = input[m+2];

    const double
      d1 = (f1 - f0) / (z1 - z0),
      d2 = (f2 - f0) / (z2 - z0),
      b  = (d2 - d1) / (z2 - z1),
      a  = d1 - b * (z1 - z0),
      c  = f0;

    while (m_z_fine[k] < z1) {
      if (k > ks) {
        break;
      }

      const double s = m_z_fine[k] - z0;

      result[k] = s * (a + b * s) + c;

      k++;
    }
  } // m-loop

  // check if we got to the end of the m-loop and use linear
  // interpolation between the remaining 2 coarse levels
  if (m == Mz() - 2) {
    const double
      z0 = m_z_coarse[m],
      z1 = m_z_coarse[m+1],
      f0 = input[m],
      f1 = input[m+1],
      lambda = (f1 - f0) / (z1 - z0);

    while (m_z_fine[k] < z1) {
      result[k] = f0 + lambda * (m_z_fine[k] - z0);

      k++;
    }
  }

  // fill the rest using constant extrapolation
  const double f0 = input[Mz() - 1];
  while (k <= ks) {
    result[k] = f0;
    k++;
  }
}

std::vector<double> ColumnInterpolation::fine_to_coarse(const std::vector<double> &input) const {
  std::vector<double> result(Mz());
  fine_to_coarse(&input[0], &result[0]);
  return result;
}

void ColumnInterpolation::fine_to_coarse(const double *input, double *result) const {
  const unsigned int N = Mz();

  for (unsigned int k = 0; k < N - 1; ++k) {
    const int m = m_fine2coarse[k];

    const double increment = (m_z_coarse[k] - m_z_fine[m]) / (m_z_fine[m + 1] - m_z_fine[m]);
    result[k] = input[m] + increment * (input[m + 1] - input[m]);
  }

  result[N - 1] = input[m_fine2coarse[N - 1]];
}

unsigned int ColumnInterpolation::Mz() const {
  return m_z_coarse.size();
}

unsigned int ColumnInterpolation::Mz_fine() const {
  return m_z_fine.size();
}

double ColumnInterpolation::dz_fine() const {
  return m_z_fine[1] - m_z_fine[0];
}

void ColumnInterpolation::init_fine_grid() {
  // Compute dz_fine as the minimum vertical spacing in the coarse
  // grid:
  unsigned int Mz = m_z_coarse.size();
  double Lz = m_z_coarse.back(), dz_fine = Lz;
  for (unsigned int k = 1; k < Mz; ++k) {
    dz_fine = std::min(dz_fine, m_z_coarse[k] - m_z_coarse[k-1]);
  }

  size_t Mz_fine = static_cast<size_t>(ceil(Lz / dz_fine) + 1);
  dz_fine = Lz / (Mz_fine - 1);

  m_z_fine.resize(Mz_fine);
  // compute levels of the fine grid:
  for (unsigned int k = 0; k < Mz_fine; k++) {
    m_z_fine[k] = m_z_coarse[0] + k * dz_fine;
  }
  // Note that it's allowed to go over Lz.
}

void ColumnInterpolation::init_interpolation() {
  unsigned int m = 0;
  double Lz = m_z_coarse.back();

  // coarse -> fine
  m_coarse2fine.resize(Mz_fine());
  m = 0;
  for (unsigned int k = 0; k < Mz_fine(); k++) {
    if (m_z_fine[k] >= Lz) {
      m_coarse2fine[k] = Mz() - 1;
      continue;
    }

    while (m_z_coarse[m + 1] < m_z_fine[k]) {
      m++;
    }

    m_coarse2fine[k] = m;
  }

  // fine -> coarse
  m_fine2coarse.resize(Mz());
  m = 0;
  for (unsigned int k = 0; k < Mz(); k++) {
    while (m < Mz_fine() - 1 &&
           m_z_fine[m + 1] < m_z_coarse[k]) {
      m++;
    }

    m_fine2coarse[k] = m;
  }

  // decide if we're going to use linear or quadratic interpolation
  double dz_min = Lz;
  double dz_max = 0.0;
  for (unsigned int k = 0; k < Mz() - 1; k++) {
    const double dz = m_z_coarse[k+1] - m_z_coarse[k];
    dz_min = std::min(dz, dz_min);
    dz_max = std::max(dz, dz_max);
  }

  if (fabs(dz_max - dz_min) <= 1.0e-8) {
    m_use_linear_interpolation = true;
  } else {
    m_use_linear_interpolation = false;
  }

}

} // end of namespace pism
