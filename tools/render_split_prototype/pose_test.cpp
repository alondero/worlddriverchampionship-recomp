#include "pose.h"
#include <cassert>
#include <cstdio>
#include <limits>

using prototype::Pose;
static void set(Pose& pose, unsigned at, float value) { pose[at] = std::bit_cast<uint32_t>(value); }
static Pose identity() {
    Pose pose{};
    for (unsigned i : {0u,5u,10u,15u}) set(pose, i, 1);
    return pose;
}
static Pose turn(unsigned axis, double degrees) {
    Pose pose = identity();
    const unsigned i=(axis+1)%3, j=(axis+2)%3;
    const double angle=degrees*std::acos(-1.0)/180;
    set(pose, i*4+i, float(std::cos(angle))); set(pose, j*4+j, float(std::cos(angle)));
    set(pose, i*4+j, float(-std::sin(angle))); set(pose, j*4+i, float(std::sin(angle)));
    return pose;
}
static void near(double a, double b) { assert(std::abs(a-b)<0.00001); }
int main() {
    const Pose a=identity();
    Pose out;
    for (unsigned axis=0; axis<3; ++axis) {
        Pose b=turn(axis,180), expected=turn(axis,90);
        set(b,12,10); set(b,13,-4); set(b,14,6);
        // Padding is opaque; even unusual bits must survive unchanged.
        b[3]=0xDEADBEEF;
        assert(prototype::interpolate_pose(a,b,0,out) && out==a);
        assert(prototype::interpolate_pose(a,b,1,out) && out==b);
        assert(prototype::interpolate_pose(a,b,.5,out));
        for(unsigned row=0;row<3;++row) for(unsigned col=0;col<3;++col)
            near(prototype::component(out,row*4+col),prototype::component(expected,row*4+col));
        near(prototype::component(out,12),5); near(prototype::component(out,13),-2);
        near(prototype::component(out,14),3); assert(out[3]==b[3]);
        assert(prototype::interpolate_pose(b,b,.37,out) && out==b);
    }
    assert(prototype::interpolate_pose(turn(2,179),turn(2,-179),.5,out));
    near(prototype::component(out,0),-1); near(prototype::component(out,5),-1);
    near(prototype::component(out,1),0);
    for (float bad : {2.0f, -1.0f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        Pose b=a; set(b,0,bad); const Pose sentinel=out;
        assert(!prototype::interpolate_pose(a,b,.5,out) && out==sentinel);
    }
    for (double bad : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
        assert(!prototype::interpolate_pose(a,a,bad,out));
    std::puts("PASS: rigid rotations, shortest arc, translation, exact endpoints, opaque words, invalid inputs");
}
