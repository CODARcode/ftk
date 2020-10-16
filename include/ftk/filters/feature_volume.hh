#ifndef _FTK_FEATURE_VOLUME_HH
#define _FTK_FEATURE_VOLUME_HH

#include <ftk/filters/feature_surface.hh>

#if FTK_HAVE_VTK
#include <vtkDoubleArray.h>
#include <vtkUnsignedIntArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridWriter.h>
#endif

namespace ftk {

struct feature_volume_t {
  std::vector<feature_point_t> pts;
  std::vector<std::array<int, 4>> conn;

#if FTK_HAVE_VTK
  vtkSmartPointer<vtkUnstructuredGrid> to_vtu() const;
#endif
};


#if FTK_HAVE_VTK
inline vtkSmartPointer<vtkUnstructuredGrid> feature_volume_t::to_vtu() const
{
  vtkSmartPointer<vtkUnstructuredGrid> grid = vtkUnstructuredGrid::New();
  vtkSmartPointer<vtkPoints> points = vtkPoints::New();
  
  vtkSmartPointer<vtkDataArray> array_id = vtkUnsignedIntArray::New();
  array_id->SetName("id");
  array_id->SetNumberOfComponents(1);
  array_id->SetNumberOfTuples(pts.size());

  vtkSmartPointer<vtkDataArray> array_time = vtkDoubleArray::New();
  array_time->SetName("time");
  array_time->SetNumberOfComponents(1);
  array_time->SetNumberOfTuples(pts.size());

  for (int i = 0; i < pts.size(); i ++) {
    const auto &p = pts[i];
    points->InsertNextPoint(p.x[0], p.x[1], p.x[2]);
    array_id->SetTuple1(i, p.id);
    array_time->SetTuple1(i, p.t);
  }
  grid->SetPoints(points);
  grid->GetPointData()->AddArray(array_id);
  grid->GetPointData()->AddArray(array_time);

  for (int i = 0; i < conn.size(); i ++) {
    const auto &c = conn[i];
    vtkIdType ids[4] = {std::get<0>(c), std::get<1>(c), std::get<2>(c), std::get<3>(c)};
    grid->InsertNextCell(VTK_TETRA, 4, ids);
  }

  return grid;
}
#endif

}

#endif
