#include <fstream>
#include <mutex>
#include <thread>
#include <cassert>
#include <cxxopts.hpp>

#include <ftk/numeric/print.hh>
#include <ftk/numeric/cross_product.hh>
#include <ftk/numeric/vector_norm.hh>
#include <ftk/numeric/linear_interpolation.hh>
#include <ftk/numeric/bilinear_interpolation.hh>
#include <ftk/numeric/inverse_linear_interpolation_solver.hh>
#include <ftk/numeric/inverse_bilinear_interpolation_solver.hh>
#include <ftk/numeric/gradient.hh>
// #include <ftk/algorithms/cca.hh>
#include <ftk/geometry/cc2curves.hh>
#include <ftk/geometry/curve2tube.hh>
#include <hypermesh/ndarray.hh>
#include <hypermesh/regular_simplex_mesh.hh>


#include <ftk/external/diy/mpi.hpp>
#include <ftk/external/diy/master.hpp>
#include <ftk/external/diy/assigner.hpp>
#include <ftk/external/diy/io/bov.hpp>
#include <ftk/basic/distributed_union_find.hh>
#include "connected_critical_point.hpp"

#if FTK_HAVE_QT5
#include "widget.h"
#include <QApplication>
#endif

#if FTK_HAVE_VTK
#include <ftk/geometry/curve2vtk.hh>
#include <vtkPolyDataMapper.h>
#include <vtkTubeFilter.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#endif

// for serialization
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>

#define TIME_OF_STEPS true
#define MULTITHREAD false
#define PRINT_FEATURE_DENSITY false

int nthreads;

int DW, DH; // the dimensionality of the data is DW*DH
int DT; // number of timesteps

double start, end; 

hypermesh::ndarray<float> scalar, grad, hess;
hypermesh::regular_simplex_mesh m(3); // the 3D space-time mesh

hypermesh::regular_simplex_mesh block_m(3); // the 3D space-time mesh of this block
hypermesh::regular_simplex_mesh block_m_ghost(3); // the 3D space-time mesh of this block with ghost cells

std::vector<std::tuple<hypermesh::regular_lattice, hypermesh::regular_lattice>> lattice_partitions;
float threshold; // threshold for trajectories. The max scalar on each trajectory should be larger than the threshold. 

std::mutex mutex;
 
Block_Critical_Point* b = new Block_Critical_Point(); 
int gid; // global id of this block / this process
std::map<std::string, intersection_t>* intersections;

// the output sets of connected elements
std::vector<std::set<std::string>> connected_components_str; // connected components 

// the output trajectories
std::vector<std::vector<float>> trajectories;

int scaling_factor; // the factor that controls the shape of the synthesize data, default value: 15

template <typename T> // the synthetic function
T f(T x, T y, T t) 
{
  return cos(x*cos(t)-y*sin(t))*sin(x*sin(t)+y*cos(t));
}

template <typename T>
hypermesh::ndarray<T> generate_synthetic_data(int DW, int DH, int DT)
{
  hypermesh::ndarray<T> scalar;
  scalar.reshape(DW, DH, DT);

  // for (int k = 0; k < DT; k ++) {
  //   for (int j = 0; j < DH; j ++) {
  //     for (int i = 0; i < DW; i ++) {

  for (int k = std::max(0, block_m_ghost.lb(2)-2); k < std::min(block_m_ghost.ub(2)+3, DT); k ++) {
    for (int j = std::max(0, block_m_ghost.lb(1)-2); j < std::min(block_m_ghost.ub(1)+3, DH); j ++) {
      for (int i = std::max(0, block_m_ghost.lb(0)-2); i < std::min(block_m_ghost.ub(0)+3, DW); i ++) {
        const T x = ((T(i) / (DW-1)) - 0.5) * scaling_factor,
                y = ((T(j) / (DH-1)) - 0.5) * scaling_factor, 
                t = (T(k) / (DT-1)) + 1e-4;
        scalar(i, j, k) = f(x, y, t);
      }
    }
  }

  return scalar;
}

