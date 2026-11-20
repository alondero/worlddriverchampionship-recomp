#pragma once
// Host-only float32 position/normal interpolation. Endpoints are bit exact;
// invalid inputs leave output untouched. Guest bounds/ownership stay at caller.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
namespace wdc::render {
using Vector3 = std::array<uint32_t,3>;
inline bool interpolate_vector(const Vector3& a, const Vector3& b, double alpha,
                               bool normal, Vector3& output) {
    if (!std::isfinite(alpha) || alpha<0 || alpha>1) return false;
    double x[3], y[3], v[3], nx=0, ny=0, nv=0;
    for(unsigned i=0;i<3;++i) {
        x[i]=std::bit_cast<float>(a[i]); y[i]=std::bit_cast<float>(b[i]);
        if(!std::isfinite(x[i]) || !std::isfinite(y[i])) return false;
        nx+=x[i]*x[i]; ny+=y[i]*y[i];
        v[i]=x[i]*(1-alpha)+y[i]*alpha; nv+=v[i]*v[i];
    }
    if(normal && (std::abs(nx-1)>.02 || std::abs(ny-1)>.02)) return false;
    if(alpha==0) { output=a; return true; }
    if(alpha==1 || a==b) { output=b; return true; }
    if(normal && nv<1e-12) return false;
    Vector3 result;
    for(unsigned i=0;i<3;++i) {
        const float value=float(normal ? v[i]/std::sqrt(nv) : v[i]);
        if(!std::isfinite(value)) return false;
        result[i]=std::bit_cast<uint32_t>(value);
    }
    output=result; return true;
}
}
