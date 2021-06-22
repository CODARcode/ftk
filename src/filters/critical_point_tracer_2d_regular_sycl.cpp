#include <CL/sycl.hpp>
#include <vector>
#include <ftk/config.hh>
#include <ftk/mesh/lattice.hh>
#include "common.cuh"

static std::vector<cp_t> extract_cp2dt(
    int scope,
    int current_timestep,
    const lattice3_t& domain,
    const lattice3_t& core, 
    const lattice2_t& ext, 
    const double *V, // 4D array: 2*W*H*2
    bool use_explicit_coords,
    const double *coords)
{
  std::vector<cp_t> results;
  const size_t ntasks = core.n() * ntypes_3_2<scope_ordinal>();
  cl::sycl::range<1> work_items{ntasks};

  int counter = 0;
  {
    cl::sycl::buffer<double> dV(V, 2 * 2 * sizeof(double) * ext.n());
    auto dcounter = cl::sycl::buffer<int>( &counter, 1 );

    cl::sycl::queue q;
    q.submit([&](cl::sycl::handler &cgh) {
      auto V = dV.get_access<cl::sycl::access::mode::read>(cgh);
      auto counter = dcounter.get_access<cl::sycl::access::mode::write>(cgh);

      cgh.parallel_for<class cp2dtk>(work_items, [=](cl::sycl::id<1> tid) {
        cl::sycl::atomic<int> atomic_counter(cl::sycl::global_ptr<int>{&counter[0]});
        atomic_counter.fetch_add(1);
      });
    });
  }

  return results;
}

std::vector<cp_t> extract_cp2dt_sycl(
    int scope, 
    int current_timestep,
    const ftk::lattice& domain,
    const ftk::lattice& core, 
    const ftk::lattice& ext, 
    const double *Vc, 
    const double *Vn, 
    const double *Jc, 
    const double *Jn, 
    const double *Sc,
    const double *Sn, 
    bool use_explicit_coords,
    const double *coords)
{
  lattice3_t D(domain);
  lattice3_t C(core);
  lattice2_t E(ext);

  double *V = (double*)malloc(2*2*sizeof(double)*ext.n());
  if (Vc) memcpy(V, Vc, 2*sizeof(double)*ext.n());
  if (Vn) memcpy(V+2*sizeof(double)*ext.n(), Vn, 2*sizeof(double)*ext.n());

  // std::cerr << "domain=" << domain 
  //   << ", core=" << core << ", current_timestep=" 
  //   << current_timestep << std::endl;

  auto results = extract_cp2dt(scope, current_timestep, 
      D, C, E, 
      V, // Vc, Vn, Jc, Jn, Sc, Sn, 
      use_explicit_coords, coords);

  return results;
}