template <typename T>
hypermesh::ndarray<T> derive_gradients2(const hypermesh::ndarray<T>& scalar)
{
  hypermesh::ndarray<T> grad;
  grad.reshape(2, scalar.dim(0), scalar.dim(1), scalar.dim(2));
  
  // for (int k = 0; k < DT; k ++) {
  //   for (int j = 1; j < DH-1; j ++) {
  //     for (int i = 1; i < DW-1; i ++) {

  for (int k = std::max(0, block_m_ghost.lb(2)-1); k < std::min(block_m_ghost.ub(2)+2, DT); k ++) {
    for (int j = std::max(1, block_m_ghost.lb(1)-1); j < std::min(block_m_ghost.ub(1)+2, DH-1); j ++) {
      for (int i = std::max(1, block_m_ghost.lb(0)-1); i < std::min(block_m_ghost.ub(0)+2, DW-1); i ++) {
        grad(0, i, j, k) = 0.5 * (scalar(i+1, j, k) - scalar(i-1, j, k)) * (DW-1);
        grad(1, i, j, k) = 0.5 * (scalar(i, j+1, k) - scalar(i, j-1, k)) * (DH-1);
      }
    }
  }
  return grad;
}

template <typename T>
hypermesh::ndarray<T> derive_hessians2(const hypermesh::ndarray<T>& grad)
{
  hypermesh::ndarray<T> hess;
  hess.reshape(2, grad.dim(0), grad.dim(1), grad.dim(2), grad.dim(3));


  // for (int k = 0; k < DT; k ++) {
  //   for (int j = 2; j < DH-2; j ++) {
  //     for (int i = 2; i < DW-2; i ++) {

  for (int k = std::max(0, block_m_ghost.lb(2)); k < std::min(block_m_ghost.ub(2)+1, DT); k ++) {
    for (int j = std::max(2, block_m_ghost.lb(1)); j < std::min(block_m_ghost.ub(1)+1, DH-2); j ++) {
      for (int i = std::max(2, block_m_ghost.lb(0)); i < std::min(block_m_ghost.ub(0)+1, DW-2); i ++) {
        const T H00 = hess(0, 0, i, j, k) = // ddf/dx2
          0.5 * (grad(0, i+1, j, k) - grad(0, i-1, j, k)) * (DW-1);
        const T H01 = hess(0, 1, i, j, k) = // ddf/dxdy
          0.5 * (grad(0, i, j+1, k) - grad(0, i, j-1, k)) * (DH-1);
        const T H10 = hess(1, 0, i, j, k) = // ddf/dydx
          0.5 * (grad(1, i+1, j, k) - grad(1, i-1, j, k)) * (DW-1);
        const T H11 = hess(1, 1, i, j, k) = // ddf/dy2
          0.5 * (grad(1, i, j+1, k) - grad(1, i, j-1, k)) * (DH-1);
      }
    }
  }
  return hess;
}

void decompose_mesh(int nblocks) {
  std::vector<size_t> given = {0}; // partition the 2D spatial space and 1D timespace
  // std::vector<size_t> given = {0, 0, 1}; // Only partition the 2D spatial space
  // std::vector<size_t> given = {1, 1, 0}; // Only partition the 1D temporal space

  std::vector<size_t> ghost = {1, 1, 1}; // at least 1, larger is ok

  const hypermesh::regular_lattice& lattice = m.lattice(); 
  lattice.partition(nblocks, given, ghost, lattice_partitions); 

  auto& lattice_pair = lattice_partitions[gid]; 
  hypermesh::regular_lattice& lattice_p = std::get<0>(lattice_pair); 
  hypermesh::regular_lattice& lattice_ghost_p = std::get<1>(lattice_pair); 
  
  block_m.set_lb_ub(lattice_p);
  block_m_ghost.set_lb_ub(lattice_ghost_p);
}

// // decompose mesh by using DIY
// void decompose_mesh_DIY(diy::mpi::communicator& world, int nblocks) {

//   // share_face is a vector of bools indicating whether faces are shared in each dimension
//   // uninitialized values default to false
//   diy::RegularDecomposer<Bounds>::BoolVector          share_face;
//   // wrap is a vector of bools indicating whether boundary conditions are periodic in each dimension
//   // uninitialized values default to false
//   diy::RegularDecomposer<Bounds>::BoolVector          wrap;
//   // ghosts is a vector of ints indicating number of ghost cells per side in each dimension
//   // uninitialized values default to 0
//   diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts = {1, 1, 1};
//   // ghosts.push_back(2); ghosts.push_back(2); ghosts.push_back(2);

//   // create a RegularDecomposer
//   // allows access to all the methods in RegularDecomposer
//   diy::RegularDecomposer<Bounds> decomposer(m.nd(),
//                                             domain,
//                                             nblocks,
//                                             share_face,       // optional
//                                             wrap,             // optional
//                                             ghosts);          // optional

