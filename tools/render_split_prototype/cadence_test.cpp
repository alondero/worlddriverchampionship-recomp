#include "cadence.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cmath>
int main() {
    for (unsigned hz : {60u, 75u, 90u, 120u, 144u, 165u, 240u}) {
        prototype::PresentationPeriod period(hz);
        // A constant-velocity pose must advance uniformly across group edges,
        // including groups that do not land on the current endpoint.
        double previous = 0;
        for (unsigned g=0; g<300; ++g) {
            for (unsigned d=1; d<=prototype::presentations_in_group(g,hz); ++d) {
                const double alpha=prototype::presentation_alpha(g,d,hz);
                assert(alpha > 0 && alpha <= 1);
                const double position=(g+alpha)/30;
                assert(std::abs(position-previous-1.0/hz)<1e-12);
                previous=position;
            }
        }
        uint64_t elapsed=0, frames=0;
        for (unsigned second=0; second<3600; ++second) {
            for (unsigned f=0; f<hz; ++f) elapsed += period.next_ns();
            assert(elapsed == uint64_t(second+1)*1000000000);
            for (unsigned g=0; g<30; ++g) frames += prototype::presentations_in_group(second*30+g,hz);
            assert(frames == uint64_t(second+1)*hz);
        }
    }
    // Source advances from sample 1 to 3: retain the latest five 144 Hz
    // slots, not five evenly spaced blends across the two-sample gap.
    const auto gap=prototype::schedule_samples(1,3,144,5);
    assert(gap.first_slot==10 && gap.count==5 && gap.skipped==5);
    assert(std::abs(gap.alpha(1)-13.0/24)<1e-12);
    assert(std::abs(gap.alpha(5)-23.0/24)<1e-12);
    for (unsigned hz : {60u,75u,90u,120u,144u,165u,240u}) {
        uint64_t sample=0, previous_slot=0;
        for(unsigned interval=0; interval<300; ++interval) {
            const unsigned delta=1+interval%4, budget=(hz+29)/30;
            auto schedule=prototype::schedule_samples(sample,sample+delta,hz,budget);
            assert(schedule.count<=budget && schedule.count>0);
            assert(schedule.first_slot>previous_slot);
            for(unsigned d=1; d<=schedule.count; ++d) {
                const auto alpha=schedule.alpha(d);
                assert(alpha>0 && alpha<=1);
                const double position=(sample+alpha*delta)/30;
                assert(std::abs(position-double(schedule.first_slot+d-1)/hz)<1e-12);
            }
            previous_slot=schedule.first_slot+schedule.count-1;
            sample+=delta;
        }
    }
    unsigned expected[]={4,5,5,5,5};
    for(unsigned g=0; g<5; ++g) assert(prototype::presentations_in_group(g,144)==expected[g]);
    puts("PASS: rational cadence and nanosecond pacing remain exact over one hour");
}
