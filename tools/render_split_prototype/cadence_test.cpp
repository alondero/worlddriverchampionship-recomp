#include "cadence.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
int main() {
    for (unsigned hz : {60u, 75u, 90u, 120u, 144u, 165u, 240u}) {
        prototype::PresentationPeriod period(hz);
        uint64_t elapsed=0, frames=0;
        for (unsigned second=0; second<3600; ++second) {
            for (unsigned f=0; f<hz; ++f) elapsed += period.next_ns();
            assert(elapsed == uint64_t(second+1)*1000000000);
            for (unsigned g=0; g<30; ++g) frames += prototype::presentations_in_group(second*30+g,hz);
            assert(frames == uint64_t(second+1)*hz);
        }
    }
    unsigned expected[]={4,5,5,5,5};
    for(unsigned g=0; g<5; ++g) assert(prototype::presentations_in_group(g,144)==expected[g]);
    puts("PASS: rational cadence and nanosecond pacing remain exact over one hour");
}