//   // call the decomposer's decompose function given a lambda
//   decomposer.decompose(world.rank(),
//    assigner,
//    [&](int gid,                   // block global id
//        const Bounds& core,        // block bounds without any ghost added
//        const Bounds& bounds,      // block bounds including any ghost region added
//        const Bounds& domain,      // global data bounds
//        const RCLink& link)        // neighborhood
//    {
//        // Block*          b   = new Block;             // possibly use custom initialization
//        // RGLink*         l   = new RGLink(link);
//        // int             lid = master.add(gid, b, l); // add block to the master (mandatory)
//        // process any additional args here, load the data, etc.

//       std::cout<<core[0]<<" "<<core[1]<<" "<<core[2]<<std::endl; 
//       std::cout<<bounds[0]<<" "<<bounds[1]<<" "<<bounds[2]<<std::endl; 
//       // std::cout<<domain[0]<<" "<<domain[1]<<" "<<domain[2]<<std::endl; 
//    });
// }

bool is_in_mesh(const hypermesh::regular_simplex_mesh_element& f, const hypermesh::regular_lattice& _lattice) { 
  // If the corner of the face is contained by the core lattice _lattice, we consider the element belongs to _lattice

  for (int i = 0; i < f.corner.size(); ++i){
    if (f.corner[i] < _lattice.start(i) || f.corner[i] > _lattice.upper_bound(i)) {
      return false; 
    }
  }

  return true;
}


// Add union edges to the block
void add_unions(const hypermesh::regular_simplex_mesh_element& f) {
  if (!f.valid()) return; // check if the 3-simplex is valid

  const auto elements = f.sides();
  std::set<std::string> features; 
  std::map<std::string, hypermesh::regular_simplex_mesh_element> id2element; 

  for (const auto& ele : elements) {
    std::string eid = ele.to_string(); 
    if(intersections->find(eid) != intersections->end()) {
      features.insert(eid); 
      id2element.insert(std::make_pair(eid, ele)); 
    }
  }

  if(features.size()  > 1) {
    std::set<std::string> features_in_block; 
    for(auto& feature : features) {
      if(b->has(feature)) {
        features_in_block.insert(feature); 
      }
    }

    if(features_in_block.size() > 1) {
      // When features are local, we just need to relate to the first feature element

      #if MULTITHREAD
        std::lock_guard<std::mutex> guard(mutex);
      #endif

      std::string first_feature = *(features_in_block.begin()); 
      for(std::set<std::string>::iterator ite_i = std::next(features_in_block.begin(), 1); ite_i != features_in_block.end(); ++ite_i) {
        std::string curr_feature = *ite_i; 

        if(first_feature < curr_feature) { // Since only when the id of related_ele < ele, then the related_ele can be the parent of ele
          b->add_related_element(curr_feature, first_feature); 
        } else {
          b->add_related_element(first_feature, curr_feature);
        }
        
      }
    }

    if(features_in_block.size() == 0 || features.size() == features_in_block.size()) {
      return ;
    }
  
    // When features are across processors, we need to relate all local feature elements to all remote feature elements
    for(auto& feature: features) {
      if(features_in_block.find(feature) == features_in_block.end()) { // if the feature is not in the block
        
        if(!b->has_gid(feature)) { // If the block id of this feature is unknown, search the block id of this feature
          hypermesh::regular_simplex_mesh_element& ele = id2element.find(feature)->second; 
          for(int i = 0; i < lattice_partitions.size(); ++i) {
            if(i != gid){ // We know the feature is not in this partition
              auto& _lattice = std::get<0>(lattice_partitions[i]); 
              if(is_in_mesh(ele, _lattice)) { // the feature is in mith partition
                #if MULTITHREAD
                  std::lock_guard<std::mutex> guard(mutex); // Use a lock for thread-save. 
                #endif

                b->set_gid(feature, i); // Set gid of this feature to mi  
              }
            }
          }
        }

        #if MULTITHREAD 
          std::lock_guard<std::mutex> guard(mutex); // Use a lock for thread-save. 
        #endif

        for(auto& feature_in_block : features_in_block) {

          if(feature < feature_in_block) { // When across processes, also, since only when the id of related_ele < ele, then the related_ele can be the parent of ele
            b->add_related_element(feature_in_block, feature); 
          }

        }
      }
    }
  }
}

