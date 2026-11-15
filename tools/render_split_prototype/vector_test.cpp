#include "vectors.h"
#include <cassert>
#include <limits>
#include <cstdio>
prototype::Vector3 v(float x,float y,float z) { return {std::bit_cast<uint32_t>(x),std::bit_cast<uint32_t>(y),std::bit_cast<uint32_t>(z)}; }
int main() {
    using prototype::interpolate_vector;
    auto a=v(-2,4,8),b=v(6,8,12),out=v(99,99,99);
    assert(interpolate_vector(a,b,.5,false,out) && out==v(2,6,10));
    assert(interpolate_vector(a,b,0,false,out) && out==a);
    assert(interpolate_vector(a,b,1,false,out) && out==b);
    a=v(1,0,0); b=v(0,1,0);
    assert(interpolate_vector(a,b,.5,true,out));
    assert(std::abs(std::bit_cast<float>(out[0])-std::sqrt(.5))<1e-6);
    assert(out[0]==out[1] && out[2]==0);
    assert(interpolate_vector(a,b,1,true,out) && out==b);
    const auto saved=out;
    assert(!interpolate_vector(a,v(-1,0,0),.5,true,out) && out==saved);
    assert(!interpolate_vector(v(0,0,0),b,.5,true,out) && out==saved);
    assert(!interpolate_vector(v(std::numeric_limits<float>::quiet_NaN(),0,0),b,.5,false,out) && out==saved);
    assert(!interpolate_vector(a,b,1.1,false,out) && out==saved);
    puts("PASS: vector interpolation, unit normals, exact endpoints and invalid-input preservation");
}
