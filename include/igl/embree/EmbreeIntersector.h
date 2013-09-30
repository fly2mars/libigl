#ifndef IGL_EMBREE_INTERSECTOR_H
#define IGL_EMBREE_INTERSECTOR_H

#undef interface
#undef near
#undef far

#include "common/intersector.h"
#include "common/accel.h"
#include <vector>
//#include "types.h"

namespace igl
{
  template <
  typename PointMatrixType,
  typename FaceMatrixType,
  typename RowVector3>
  class EmbreeIntersector
  {
  public:
    // V  #V by 3 list of vertex positions
    // F  #F by 3 list of Oriented triangles
    //
    // Note: this will only find front-facing hits. To consider all hits then
    // pass [F;fliplr(F)]
    EmbreeIntersector();
    EmbreeIntersector(const PointMatrixType & V, const FaceMatrixType & F);
    virtual ~EmbreeIntersector();
  
    // Given a ray find the first *front-facing* hit
    // 
    // Inputs:
    //   origin  3d origin point of ray
    //   direction  3d (not necessarily normalized) direction vector of ray
    // Output:
    //   hit  embree information about hit
    // Returns true if and only if there was a hit
    bool intersectRay(
      const RowVector3& origin, 
      const RowVector3& direction,
      embree::Hit &hit) const;
    // Given a ray find the all *front-facing* hits in order
    // 
    // Inputs:
    //   origin  3d origin point of ray
    //   direction  3d (not necessarily normalized) direction vector of ray
    // Output:
    //   hit  embree information about hit
    //   num_rays  number of rays shot (at least one)
    // Returns true if and only if there was a hit
    bool intersectRay(
      const RowVector3& origin, 
      const RowVector3& direction, 
      std::vector<embree::Hit > &hits,
      int & num_rays) const;
  
    // Given a ray find the first *front-facing* hit
    // 
    // Inputs:
    //   a  3d first end point of segment
    //   ab  3d vector from a to other endpoint b
    // Output:
    //   hit  embree information about hit
    // Returns true if and only if there was a hit
    bool intersectSegment(const RowVector3& a, const RowVector3& ab, embree::Hit &hit) const;
    
  private:
    embree::BuildTriangle *triangles;
    embree::BuildVertex *vertices;
    embree::Ref<embree::Accel> _accel;
    embree::Ref<embree::Intersector> _intersector;
  };
}

// Implementation
#include <igl/EPS.h>

template <typename RowVector3>
inline embree::Vec3f toVec3f(const RowVector3 &p) { return embree::Vec3f((float)p[0], (float)p[1], (float)p[2]); }

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::EmbreeIntersector()
{
  static bool inited = false;
  if(!inited)
  {
	//embree::TaskScheduler::start();//init();
    inited = true;
  }
}

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::EmbreeIntersector(const PointMatrixType & V, const FaceMatrixType & F)
{
  static bool inited = false;
  if(!inited)
  {
	//embree::TaskScheduler::start();//init();
    inited = true;
  }
  
  size_t numVertices = 0;
  size_t numTriangles = 0;
  
  triangles = (embree::BuildTriangle*) embree::rtcMalloc(sizeof(embree::BuildTriangle) * F.rows());
  vertices = (embree::BuildVertex*) embree::rtcMalloc(sizeof(embree::BuildVertex) * V.rows());

  for(int i = 0; i < (int)V.rows(); ++i)
  {
    vertices[numVertices++] = embree::BuildVertex((float)V(i,0),(float)V(i,1),(float)V(i,2));
  }
  
  for(int i = 0; i < (int)F.rows(); ++i)
  {
    triangles[numTriangles++] = embree::BuildTriangle((int)F(i,0),(int)F(i,1),(int)F(i,2),i);
  }
  
  _accel = embree::rtcCreateAccel("default", "default", triangles, numTriangles, vertices, numVertices);
  _intersector = _accel->queryInterface<embree::Intersector>();
}

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::~EmbreeIntersector()
{
	embree::rtcFreeMemory();
}

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
bool 
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::intersectRay(const RowVector3& origin, const RowVector3& direction, embree::Hit &hit) const
{
  embree::Ray ray(toVec3f(origin), toVec3f(direction), 1e-4f);
  _intersector->intersect(ray, hit);
  return hit ; 
}

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
bool 
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::intersectRay(
  const RowVector3& origin, 
  const RowVector3& direction, 
  std::vector<embree::Hit > &hits,
  int & num_rays) const
{
  using namespace std;
  num_rays = 0;
  hits.clear();
  embree::Vec3f o = toVec3f(origin);
  embree::Vec3f d = toVec3f(direction);
  int last_id0 = -1;
  double self_hits = 0;
  // This epsilon is directly correleated to the number of missed hits, smaller
  // means more accurate and slower
  //const double eps = DOUBLE_EPS;
  const double eps = FLOAT_EPS;
  double min_t = embree::zero;
  bool large_hits_warned = false;
  while(true)
  {
#ifdef VERBOSE
    cout<<
      o[0]<<" "<<o[1]<<" "<<o[2]<<" + t*"<<
      d[0]<<" "<<d[1]<<" "<<d[2]<<" ---> "<<
      endl;
#endif
    embree::Hit hit;
    embree::Ray ray(o,d,min_t);
    num_rays++;
    _intersector->intersect(ray, hit);
    if(hit)
    {
      // Hit self again, progressively advance
      if(hit.id0 == last_id0 || hit.t <= min_t)
      {
        // sanity check
        assert(hit.t<1);
        // push min_t a bit more
        //double t_push = pow(2.0,self_hits-4)*(hit.t<eps?eps:hit.t);
        double t_push = pow(2.0,self_hits)*eps;
#ifdef VERBOSE
        cout<<"  t_push: "<<t_push<<endl;
#endif
        //o = o+t_push*d;
        min_t += t_push;
        self_hits++;
      }else
      {
        hits.push_back(hit);
#ifdef VERBOSE
        cout<<"  t: "<<hit.t<<endl;
#endif
        // Instead of moving origin, just change min_t. That way calculations
        // all use exactly same origin values
        min_t = hit.t;

        // reset t_scale
        self_hits = 0;
      }
      last_id0 = hit.id0;
      //cout<<"  id0: "<<hit.id0<<endl;
    }else
    {
      break;
    }
    if(hits.size()>1000 && !large_hits_warned)
    {
      cerr<<"Warning: Large number of hits..."<<endl;
      cerr<<"[ ";
      for(vector<embree::Hit>::iterator hit = hits.begin();
          hit != hits.end();
          hit++)
      {
        cerr<<(hit->id0+1)<<" ";
      }
      cerr.precision(std::numeric_limits< double >::digits10);
      cerr<<"[ ";
      for(vector<embree::Hit>::iterator hit = hits.begin();
          hit != hits.end();
          hit++)
      {
        cerr<<(hit->t)<<endl;;
      }

      cerr<<"]"<<endl;
      large_hits_warned = true;
      return hits.empty();
    }
  }
  return hits.empty();
}

template <
typename PointMatrixType,
typename FaceMatrixType,
typename RowVector3>
bool 
igl::EmbreeIntersector < PointMatrixType, FaceMatrixType, RowVector3>
::intersectSegment(const RowVector3& a, const RowVector3& ab, embree::Hit &hit) const
{
  embree::Ray ray(toVec3f(a), toVec3f(ab), embree::zero, embree::one);
  _intersector->intersect(ray, hit);
  return hit ; 
}

#endif //EMBREE_INTERSECTOR_H