void extract_connected_components(diy::mpi::communicator& world, diy::Master& master, diy::ContiguousAssigner& assigner, std::vector<std::set<std::string>>& components_str)
{
  // Initialization
    // Init union-find blocks
  std::vector<Block_Union_Find*> local_blocks;
  local_blocks.push_back(b); 

  // std::cout<<"Start Adding Elements to Blocks: "<<world.rank()<<std::endl; 

  std::vector<hypermesh::regular_simplex_mesh_element> eles_with_intersections;
  for(auto& pair : *intersections) {
    auto& eid = pair.first;
    // hypermesh::regular_simplex_mesh_element f = hypermesh::regular_simplex_mesh_element(m, 2, eid); 
    auto&f = eles_with_intersections.emplace_back(m, 2, eid); 

    if(is_in_mesh(f, block_m.lattice())) {
      b->add(eid); 
      b->set_gid(eid, gid);
    }
  }

  // std::cout<<"Finish Adding Elements to Blocks: "<<world.rank()<<std::endl; 

  // Connected Component Labeling by using union-find. 

  // std::cout<<"Start Adding Union Operations of Elements to Blocks: "<<world.rank()<<std::endl; 

  // // Method one:
  //   // For dense critical points
  //   // Enumerate each 3-d element to connect 2-d faces that contain critical points  
  // _m_ghost.element_for(3, add_unions, nthreads); 

  // Method two:
    // For sparse critical points
    // Enumerate all critical points, find their higher-order geometry; to connect critical points in this higher-order geometry
  std::set<std::string> visited_hypercells;
  for(auto& e : eles_with_intersections) { 
    const auto hypercells = e.side_of();
    for(auto& hypercell : hypercells) {
      std::string id_hypercell = hypercell.to_string(); 
      if(visited_hypercells.find(id_hypercell) == visited_hypercells.end()) {
        visited_hypercells.insert(id_hypercell); 
        add_unions(hypercell); 
      }
    }
  }

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      MPI_Barrier(world);
      end = MPI_Wtime();
      if(world.rank() == 0) {
        std::cout << "CCL: Init Blocks: " << end - start << " seconds. " << std::endl;
      }
      start = end; 
    #endif
  #endif

  // std::cout<<"Finish Adding Union Operations of Elements to Blocks: "<<world.rank()<<std::endl; 

  // std::cout<<"Start Distributed Union-Find: "<<world.rank()<<std::endl; 

  // get_connected_components
  bool is_iexchange = true; // true false
  exec_distributed_union_find(world, master, assigner, local_blocks, is_iexchange); 

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      MPI_Barrier(world);
      end = MPI_Wtime();
      if(world.rank() == 0) {
        std::cout << "CCL: Distributed Union-Find: " << end - start << " seconds. " << std::endl;
      }
      start = end; 
    #endif
  #endif

  // std::cout<<"Finish Distributed Union-Find: "<<world.rank()<<std::endl; 

  // Get disjoint sets of element IDs
  // b = static_cast<Block_Critical_Point*> (master.get(0)); // load block with local id 0
  b->get_sets(world, master, assigner, components_str); 

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      MPI_Barrier(world);
      end = MPI_Wtime();
      if(world.rank() == 0) {
        std::cout << "CCL: Gather Connected Components: " << end - start << " seconds. " << std::endl;
      }
      start = end; 
    #endif
  #endif 
}

void trace_intersections(diy::mpi::communicator& world, diy::Master& master, diy::ContiguousAssigner& assigner)
{
  typedef hypermesh::regular_simplex_mesh_element element_t; 

  // std::cout<<"Start Extracting Connected Components: "<<world.rank()<<std::endl; 

  extract_connected_components(world, master, assigner, connected_components_str);

  // std::cout<<"Finish Extracting Connected Components: "<<world.rank()<<std::endl; 

  // Convert connected components to geometries

  // if(world.rank() == 0) {
  if(connected_components_str.size() > 0) {
    
    std::vector<std::set<element_t>> cc; // connected components 
    // Convert element IDs to elements
    for(auto& comp_str : connected_components_str) {
      cc.push_back(std::set<element_t>()); 
      std::set<element_t>& comp = cc[cc.size() - 1]; 
      for(auto& eid : comp_str) {
        comp.insert(hypermesh::regular_simplex_mesh_element(m, 2, eid)); 
      }
    }

    auto neighbors = [](element_t f) {
      std::set<element_t> neighbors;
      const auto cells = f.side_of();
      for (const auto c : cells) {
        const auto elements = c.sides();
        for (const auto f1 : elements)
          neighbors.insert(f1);
      }
      return neighbors;
    };
    
    for (int i = 0; i < cc.size(); i ++) {
      std::vector<std::vector<float>> mycurves;
      auto linear_graphs = ftk::connected_component_to_linear_components<element_t>(cc[i], neighbors);
      for (int j = 0; j < linear_graphs.size(); j ++) {
        std::vector<float> mycurve, mycolors;
        float max_value = std::numeric_limits<float>::min();
        for (int k = 0; k < linear_graphs[j].size(); k ++) {
          auto p = intersections->at(linear_graphs[j][k].to_string());
          mycurve.push_back(p.x[0]); //  / (DW-1));
          mycurve.push_back(p.x[1]); //  / (DH-1));
          mycurve.push_back(p.x[2]); //  / (DT-1));
          mycurve.push_back(p.val);
          max_value = std::max(max_value, p.val);
        }
        if (max_value > threshold) {
          trajectories.emplace_back(mycurve);
        }
      }
    }

  }

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      MPI_Barrier(world);
      end = MPI_Wtime();
      if(world.rank() == 0) {
        std::cout << "Generate trajectories: " << end - start << " seconds. " << std::endl;
      }
      start = end; 
    #endif
  #endif
}

