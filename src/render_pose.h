#pragma once
// Host-only rigid-pose math. Rows occupy words 0..2, 4..6, 8..10;
// translation occupies 12..14. Other words are opaque and never interpolated.
// Callers own guest bounds/identity/cut checks. False leaves output untouched.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace wdc::render {
using Pose = std::array<uint32_t, 16>;
using Quaternion = std::array<double, 4>; // x,y,z,w
inline double component(const Pose& p, unsigned n) { return std::bit_cast<float>(p[n]); }
inline bool rotation(const Pose& p, Quaternion& q) {
    double m[3][3];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            m[i][j] = component(p, i * 4 + j);
            if (!std::isfinite(m[i][j])) return false;
        }
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(component(p, 12 + i))) return false;
        for (unsigned j = i; j < 3; ++j) {
            double dot = 0;
            for (unsigned k = 0; k < 3; ++k) dot += m[i][k] * m[j][k];
            if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 0.01) return false;
        }
    }
    const double det = m[0][0] * (m[1][1]*m[2][2]-m[1][2]*m[2][1])
        - m[0][1] * (m[1][0]*m[2][2]-m[1][2]*m[2][0])
        + m[0][2] * (m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if (std::abs(det - 1.0) > 0.02) return false;
    const double trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        q = {(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4};
    } else {
        unsigned i = 0;
        if (m[1][1] > m[i][i]) i = 1;
        if (m[2][2] > m[i][i]) i = 2;
        const unsigned j = (i+1)%3, k = (i+2)%3;
        const double s = std::sqrt(1.0 + m[i][i] - m[j][j] - m[k][k]) * 2;
        q[i] = s/4; q[j] = (m[i][j]+m[j][i])/s; q[k] = (m[i][k]+m[k][i])/s;
        q[3] = (m[k][j]-m[j][k])/s;
    }
    double norm = 0;
    for (double v : q) norm += v*v;
    for (double& v : q) v /= std::sqrt(norm);
    return true;
}
inline bool interpolate_pose(const Pose& previous, const Pose& current, double alpha,
                             Pose& result) {
    Quaternion a, b;
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1 ||
        !rotation(previous, a) || !rotation(current, b)) return false;
    // Endpoint identity matters to the real-ROM geometry regression.
    if (alpha == 0) { result = previous; return true; }
    if (alpha == 1 || previous == current) { result = current; return true; }
    double dot = 0;
    for (unsigned i = 0; i < 4; ++i) dot += a[i]*b[i];
    if (dot < 0) { for (double& v : b) v = -v; dot = -dot; }
    double wa = 1-alpha, wb = alpha;
    if (dot < 0.9995) {
        const double angle = std::acos(dot);
        wa = std::sin((1-alpha)*angle)/std::sin(angle);
        wb = std::sin(alpha*angle)/std::sin(angle);
    }
    Quaternion q;
    double norm = 0;
    for (unsigned i = 0; i < 4; ++i) { q[i] = wa*a[i]+wb*b[i]; norm += q[i]*q[i]; }
    for (double& v : q) v /= std::sqrt(norm);
    const auto [x,y,z,w] = q;
    const double m[3][3] = {
        {1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)},
        {2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)},
        {2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)}};
    result = current;
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j)
            result[i*4+j] = std::bit_cast<uint32_t>(float(m[i][j]));
        result[12+i] = std::bit_cast<uint32_t>(float(
            (1-alpha)*component(previous, 12+i) + alpha*component(current, 12+i)));
    }
    return true;
}
}
