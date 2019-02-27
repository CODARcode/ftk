#ifndef _FTK_EIGEN_SOLVER_HH
#define _FTK_EIGEN_SOLVER_HH

#include <ftk/numeric/trace.hh>
#include <ftk/numeric/det.hh>
#include <ftk/numeric/matrix_multiplication.hh>
#include <ftk/numeric/quadratic_solver.hh>
#include <ftk/numeric/cubic_solver.hh>
#include <ftk/numeric/quartic_solver.hh>
#include <ftk/numeric/vector_normalization.hh>
#include <ftk/numeric/characteristic_polynomial.hh>
#include <iostream>

namespace ftk {

template <typename T>
inline void solve_eigenvalues_real_symmetric2x2(T m00, T m10, T m11, T eig[2])
{
  const T b = -(m00 + m11), c = m00*m11 - m10*m10;
  // const T delta = b*b - 4*c;
  const T delta = std::fma(b, b, -4*c); 
  const T sqrt_delta = delta < 0 ? 0 : std::sqrt(delta); // in theory, delta should not be less than 0
  
  eig[0] = 0.5*(-b+sqrt_delta);
  eig[1] = 0.5*(-b-sqrt_delta);

  if (abs(eig[0]) < abs(eig[1]))
    std::swap(eig[0], eig[1]);
}

template <typename T>
inline void solve_eigenvalues_real_symmetric2x2(const T m[2][2], T eig[2])
{
  return solve_eigenvalues_real_symmetric2x2(m[0][0], m[1][0], m[1][1], eig);
}

template <typename T>
inline void solve_eigenvectors_real2x2(const T M[2][2], const T eig[2], T eigvecs[2][2])
{
  for (int i = 0; i < 2; i ++) {
    const T a[2] = {
      {M[0][0] - eig[i] - M[0][1]}, 
      {M[1][0] - M[1][1] + eig[i]}
    };
    const T b[2] = {-M[0][1], -M[1][1]};

    solve_least_square2x1(a, b, eigvecs[i][0]);
    eigvecs[i][1] = T(1) - eigvecs[i][0];
  }
}

template <typename T>
inline int solve_real_eigenvalues_real2x2(const T M[2][2], T eig[2])
{
  T P[3];
  characteristic_polynomial_2x2(M, P);
  return solve_quadratic_real(P, eig);
}

template <typename T>
inline void solve_generalized_eigenvalues_real2x2(const T A[2][2], const T B[2][2], std::complex<T> eig[2])
{
  T P[3];
  characteristic_polynomial_2x2(A, B, P);
  // characteristic_polynomial_2x2(A[0][0], A[0][1], A[1][0], A[1][1], B[0][0], B[0][1], B[1][0], B[1][1], P);
  solve_quadratic(P, eig);
}

template <typename T>
inline void solve_eigenvalues_real_symmetric3x3(const T A[3][3], T x[3]/*eig[3]*/)
{
  T P[4];
  characteristic_polynomial_3x3(A, P);
  const T b = P[2], c = P[1], d = P[0];

  T disc, q, r, dum1, s, t, term1, r13;

  q = (3.0*c - (b*b))/9.0; 
  r = (-(27.0*d) + b*(9.0*c - 2.0*(b*b)))/54.0;
  disc = q*q*q + r*r; // std::max(T(0), q*q*q + r*r); // enforcing disc to be less or equal than 0
  term1 = (b/3.0);

  if (disc >= T(0)) { // all roots real, at least two are equal.
    r13 = ((r < 0) ? -pow(-r,(1.0/3.0)) : pow(r,(1.0/3.0)));
    x[0] = -term1 + 2.0*r13;
    x[1] = -(r13 + term1);
    x[2] = -(r13 + term1);
  } else { // all roots are real and unequal
    q = -q;
    dum1 = q*q*q;
    dum1 = acos(r/sqrt(dum1));
    r13 = 2.0*sqrt(q);
    x[0] = -term1 + r13*cos(dum1/3.0);
    x[1] = -term1 + r13*cos((dum1 + 2.0*M_PI)/3.0);
    x[2] = -term1 + r13*cos((dum1 + 4.0*M_PI)/3.0);
  }
}

template <typename T>
inline void solve_generalized_eigenvalues_real3x3(const T A[3][3], const T B[3][3], std::complex<T> eig[3])
{
  T P[4];
  characteristic_polynomial_3x3(A, B, P);
  solve_cubic(P, eig);
}

/////////////////////////// legacy code
// compute eigenvector for a given matrix and one of its eigenvalue
template <typename T>
inline void eigvec3(const T m[9], std::complex<T> lambda, std::complex<T> v[3])
{
  std::complex<T> D = (m[0] - lambda) * (m[4] - lambda) - m[1] * m[3], 
    Dx = -m[2] * (m[4] - lambda) + m[1] * m[5],
    Dy = -m[5] * (m[0] - lambda) + m[2] * m[3];

  v[0] = Dx / D;
  v[1] = Dy / D;
  v[2] = std::complex<T>(T(1), T(0));

  vector_normalization2_3(v);
}

template <typename T>
inline void eigvec3(const T m[3][3], std::complex<T> lambda, std::complex<T> v[3])
{
  std::complex<T> D = (m[0][0] - lambda) * (m[1][1] - lambda) - m[0][1] * m[1][0], 
    Dx = -m[0][2] * (m[1][1] - lambda) + m[0][1] * m[1][2],
    Dy = -m[1][2] * (m[0][0] - lambda) + m[0][2] * m[1][0];

  v[0] = Dx / D;
  v[1] = Dy / D;
  v[2] = std::complex<T>(T(1), T(0));

  vector_normalization2_3(v);
}

template <typename T>
inline void eig2(const T m[2][2], std::complex<T> eig[2], std::complex<T> eigvec[2])
{
  const T b = -trace2(m), c = det2(m);
  quadratic_solver(b, c, eig);

  for (int i=0; i<2; i++) 
    eigvec2(m, eig[i], eigvec[i]);
}

template <typename T>
inline void eig3(const T m[9], std::complex<T> eig[3], std::complex<T> eigvec[3][3]) 
{
  const T b = -trace3(m),
          c = m[4]*m[8] + m[0]*m[8] + m[0]*m[4] - m[1]*m[3] - m[5]*m[7] - m[2]*m[6],
          d = -det3(m);

  solve_cubic(b, c, d, eig);

  for (int i=0; i<3; i++) 
    eigvec3(m, eig[i], eigvec[i]);
}

template <typename T>
inline void eig3(
    const T M[3][3], 
    std::complex<T> eig[3], 
    std::complex<T> eigvec[3][3]) // untested
{
#if 1
  T P[4];
  characteristic_polynomial_3x3(M, P);
  solve_cubic(P, eig); // b, c, d, eig);
#else
  const T b = -trace3(M),
          c = M[1][1]*M[2][2] + M[0][0]*M[2][2] + M[0][0]*M[1][1] - M[0][1]*M[1][0] - M[1][2]*M[2][1] - M[0][2]*M[2][0],
          d = -det3(M);
  solve_cubic(b, c, d, eig);
#endif

  for (int i=0; i<3; i++) 
    eigvec3(M, eig[i], eigvec[i]);
}

// compute eigenvector for a given matrix and one of its eigenvalue
template <typename T>
inline void eigvec4(const T m[16], std::complex<T> lambda, std::complex<T> v[4])
{
  std::complex<T> D = det3<std::complex<T> >(m[0]-lambda, m[1], m[2], m[4], m[5]-lambda, m[6], m[8], m[9], m[10]-lambda), 
    Dx = det3<std::complex<T> >(-m[3], m[1], m[2], -m[7], m[5]-lambda, m[6], -m[11], m[9], m[10]-lambda), 
    Dy = det3<std::complex<T> >(m[0]-lambda, -m[3], m[2], m[4], -m[7], m[6], m[8], -m[11], m[10]-lambda), 
    Dz = det3<std::complex<T> >(m[0]-lambda, m[1], -m[3], m[4], m[5]-lambda, -m[7], m[8], m[9], -m[11]);

  v[0] = Dx / D;
  v[1] = Dy / D;
  v[2] = Dz / D;
  v[3] = std::complex<T>(T(1), T(0));

  vector_normalization2_4(v);
}

template <typename T>
inline void eig4(const T m[16], std::complex<T> eig[4], std::complex<T> eigvec[4][4]) 
{
  const T a0 = det4(m), 
                  a1 =  m[0]  * m[11] * m[14] - m[0]  * m[10] * m[15] - m[11] * m[12] * m[2] + m[10] * m[12] * m[3]
                      + m[1]  * m[10] * m[4]  + m[1]  * m[15] * m[4]  - m[13] * m[3]  * m[4] - m[0]  * m[10] * m[5]
                      + m[11] * m[14] * m[5]  - m[0]  * m[15] * m[5]  - m[10] * m[15] * m[5] + m[12] * m[3]  * m[5]
                      - m[11] * m[13] * m[6]  - m[1]  * m[12] * m[7]  + m[0]  * m[13] * m[7] + m[10] * m[13] * m[7]
                      + m[15] * m[2]  * m[8]  - m[14] * m[3]  * m[8]  + m[2]  * m[5]  * m[8] - m[1]  * m[6]  * m[8]
                      - m[2]  * m[4]  * m[9]  + m[0]  * m[6]  * m[9]  + m[15] * m[6]  * m[9] - m[14] * m[7]  * m[9],
                  a2 =  m[0]  * m[10] - m[11] * m[14] + m[0]  * m[15] + m[10] * m[15]
                      - m[12] * m[3]  - m[1]  * m[4]  + m[0]  * m[5]  + m[10] * m[5]
                      + m[15] * m[5]  - m[13] * m[7]  - m[2]  * m[8]  - m[6]  * m[9],
                  a3 = -trace4(m);

  quartic_solve(a3, a2, a1, a0, eig); 

  for (int i=0; i<4; i++)
    eigvec4(m, eig[i], eigvec[i]);
}

}

#endif