void check_simplex(const hypermesh::regular_simplex_mesh_element& f)
{
  if (!f.valid()) return; // check if the 2-simplex is valid
  const auto &vertices = f.vertices(); // obtain the vertices of the simplex
  float g[3][2], value[3];

  for (int i = 0; i < 3; i ++) {
    g[i][0] = grad(0, vertices[i][0], vertices[i][1], vertices[i][2]);
    g[i][1] = grad(1, vertices[i][0], vertices[i][1], vertices[i][2]);
    value[i] = scalar(vertices[i][0], vertices[i][1], vertices[i][2]);
  }
 
  float mu[3];
  bool succ = ftk::inverse_lerp_s2v2(g, mu);
  float val = ftk::lerp_s2(value, mu);
  
  if (!succ) return;

  float hessxx[3], hessxy[3], hessyy[3];
  for (int i = 0; i < vertices.size(); i ++) {
    hessxx[i] = hess(0, 0, vertices[i][0], vertices[i][1], vertices[i][2]);
    hessxy[i] = hess(0, 1, vertices[i][0], vertices[i][1], vertices[i][2]);
    hessyy[i] = hess(1, 1, vertices[i][0], vertices[i][1], vertices[i][2]);
  }
  float hxx = ftk::lerp_s2(hessxx, mu),
        hxy = ftk::lerp_s2(hessxy, mu), 
        hyy = ftk::lerp_s2(hessyy, mu);
  float eig[2];
  ftk::solve_eigenvalues_symmetric2x2(hxx, hxy, hyy, eig);

  if (eig[0] < 0 && eig[1] < 0) { 
    float X[3][3];
    for (int i = 0; i < vertices.size(); i ++)
      for (int j = 0; j < 3; j ++)
        X[i][j] = vertices[i][j];

    intersection_t I;
    I.eid = f.to_string();
    ftk::lerp_s2v3(X, mu, I.x);
    I.val = ftk::lerp_s2(value, mu);

    {
      #if MULTITHREAD 
        std::lock_guard<std::mutex> guard(mutex);
      #endif

      intersections->insert(std::make_pair(f.to_string(), I)); 
      // fprintf(stderr, "x={%f, %f}, t=%f, val=%f\n", I.x[0], I.x[1], I.x[2], I.val);
    }
  }
}

void scan_intersections() 
{
  block_m_ghost.element_for(2, check_simplex, nthreads); // iterate over all 2-simplices
}

void print_trajectories()
{
  printf("We found %lu trajectories:\n", trajectories.size());
  for (int i = 0; i < trajectories.size(); i ++) {
    printf("--Curve %d:\n", i);
    const auto &curve = trajectories[i];
    for (int k = 0; k < curve.size()/4; k ++) {
      printf("---x=(%f, %f), t=%f, val=%f\n", curve[k*4], curve[k*4+1], curve[k*4+2], curve[k*4+3]);
    }
  }
}

void read_traj_file(const std::string& f)
{
  std::ifstream ifs(f, std::ios::in | std::ios::binary);

  while(!ifs.eof()){
    float ncoord;
    ifs.read(reinterpret_cast<char*>(&ncoord), sizeof(float)); 

    // std::cout<<ncoord<<std::endl;

    auto& traj = trajectories.emplace_back(); 
    for(int j = 0; j < (int) ncoord; ++j) {
      float coord;
      ifs.read(reinterpret_cast<char*>(&coord), sizeof(float)); 

      traj.push_back(coord); 
    }
  }

  ifs.close();
}

