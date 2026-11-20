#undef NDEBUG
#include "race_frame_schedule.h"
#include "race_globals.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cmath>
static void test_draw_time_estimate() {
    using namespace wdc::render;
    using namespace std::chrono_literals;
    const std::chrono::steady_clock::time_point start{};
    RaceFrameSchedule schedule;
    assert(schedule.begin_group({240,1}, 1, true, true));
    schedule.finish_at_swap(start); // Authoritative swap before any private draw.
    schedule.begin_private_draw(start);
    assert(schedule.preparation_ns(100) == 100);
    schedule.finish_at_swap(start + 2ms);
    schedule.finish_at_swap(start + 7ms); // Later swap in the same draw.
    schedule.end_private_draw();
    schedule.finish_at_swap(start + 10ms); // Authoritative endpoint.
    assert(schedule.begin_group({240,1}, 1, false, true));
    assert(schedule.preparation_ns(100) == 2000100);
    schedule.begin_private_draw(start + 11ms);
    schedule.end_private_draw(); // Pose failure before the private swap.
    schedule.finish_at_swap(start + 20ms);
    assert(schedule.preparation_ns(0) == 2000000);
    schedule.begin_private_draw(start);
    schedule.finish_at_swap(start + 8ms);
    assert(schedule.preparation_ns(100) == 4166666);
    assert(schedule.begin_group({240,1}, 1, true, true));
    assert(schedule.preparation_ns(100) == 100);
    schedule.begin_private_draw(start);
    schedule.finish_at_swap(start + 3ms);
    schedule.begin_private_draw(start + 4ms);
    assert(schedule.begin_group({90,1}, 1, false, true));
    schedule.finish_at_swap(start + 9ms);
    assert(schedule.preparation_ns(100) == 100);
}

