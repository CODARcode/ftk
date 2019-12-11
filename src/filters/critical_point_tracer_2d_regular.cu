#include <nvfunctional>
#include <cstdio>
// #include <ftk/filters/critical_point_tracker_2d.hh>
#include <ftk/numeric/inverse_linear_interpolation_solver.hh>
#include <ftk/numeric/linear_interpolation.hh>
#include <ftk/hypermesh/lattice.hh>
#include <ftk/filters/critical_point.hh>
#include "common.cuh"
  
__device__
bool check_simplex_cp2t(
    const lattice3_t& core, 
    const lattice3_t& ext, 
    const element32_t& e, 
    const double *V, 
    cp3_t &cp)
{
  int vertices[3][3];
  for (int i = 0; i < 3; i ++)
    for (int j = 0; j < 3; j ++) {
      vertices[i][j] = e.corner[j]
        + unit_simplices_3_2[e.type][i][j];
      if (vertices[i][j] < core.st[j] || 
          vertices[i][j] > core.st[j] + core.sz[j] - 1)
        return false;
    }

  double v[3][2];
  for (int i = 0; i < 3; i ++) {
    size_t k = ext.to_index(vertices[i]);
    for (int j = 0; j < 2; j ++)
      v[i][j] = V[k*2+j]; // V has two channels
  }

  double mu[3];
  bool succ = ftk::inverse_lerp_s2v2(v, mu, 0.0);
 
  if (succ) {
    double X[3][3];
    for (int i = 0; i < 3; i ++)
      for (int j = 0; j < 3; j ++)
        X[i][j] = vertices[i][j];
    ftk::lerp_s2v3(X, mu, cp.x);
    return true;
  } else 
    return false;
}

__global__
void sweep_simplices(
    const lattice3_t core, int scope,
    const lattice3_t ext, const double *V, 
    unsigned long long &ncps, cp3_t *cps)
{
  int tid = getGlobalIdx_3D_1D();
  const element32_t e = element32_from_index(core, scope, tid);

  cp3_t cp;
  bool succ = check_simplex_cp2t(core, ext, e, V, cp);
  if (succ) {
    unsigned long long i = atomicAdd(&ncps, 1ul);
    cp.tag = tid;
    cps[i] = cp;
  }
}

static std::vector<cp3_t> extract_cp2dt(
    const lattice3_t& core, int scope, 
    const lattice3_t& ext, const double *V/* 4D array: 2*W*H*T */)
{
  fprintf(stderr, "init GPU...\n");
  const size_t ntasks = core.n() * 12; // ntypes_3[2] = 12; ntypes_3 is in device constant memory
  const int maxGridDim = 1024;
  const int blockSize = 256;
  const int nBlocks = idivup(ntasks, blockSize);
  dim3 gridSize;
  
  if (nBlocks >= maxGridDim) 
    gridSize = dim3(idivup(nBlocks, maxGridDim), maxGridDim);
  else 
    gridSize = dim3(nBlocks);

  double *dV;
  cudaMalloc((void**)&dV, 2 * sizeof(double) * ext.n());
  cudaMemcpy(dV, V, 2 * sizeof(double) * ext.n(), cudaMemcpyHostToDevice);

  unsigned long long *dncps; // number of cps
  cudaMalloc((void**)&dncps, sizeof(unsigned long long));
  cudaMemset(dncps, 0, sizeof(unsigned long long));

  cp3_t *dcps;
  cudaMalloc((void**)&dcps, sizeof(cp3_t) * ext.n());
  checkLastCudaError("[FTK-CUDA] error: sweep_simplices: cudaMalloc/cudaMemcpy");

  fprintf(stderr, "calling kernel func...\n");
  sweep_simplices<<<gridSize, blockSize>>>(core, scope, ext, dV, *dncps, dcps);
  checkLastCudaError("[FTK-CUDA] error: sweep_simplices");

  unsigned long long ncps;
  cudaMemcpy(&ncps, dncps, sizeof(unsigned long long), cudaMemcpyDeviceToHost);
  fprintf(stderr, "ncps=%lu\n", ncps);

  std::vector<cp3_t> cps(ncps);
  cudaMemcpy(cps.data(), dcps, sizeof(cp3_t) * ncps, cudaMemcpyDeviceToHost);
  
  cudaFree(dV);
  cudaFree(dncps);
  cudaFree(dcps);
  checkLastCudaError("[FTK-CUDA] error: sweep_simplices: cudaFree");
 
  cudaDeviceSynchronize();
  fprintf(stderr, "exit, ncps=%lu\n", ncps);

  return cps;
}

std::vector<cp3_t>
extract_cp2dt_cuda(
    const ftk::lattice& core, int scope, 
    const ftk::lattice& ext, const double *V)
{
  lattice3_t C(core), E(ext);
  return extract_cp2dt(C, scope, E, V);
}
