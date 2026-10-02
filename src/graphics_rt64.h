#pragma once
#include "graphics_config.h"
#include "common/rt64_user_configuration.h"

namespace wdc::graphics {
inline void configure(RT64::UserConfiguration& c, Settings requested) {
    using C = RT64::UserConfiguration;
    const auto s = normalize(requested);
    c.resolution = s.resolution == 0 ? C::Resolution::WindowIntegerScale : C::Resolution::Manual;
    c.resolutionMultiplier = s.resolution == 0 ? 1 : s.resolution;
    c.downsampleMultiplier = 1;
    c.aspectRatio = C::AspectRatio::Original;
    c.aspectTarget = 4.0 / 3.0;
    c.extAspectRatio = C::AspectRatio::Original;
    c.refreshRate = s.rate == 0 ? C::RefreshRate::Original : s.rate == -1 ? C::RefreshRate::Display : C::RefreshRate::Manual;
    c.refreshRateTarget = s.rate > 0 ? s.rate : 60;
    c.antialiasing = s.samples == 8 ? C::Antialiasing::MSAA8X : s.samples == 4 ? C::Antialiasing::MSAA4X : s.samples == 2 ? C::Antialiasing::MSAA2X : C::Antialiasing::None;
    c.filtering = s.filtering == 0 ? C::Filtering::Nearest : s.filtering == 1 ? C::Filtering::Linear : C::Filtering::AntiAliasedPixelScaling;
    c.internalColorFormat = s.precision == 0 ? C::InternalColorFormat::Automatic : s.precision == 1 ? C::InternalColorFormat::Standard : C::InternalColorFormat::High;
    c.displayBuffering = C::DisplayBuffering::Triple;
}
}
