#ifndef _FTK_POLYNOMIAL_HH
#define _FTK_POLYNOMIAL_HH

#include <cmath>

namespace ftk {

template <typename T>
bool polynomial_equals_to_zero(const T P[], int m, const T epsilon=1e-9)
{
  for (int i = 0; i <= m; i ++) 
    if (std::abs(P[i]) > epsilon) 
      return false;
  return true;
}

template <typename T>
bool polynomial_equals_to_constant(const T P[], int m, const T epsilon=1e-9)
{
  for (int i = 1; i <= m; i ++) 
    if (std::abs(P[i]) > epsilon) 
      return false;
  return true;
}

template <typename T>
T polynomial_evaluate(const T P[], int m, T x)
{
  T y(0);
  for (int i = 0; i <= m; i ++) 
    y += P[i] * std::pow(x, i);
  return y;
}

template <typename T>
void polynomial_derivative(const T P[], int m, T R[])
{
  for (int i = 0; i < m; i ++) 
    R[i] = P[i+1] * (i+1);
}

template <typename T>
T polynomial_evaluate_derivative(const T P[], int m, T x)
{
  T y(0);
  for (int i = 0; i < m; i++)
    y += P[i+1] * (i+1) * std::pow(x, i);
  return y;
}

template <typename T>
void polynomial_copy(const T P[], int m, T Q[])
{
  for (int i = 0; i <= m; i ++) 
    Q[i] = P[i];
}

template <typename T>
void polynomial_addition(const T P[], int m, const T Q[], int n, T R[]) 
{
  if (m >= n)
    for (int i = 0; i <= m; i ++)
      if (i <= n) R[i] = P[i] + Q[i];
      else R[i] = P[i];
  else 
    polynomial_addition(Q, n, P, m, R);
}

template <typename T>
void polynomial_subtraction(const T P[], int m, const T Q[], int n, T R[]) 
{
  if (m >= n)
    for (int i = 0; i <= m; i ++)
      if (i <= n) R[i] = P[i] - Q[i];
      else R[i] = P[i];
  else 
    polynomial_subtraction(Q, n, P, m, R);
}

template <typename T>
void polynomial_addition_in_place(T P[], int m, const T Q[], int n) // m >= n
{
  for (int i = 0; i <= n; i ++)
    P[i] += Q[i];
}

template <typename T>
void polynomial_subtraction_in_place(T P[], int m, const T Q[], int n) // m >= n
{
  for (int i = 0; i <= n; i ++)
    P[i] -= Q[i];
}

template <typename T>
void polynomial_multiplication(const T P[], int m, const T Q[], int n, T R[])
{
  for (int i = 0; i <= m+n; i ++) 
    R[i] = T(0);

  for (int i = 0; i <= m; i ++) 
    for (int j = 0; j <= n; j ++) 
      R[i+j] += P[i] * Q[j];
}

template <typename T>
void polynomial_scalar_multiplication(T P[], int m, T scalar)
{
  for (int i = 0; i <= m; i ++)
    P[i] *= scalar;
}

template <typename T>
void polynomial_scalar_multiplication(const T P[], int m, T scalar, const T R[])
{
  for (int i = 0; i <= m; i ++) 
    R[i] = P[i] * scalar;
}

// A = BQ + R
template <typename T>
void polynomial_long_division(const T A[], int m, const T B[], int n, T Q[], T R[])
{

}

}

#endif