void write_traj_file(diy::mpi::communicator& world, const std::string& f)
{
  std::vector<float> buf; 

  for(auto& traj : trajectories) {
    buf.push_back(traj.size()); 

    // std::cout<<traj.size()<<std::endl;

    for(auto& coord : traj) {
      buf.push_back(coord);  
    }
  }

  MPI_Status status;
  MPI_File fh;

  MPI_File_open(world, f.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);

  // MPI_File_write_shared(fh, &buf[0], buf.size(), MPI_FLOAT, &status);
  MPI_File_write_ordered(fh, &buf[0], buf.size(), MPI_FLOAT, &status); // Collective version of write_shared // refer to: https://www.mpi-forum.org/docs/mpi-2.2/mpi22-report/node283.htm

  MPI_File_close(&fh);
}

void read_dump_file(const std::string& f)
{
  std::vector<intersection_t> vector;

  std::ifstream ifs(f);
  cereal::JSONInputArchive ar(ifs);
  ar(vector);
  ifs.close();

  for (const auto &i : vector) {
    hypermesh::regular_simplex_mesh_element e(m, 2, i.eid);
    intersections->at(e.to_string()) = i;
  }
}

void write_dump_file(const std::string& f)
{
  std::vector<intersection_t> vector;
  for (const auto &i : *intersections)
    vector.push_back(i.second);
  
  std::ofstream ofs(f);
  cereal::JSONOutputArchive ar(ofs);
  ar(vector);
  ofs.close();
}

void write_element_sets_file(diy::mpi::communicator& world, const std::string& f)
{
  // diy::mpi::io::file out(world, f, diy::mpi::io::file::wronly | diy::mpi::io::file::create | diy::mpi::io::file::sequential | diy::mpi::io::file::append);

  std::stringstream ss;
  for(auto& comp_str : connected_components_str) {
    std::vector comp_str_vec(comp_str.begin(), comp_str.end()); 
    std::sort(comp_str_vec.begin(), comp_str_vec.end()); 

    for(auto& ele_id : comp_str_vec) {
      ss<<ele_id<<" "; 
    }
    ss<<std::endl; 
  }

  const std::string buf = ss.str();
  // out.write_at_all(0, buf.c_str(), buf.size()); 

  MPI_Status status;
  MPI_File fh;

  MPI_File_open(world, f.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &fh);

  // MPI_File_write_shared(fh, buf.c_str(), buf.length(), MPI_CHAR, &status);
  MPI_File_write_ordered(fh, buf.c_str(), buf.length(), MPI_CHAR, &status); // Collective version of write_shared // refer to: https://www.mpi-forum.org/docs/mpi-2.2/mpi22-report/node283.htm

  MPI_File_close(&fh);

  // out.close();
}

#if FTK_HAVE_VTK
void start_vtk_window()
{
  auto vtkcurves = ftk::curves2vtk(trajectories, 4);
  vtkcurves->Print(std::cerr);

  vtkSmartPointer<vtkTubeFilter> tubeFilter = vtkSmartPointer<vtkTubeFilter>::New();
  tubeFilter->SetInputData(vtkcurves);
  tubeFilter->SetRadius(1);
  tubeFilter->SetNumberOfSides(50);
  tubeFilter->Update();
  
  vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  // mapper->SetInputData(vtkcurves);
  mapper->SetInputConnection(tubeFilter->GetOutputPort());

  vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);

  // a renderer and render window
  vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();
  vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
  renderWindow->AddRenderer(renderer);

  // add the actors to the scene
  renderer->AddActor(actor);
  renderer->SetBackground(1, 1, 1); // Background color white

  vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor =
    vtkSmartPointer<vtkRenderWindowInteractor>::New();
  renderWindowInteractor->SetRenderWindow(renderWindow);

  vtkSmartPointer<vtkInteractorStyleTrackballCamera> style = 
      vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  renderWindowInteractor->SetInteractorStyle( style );

  renderWindowInteractor->Start();
}
#endif

