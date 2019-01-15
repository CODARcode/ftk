#ifndef _FTK_PARALLEL_VECTOR_SOLVER_H
#define _FTK_PARALLEL_VECTOR_SOLVER_H

#include <ftk/numeric/matrix_inverse.hh>
#include <ftk/numeric/eigen_solver.hh>
#include <ftk/numeric/linear_solver.hh>
#include <ftk/numeric/linear_interpolation.hh>
#include <ftk/numeric/cross_product.hh>
#include <ftk/numeric/transpose.hh>
#include <ftk/numeric/characteristic_polynomial.hh>
#include <ftk/numeric/polynomial.hh>
#include <ftk/numeric/isnan.hh>
#include <ftk/numeric/print.hh>

namespace ftk {

template <typename T>
inline int solve_parallel_vector_barycentric(const T V[9], const T W[9], T lambda[3], T mu[9])
{
  T invV[9], invW[9];
  const auto detV = matrix_inverse3(V, invV),
             detW = matrix_inverse3(W, invW);

  if (isnan_mat3x3(invW) || isnan_mat3x3(invV)) return false;
  // if (detW < 1e-4) return false;

  T m[9];
  matrix_matrix_multiplication_3x3_3x3(invW, V, m);
  // if (detW > detV) mulmat3(invW, V, m); 
  // else mulmat3(invV, W, m); // very wrong...

  std::complex<T> eig[3], eigvec[3][3];
  eig3(m, eig, eigvec);

  // fprintf(stderr, "eigs_impl0: (%f, %f), (%f, %f), (%f, %f)\n", 
  //     eig[0].real(), eig[0].imag(), eig[1].real(), eig[1].imag(), eig[2].real(), eig[2].imag());

  int n_solutions = 0;
  for (int i=0; i<3; i++) {
    if (std::isnan(eig[i].real()) || eig[i].imag() != 0) continue; // non-real eigenvalue
    T l[3] = {eigvec[i][0].real(), eigvec[i][1].real(), eigvec[i][2].real()};
    if (l[0] < 0 || l[1] < 0) continue;
    const auto sum = l[0] + l[1] + l[2];
    mu[n_solutions*3+0] = l[0] / sum;
    mu[n_solutions*3+1] = l[1] / sum;
    mu[n_solutions*3+2] = l[2] / sum;
    lambda[n_solutions] = eig[i].real();
    if (std::isnan(mu[0]) || std::isnan(mu[1]) || std::isnan(mu[2])) continue;
    n_solutions ++;
    // return true;
  }

  // if (n_solutions)
  //   fprintf(stderr, "n_solutions=%d\n", n_solutions);
  return n_solutions;
  // return false;
}

#if 0
template <typename T>
inline int solve_parallel_vector_barycentric(const T V[3][3], const T W[3][3], T lambda[3], T mu[3][3])
{
  T VV[] = {V[0][0], V[0][1], V[0][2], V[1][0], V[1][1], V[1][2], V[2][0], V[2][1], V[2][2]};
  T WW[] = {W[0][0], W[0][1], W[0][2], W[1][0], W[1][1], W[1][2], W[2][0], W[2][1], W[2][2]};
  transpose3(VV);
  transpose3(WW);
  
  T mu1[9];
  const auto n = solve_parallel_vector_barycentric(VV, WW, lambda, mu1);
  for (int i = 0; i < n; i ++)
    for (int j = 0; j < 3; j ++)
      mu[i][j] = mu1[i*3+j];

  return n;
}
#else
template <typename T>
inline int solve_parallel_vector_barycentric(const T VV[3][3], const T WW[3][3], T lambda[3], T mu[3][3])
{
  T V[3][3], W[3][3]; // transposed V and W
  transpose3(VV, V);
  transpose3(WW, W);

  T P[4]; // characteristic polynomial
  characteristic_polynomial_3x3(V, W, P);

  T l[3];
  const int n_roots = solve_cubic_real(P, l);
  int n_solutions = 0;

  for (int i = 0; i < n_roots; i ++) {
    const T M[2][2] = {
      {(V[0][0] - V[0][2]) - l[i] * (W[0][0] - W[0][2]), (V[0][1] - V[0][2]) - l[i] * (W[0][1] - W[0][2])}, 
      {(V[1][0] - V[1][2]) - l[i] * (W[1][0] - W[1][2]), (V[1][1] - V[1][2]) - l[i] * (W[1][1] - W[1][2])}
    };
    const T b[2] = {
      -(V[0][2] - l[i]*W[0][2]), 
      -(V[1][2] - l[i]*W[1][2])
    };
    T nu[3];
    const auto det = solve_linear_real2x2(M, b, nu);
    nu[2] = T(1) - nu[0] - nu[1];
    
    // fprintf(stderr, "lambda=%f, nu={%f, %f, %f}, det=%f\n", l[i], nu[0], nu[1], nu[2], det);
    // print2x2("M", M);
    // fprintf(stderr, "b={%f, %f}\n", b[0], b[1]);

    // if (det == T(0)) {
    //   fprintf(stderr, "FATAL: det(M)=0, P={%f, %f, %f, %f}, n_roots=%d, i=%d, lambda=%f, nu={%f, %f, %f}\n", 
    //       P[0], P[1], P[2], P[3], n_roots, i, l[i], nu[0], nu[1], nu[2]);
    //   print3x3("V", VV);
    //   print3x3("W", WW);
    // }

    if (det == T(0)) continue;
    if (nu[0] >= 0 && nu[0] < 1 && nu[1] >= 0 && nu[1] < 1 && nu[2] >= 0 && nu[2] < 1) {
      // check interpolation results
      double v[3], w[3], c[3]; 
      ftk::linear_interpolation3_3(VV, nu, v);
      ftk::linear_interpolation3_3(WW, nu, w);
      ftk::cross_product(v, w, c);
      const double norm = ftk::vector_2norm_3(c);
      if (norm > 1e-3) continue; // reject
      
      const int j = n_solutions ++;
      lambda[j] = l[i];
      mu[j][0] = nu[0]; 
      mu[j][1] = nu[1];
      mu[j][2] = nu[2];
    }
  }
 
  // if (n_solutions > 0) 
  //   if (P[3] == T(0) || std::isinf(P[3]) || std::isnan(P[3])) {
  //     fprintf(stderr, "WARN: degeneracy case, P={%f, %f, %f, %f}, n_roots=%d, n_solutions=%d\n", 
  //         P[0], P[1], P[2], P[3], n_roots, n_solutions); 
  //   }

  return n_solutions;
}
#endif

template <typename T>
inline int solve_parallel_vector_bibarycentric(const T V[4][3], const T W[4][3], T mu[6][2])
{
  T X0[3][3] = {{0, 0, 0}, 
                {1, 0, 0},
                {0, 1, 0}};
  T X1[3][3] = {{1, 0, 0}, 
                {1, 1, 0},
                {0, 1, 0}};
  
  T V0[] = {V[0][0], V[0][1], V[0][2], V[1][0], V[1][1], V[1][2], V[2][0], V[2][1], V[2][2]},
    W0[] = {W[0][0], W[0][1], W[0][2], W[1][0], W[1][1], W[1][2], W[2][0], W[2][1], W[2][2]}, 
    V1[] = {V[0][0], V[0][1], V[0][2], V[2][0], V[2][1], V[2][2], V[3][0], V[3][1], V[3][2]}, 
    W1[] = {W[0][0], W[0][1], W[0][2], W[2][0], W[2][1], W[2][2], W[3][0], W[3][1], W[3][2]};

  transpose3(V0);
  transpose3(W0);
  transpose3(V1);
  transpose3(W1);

  // TODO
  T p[3];
#if 0
  if (find_zero_triangle(R0, I0, X0, p)) {
    pos[0] = p[0]; 
    pos[1] = p[1];
    return true;
  } else 
    return false;
#endif
}

template <typename T>
inline void solve_parallel_vector_tetrahedron(const T V[4][3], const T W[4][3], T Q[4], T P[4][4])
{
  const T A[3][3] = { // linear transformation
    {V[0][0] - V[3][0], V[1][0] - V[3][0], V[2][0] - V[3][0]}, 
    {V[0][1] - V[3][1], V[1][1] - V[3][1], V[2][1] - V[3][1]}, 
    {V[0][2] - V[3][2], V[1][2] - V[3][2], V[2][2] - V[3][2]}
  };
  const T B[3][3] = {
    {W[0][0] - W[3][0], W[1][0] - W[3][0], W[2][0] - W[3][0]}, 
    {W[0][1] - W[3][1], W[1][1] - W[3][1], W[2][1] - W[3][1]}, 
    {W[0][2] - W[3][2], W[1][2] - W[3][2], W[2][2] - W[3][2]}
  };
  const T a[3] = {V[3][0], V[3][1], V[3][2]};
  const T b[3] = {W[3][0], W[3][1], W[3][2]};

  const T rhs[3][2] = { // polynomials for the right hand side vectors
    {-a[0], b[0]}, 
    {-a[1], b[1]}, 
    {-a[2], b[2]}};

  // coefficients for Q
  characteristic_polynomial_3x3(A, B, Q);

  T adj[3][3][3]; // build adjugate matrix
  characteristic_polynomial_2x2(A[1][1], A[1][2], A[2][1], A[2][2], B[1][1], B[1][2], B[2][1], B[2][2], adj[0][0]);
  characteristic_polynomial_2x2(A[1][0], A[1][2], A[2][0], A[2][2], B[1][0], B[1][2], B[2][0], B[2][2], adj[1][0]);
  characteristic_polynomial_2x2(A[1][0], A[1][1], A[2][0], A[2][1], B[1][0], B[1][1], B[2][0], B[2][1], adj[2][0]);
  characteristic_polynomial_2x2(A[0][1], A[0][2], A[2][1], A[2][2], B[0][1], B[0][2], B[2][1], B[2][2], adj[0][1]);
  characteristic_polynomial_2x2(A[0][0], A[0][2], A[2][0], A[2][2], B[0][0], B[0][2], B[2][0], B[2][2], adj[1][1]);
  characteristic_polynomial_2x2(A[0][0], A[0][1], A[2][0], A[2][1], B[0][0], B[0][1], B[2][0], B[2][1], adj[2][1]);
  characteristic_polynomial_2x2(A[0][1], A[0][2], A[1][1], A[1][2], B[0][1], B[0][2], B[1][1], B[1][2], adj[0][2]);
  characteristic_polynomial_2x2(A[0][0], A[0][2], A[1][0], A[1][2], B[0][0], B[0][2], B[1][0], B[1][2], adj[1][2]);
  characteristic_polynomial_2x2(A[0][0], A[0][1], A[1][0], A[1][1], B[0][0], B[0][1], B[1][0], B[1][1], adj[2][2]);
  polynomial_scalar_multiplication(adj[0][1], 2, T(-1));
  polynomial_scalar_multiplication(adj[1][0], 2, T(-1));
  polynomial_scalar_multiplication(adj[1][2], 2, T(-1));
  polynomial_scalar_multiplication(adj[2][1], 2, T(-1));

  for (int i = 0; i < 3; i ++) {
    T poly[4] = {0};
    for (int j = 0; j < 4; j ++) 
      P[i][j] = 0; // clear results
    for (int j = 0; j <3; j ++) {
      polynomial_multiplication(adj[i][j], 2, rhs[j], 1, poly);
      polynomial_add_in_place(P[i], 3, poly, 3);
    }
  }

  P[3][0] = Q[0] - P[0][0] - P[1][0] - P[2][0];
  P[3][1] = Q[1] - P[0][1] - P[1][1] - P[2][1];
  P[3][2] = Q[2] - P[0][2] - P[1][2] - P[2][2];
  P[3][3] = Q[3] - P[0][3] - P[1][3] - P[2][3];
}

}
#endif