int main() {
    test_draw_time_estimate();
    wdc::render::race::DispatcherHistory history;
    assert(!history.restored_while_presenting(false, 1));
    assert(!history.restored_while_presenting(true, 1));
    assert(history.restored_while_presenting(true, 2));
    for (unsigned hz : {60u, 75u, 90u, 120u, 144u, 165u, 240u, 480u, 540u, 1000u}) {
        wdc::render::PresentationPeriod period({hz,1});
        // A constant-velocity pose must advance uniformly across group edges,
        // including groups that do not land on the current endpoint.
        double previous = 0;
        for (unsigned g=0; g<300; ++g) {
            for (unsigned d=1; d<=wdc::render::presentations_in_group(g,{hz,1}); ++d) {
                const double alpha=wdc::render::presentation_alpha(g,d,{hz,1});
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
            for (unsigned g=0; g<30; ++g) frames += wdc::render::presentations_in_group(second*30+g,{hz,1});
            assert(frames == uint64_t(second+1)*hz);
        }
    }
    // Source advances from sample 1 to 3: retain the latest five 144 Hz
    // slots, not five evenly spaced blends across the two-sample gap.
    const auto gap=wdc::render::schedule_samples(1,3,{144,1},5);
    assert(gap.first_slot==10 && gap.count==5 && gap.skipped==5);
    assert(std::abs(gap.alpha(1)-13.0/24)<1e-12);
    assert(std::abs(gap.alpha(5)-23.0/24)<1e-12);
    for (unsigned hz : {60u,75u,90u,120u,144u,165u,240u}) {
        uint64_t sample=0, previous_slot=0;
        for(unsigned interval=0; interval<300; ++interval) {
            const unsigned delta=1+interval%4, budget=(hz+29)/30;
            auto schedule=wdc::render::schedule_samples(sample,sample+delta,{hz,1},budget);
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
    assert(wdc::render::due_slot(0,5,0,{144,1})==0);
    assert(wdc::render::due_slot(1,5,1000000,{144,1})==1); // No repeated slot when early.
    assert(wdc::render::due_slot(1,5,20833333,{144,1})==2); // Before third-slot deadline.
    assert(wdc::render::due_slot(1,5,20833334,{144,1})==3);
    assert(wdc::render::due_slot(2,5,1000000000,{144,1})==4); // Keep final pose after stall.
    // First fractional deadline is 16,683,333 and one third ns.
    assert(wdc::render::due_slot(0, 2, 16683333, {60000,1001}) == 0);
    assert(wdc::render::due_slot(0, 2, 16683334, {60000,1001}) == 1);
    assert(wdc::render::due_slot(0, 8, UINT64_MAX, {120000,1001}) == 7);
    unsigned cursor=0, draws=0;
    for(uint64_t elapsed : {0ull, 10000000ull, 23000000ull, 35000000ull}) {
        const auto selected=wdc::render::due_slot(cursor,5,elapsed,{144,1});
        assert(selected>=cursor && selected<5);
        cursor=selected+1; ++draws;
    }
    assert(cursor==5 && draws==4); // One stale slot omitted, latest retained.
    // The last slot in every fifth 144 Hz sample interval is the native pose.
    // Missing a simulation update retains the same endpoint phase.
    for (unsigned end = 2; end <= 15; ++end) {
        for (unsigned gap : {1u, 2u}) {
            auto schedule = wdc::render::schedule_samples(end - gap, end, {144,1}, 5);
            assert(schedule.ends_on_grid() == (end % 5 == 0));
        }
    }
    wdc::render::RaceFrameSchedule scheduler;
    assert(!scheduler.next());
    assert(!scheduler.begin_group({60,0},1,false,true));
    assert(scheduler.begin_group({75,1},1,false,true));
    assert(!scheduler.present_native());
    assert(scheduler.next()->alpha == 0.4);
    assert(scheduler.next()->alpha == 0.8);
    assert(!scheduler.next());
    assert(scheduler.begin_group({75,1},1,false,true));
    assert(scheduler.present_native());
    const auto late = scheduler.next(UINT64_MAX);
    assert(late && late->index == 1 && late->skipped == 1 && late->slot == 4);
    assert(!scheduler.next());
    assert(scheduler.begin_group({75,1},2,false,true));
    assert(scheduler.group().begin == 2 && scheduler.group().end == 4 && scheduler.group().skipped == 2);
    assert(scheduler.begin_group({75,1},1,true,true) && scheduler.group().begin == 0);
    assert(scheduler.begin_group({60000,1001},1,false,true) && scheduler.group().begin == 0);
    assert(scheduler.begin_group({60000,1001},0,false,true) && scheduler.group().end == 1);
    assert(scheduler.begin_group({60000,1001},5,false,true) && scheduler.group().end == 1);
    assert(scheduler.begin_group({60,1},1,false,false) && !scheduler.present_native());
    assert(scheduler.next() && scheduler.next()->alpha == 1.0 && !scheduler.next());
    assert(scheduler.begin_group({360,1},1,false,true) && scheduler.group().count == 12 && scheduler.present_native());
    assert(scheduler.begin_group({1000,1},1,false,true) && scheduler.group().count == 33);
    assert(!scheduler.begin_group({1001,1},1,false,true) && !scheduler.next() && !scheduler.present_native());
    // A late private draw would delay the next authoritative simulation tick.
    // Keep the native endpoint at both integer and fractional display rates.
    for (wdc::render::DisplayRate rate : {wdc::render::DisplayRate{90,1}, {144,1}}) {
        assert(scheduler.begin_group(rate,1,true,true));
        assert(scheduler.next(0,33333333));
        assert(!scheduler.next(33333333,33333333) && scheduler.present_native());
        assert(!scheduler.next());
    }
    // Finishing a fractional group on time has already supplied its last
    // presentation. A later poll must not add a duplicate native endpoint.
    assert(scheduler.begin_group({75,1},1,true,true));
    assert(scheduler.next(0,33333333) && scheduler.next(0,33333333));
    assert(!scheduler.next(34000000,33333333) && !scheduler.present_native());
    // A fence taking 20 ms advances the same fractional clock used by the
    // swap. Skipping slot 3 does not shift slot 4 or the next group.
    assert(scheduler.begin_group({75,1},1,true,true));
    assert(scheduler.begin_group({75,1},1,false,true));
    const auto& grid = scheduler.group();
    assert(grid.swap_offset_ns(3, 0) == -6666666 && grid.swap_offset_ns(4, 0) == 6666666);
    const auto after_fence = scheduler.next(20000000 - grid.swap_offset_ns(3, 0), 40000000);
    assert(after_fence && after_fence->slot == 4 && grid.swap_offset_ns(5, 0) == 20000000);
    // At 59.94 Hz the second group starts just after a fractional slot.
    // A 2 ms capture still has time to draw both intermediate frames.
    assert(scheduler.begin_group({2997,50},1,true,true));
    assert(scheduler.begin_group({2997,50},1,false,true));
    const auto offset = scheduler.group().swap_offset_ns(scheduler.group().first_slot, 2'000'000);
    assert(scheduler.next(2'000'000 - offset)->index == 0);
    assert(scheduler.native_swap(true) == wdc::render::NativeSwap::Immediate);
    assert(scheduler.native_swap(false) == wdc::render::NativeSwap::Skip);
    assert(scheduler.begin_group({240,1},1,true,true));
    const auto& fast = scheduler.group();
    assert(fast.preparation_ns(0, 0) == 0);
    assert(fast.preparation_ns(200000, 700000) == 900000);
    const auto slow = fast.preparation_ns(12000000, 2500000);
    assert(slow == 4166666);
    const auto selected = scheduler.next(12000000 - fast.swap_offset_ns(fast.first_slot, slow));
    assert(selected && selected->slot == 2 && selected->alpha == 0.25);
    assert(fast.swap_offset_ns(selected->slot, slow) == slow + 4166666);
    assert(scheduler.native_swap(false) == wdc::render::NativeSwap::Paced);
    assert(scheduler.native_swap(true) == wdc::render::NativeSwap::Immediate);
    unsigned expected[]={4,5,5,5,5};
    for(unsigned g=0; g<5; ++g) assert(wdc::render::presentations_in_group(g,{144,1})==expected[g]);
    for (wdc::render::DisplayRate rate : {wdc::render::DisplayRate{60000,1001}, {120000,1001}, {2997,50}}) {
        wdc::render::PresentationPeriod period(rate);
        uint64_t elapsed = 0, slots = 0;
        for (unsigned sample = 0; sample < 30 * rate.denominator; ++sample) {
            auto schedule = wdc::render::schedule_samples(sample, sample + 1, rate, rate.draw_budget());
            assert(schedule.first_slot == slots + 1 && schedule.skipped == 0);
            assert(schedule.ends_on_grid() == (schedule.alpha(schedule.count) == 1.0));
            for (unsigned i = 1; i <= schedule.count; ++i) {
                elapsed += period.next_ns();
                ++slots;
                const double seconds = (sample + schedule.alpha(i)) / 30;
                assert(std::abs(seconds - double(slots) * rate.denominator / rate.numerator) < 1e-12);
            }
        }
        assert(slots == rate.numerator && elapsed == 1000000000ull * rate.denominator);
    }
    puts("PASS: rational cadence and nanosecond pacing remain exact over one hour");
}