int main(int argc, char **argv)
{
  diy::mpi::environment     env(0, 0); // env(NULL, NULL)
  diy::mpi::communicator    world;

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      if(world.rank() == 0) {
        std::cout << "Start! " << std::endl;
      }
    #endif
  #endif

  int nblocks = world.size(); 

  nthreads = 1; 
  
  #if MULTITHREAD
    if(world.size() == 1) {
      nthreads = std::thread::hardware_concurrency(); 
      std::cout<<"Use MULTITHREAD! "<<std::endl; 
    }
  #endif

  diy::Master               master(world, nthreads);
  diy::ContiguousAssigner   assigner(world.size(), nblocks);

  std::vector<int> gids; // global ids of local blocks
  assigner.local_gids(world.rank(), gids);
  gid = gids[0]; // We just assign one block for each process

  std::string pattern, format;
  std::string filename_dump_r, filename_dump_w;
  std::string filename_traj_r, filename_traj_w;
  std::string filename_sets_w; 
  bool show_qt = false, show_vtk = false;

  cxxopts::Options options(argv[0]);
  options.add_options()
    ("i,input", "input file name pattern", cxxopts::value<std::string>(pattern))
    ("f,format", "input file format", cxxopts::value<std::string>(format)->default_value("float32"))
    ("read-dump", "read dump file", cxxopts::value<std::string>(filename_dump_r))
    ("write-dump", "write dump file", cxxopts::value<std::string>(filename_dump_w))
    ("read-traj", "read traj file", cxxopts::value<std::string>(filename_traj_r))
    ("write-traj", "write traj file", cxxopts::value<std::string>(filename_traj_w))
    ("write-sets", "write sets of connected elements", cxxopts::value<std::string>(filename_sets_w))
    ("w,width", "width", cxxopts::value<int>(DW)->default_value("128"))
    ("h,height", "height", cxxopts::value<int>(DH)->default_value("128"))
    ("t,timesteps", "timesteps", cxxopts::value<int>(DT)->default_value("10"))
    ("scaling-factor", "scaling factor for synthetic data", cxxopts::value<int>(scaling_factor)->default_value("15"))
    ("threshold", "threshold", cxxopts::value<float>(threshold)->default_value("0"))
    ("vtk", "visualization with vtk", cxxopts::value<bool>(show_vtk))
    ("qt", "visualization with qt", cxxopts::value<bool>(show_qt))
    ("d,debug", "enable debugging");
  auto results = options.parse(argc, argv);

  #ifdef FTK_HAVE_MPI
    start = MPI_Wtime();
  #endif

  // Decompose mesh
  // ========================================

  m.set_lb_ub({2, 2, 0}, {DW-3, DH-3, DT-1}); // update the mesh; set the lower and upper bounds of the mesh

  decompose_mesh(nblocks); 
  // decompose_mesh_DIY(nblocks); 
  
  intersections = &b->intersections; 

  // Load data
  // ========================================

  if (pattern.empty()) { // if the input data is not given, generate a synthetic data for the demo
    scalar = generate_synthetic_data<float>(DW, DH, DT);
  } else { // load the binary data

    diy::mpi::io::file in(world, pattern, diy::mpi::io::file::rdonly);
    
    std::vector<unsigned> shape;
    shape.push_back(DT);
    shape.push_back(DH);
    shape.push_back(DW);

    diy::io::BOV reader(in, shape);

    diy::DiscreteBounds box(3);
    box.min[0] = std::max(0, block_m_ghost.lb(2)-2); box.max[0] = std::min(block_m_ghost.ub(2)+2, DT-1); 
    box.min[1] = std::max(0, block_m_ghost.lb(1)-2); box.max[1] = std::min(block_m_ghost.ub(1)+2, DH-1); 
    box.min[2] = std::max(0, block_m_ghost.lb(0)-2); box.max[2] = std::min(block_m_ghost.ub(0)+2, DW-1); 

    int box_size = (box.max[0] - box.min[0] + 1) * (box.max[1] - box.min[1] + 1) * (box.max[2] - box.min[2] + 1); 
    std::vector<float> _data(box_size);
    reader.read(box, &_data[0], true);

    scalar.reshape(DW, DH, DT);

    int ite = 0; 
    for (int k = box.min[0]; k < box.max[0]+1; k ++) {
      for (int j = box.min[1]; j < box.max[1]+1; j ++) {
        for (int i = box.min[2]; i < box.max[2]+1; i ++) {
          scalar(i, j, k) = _data[ite++];
        }
      }
    }
    
  }

  #ifdef FTK_HAVE_MPI
    #if TIME_OF_STEPS
      MPI_Barrier(world);
      end = MPI_Wtime();
      if(world.rank() == 0) {
        std::cout << "Init Data and Partition Mesh: " << end - start << " seconds. " << std::endl;
      }
      start = end;  
    #endif
  #endif

// ========================================

  //// For debug
  
  // if(world.rank() == 0) {
  //   for (auto& _m_pair : ms) {
  //     hypermesh::regular_simplex_mesh& _m = std::get<0>(_m_pair); 
  //     hypermesh::regular_simplex_mesh& _m_ghost = std::get<1>(_m_pair); 

  //     auto sizes = _m_ghost.sizes(); 
  //     std::cout << sizes[0] << " " << sizes[1] << " " << sizes[2] << std::endl; 
  //   }
  // }
  // exit(0); 

// ========================================
  
  if (!filename_traj_r.empty()) { // if the trajectory file is given, skip all the analysis and visualize/print the trajectories
    if(world.rank() == 0) {
      read_traj_file(filename_traj_r);
    }
  } else { // otherwise do the analysis

    if (!filename_dump_r.empty()) { // if the dump file is given, skill the sweep step; otherwise do sweep-and-trace
      read_dump_file(filename_dump_r);
    } else { // derive gradients and do the sweep
      grad = derive_gradients2(scalar);
      hess = derive_hessians2(grad);

      // #ifdef FTK_HAVE_MPI
      //   #if TIME_OF_STEPS
      //     MPI_Barrier(world);
      //     end = MPI_Wtime();
      //     if(world.rank() == 0) {
      //       std::cout << "Derive gradients: " << end - start << " seconds. " << std::endl;
      //     }
      //     start = end; 
      //   #endif
      // #endif

      // std::cout<<"Start scanning: "<<world.rank()<<std::endl; 

      scan_intersections();

      // #ifdef FTK_HAVE_MPI
      //   #if TIME_OF_STEPS
      //     end = MPI_Wtime();
      //     std::cout << end - start <<std::endl;
      //     // std::cout << "Scan Critical Points: " << end - start << " seconds. " << gid <<std::endl;
      //     start = end; 
      //   #endif
      // #endif

      #ifdef FTK_HAVE_MPI
        #if TIME_OF_STEPS
          MPI_Barrier(world);
          end = MPI_Wtime();
          if(world.rank() == 0) {
            std::cout << "Scan for Critical Points: " << end - start << " seconds. " <<std::endl;
          }
          start = end; 
        #endif
      #endif

      #if PRINT_FEATURE_DENSITY
        int n_2d_element; 
        block_m_ghost.element_for(2, [&](const hypermesh::regular_simplex_mesh_element& f){
          n_2d_element++ ;
        }, nthreads);
        std::cout<<"Feature Density: "<< (intersections->size()) / (float)n_2d_element << std::endl; 
        #ifdef FTK_HAVE_MPI
          #if TIME_OF_STEPS
            MPI_Barrier(world);
            end = MPI_Wtime();
            start = end; 
          #endif
        #endif
      #endif

      // std::cout<<"Finish scanning: "<<world.rank()<<std::endl; 
    }

    if (!filename_dump_w.empty())
      write_dump_file(filename_dump_w);


    // std::cout<<"Start tracing: "<<world.rank()<<std::endl; 

    trace_intersections(world, master, assigner);

    // std::cout<<"Finish tracing: "<<world.rank()<<std::endl; 

    #ifdef FTK_HAVE_MPI
      if (!filename_traj_w.empty()) {
        write_traj_file(world, filename_traj_w);

        #ifdef FTK_HAVE_MPI
          #if TIME_OF_STEPS
            MPI_Barrier(world);
            end = MPI_Wtime();
            if(world.rank() == 0) {
              std::cout << "Output trajectories: " << end - start << " seconds. " << std::endl;
            }
            start = end; 
          #endif
        #endif
      }
    #endif

    #ifdef FTK_HAVE_MPI
      // if(world.rank() == 0) {
        if (!filename_sets_w.empty())
          write_element_sets_file(world, filename_sets_w);
      // }
    #endif
  }

  if(world.rank() == 0) {
    if (show_qt) {
  #if FTK_HAVE_QT5
      QApplication app(argc, argv);
      QGLFormat fmt = QGLFormat::defaultFormat();
      fmt.setSampleBuffers(true);
      fmt.setSamples(16);
      QGLFormat::setDefaultFormat(fmt);

      CGLWidget *widget = new CGLWidget(scalar);
      widget->set_trajectories(trajectories, threshold);
      widget->show();
      return app.exec();
  #else
      fprintf(stderr, "Error: the executable is not compiled with Qt\n");
  #endif
    } else if (show_vtk) {
  #if FTK_HAVE_VTK
      start_vtk_window();
      // ftk::write_curves_vtk(trajectories, "trajectories.vtp");
  #else
      fprintf(stderr, "Error: the executable is not compiled with VTK\n");
  #endif
    } else {
      // print_trajectories();
    }
  }

  return 0;
}
