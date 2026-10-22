#pragma once
#include "graphics_config.h"
#include "common/rt64_user_configuration.h"

namespace wdc::graphics {
inline void configure(RT64::UserConfiguration& c, Settings requested,
                      float width_scale = 1.0f, float framebuffer_aspect = 4.0f / 3.0f) {
    using C = RT64::UserConfiguration;
    const auto s = normalize(requested);
    c.resolution = s.resolution == 0 ? C::Resolution::WindowIntegerScale : C::Resolution::Manual;
    c.resolutionMultiplier = s.resolution == 0 ? 1 : s.resolution;
    c.downsampleMultiplier = 1;
    // RT64 derives its source ratio from the framebuffer. WDC's 240- and
    // 480-line buffers both represent a 4:3 TV picture. Expand by the same
    // factor as the guest camera, independent of the framebuffer's pixel shape.
    c.aspectRatio = C::AspectRatio::Manual;
    c.aspectTarget = framebuffer_aspect * width_scale;
    c.extAspectRatio = s.hud == 0 ? C::AspectRatio::Original :
        s.hud == 1 ? C::AspectRatio::Manual : C::AspectRatio::Expand;
    c.extAspectTarget = framebuffer_aspect * (4.0f / 3.0f);
    c.refreshRate = s.rate == 0 ? C::RefreshRate::Original : s.rate == -1 ? C::RefreshRate::Display : C::RefreshRate::Manual;
    c.refreshRateTarget = s.rate > 0 ? s.rate : 60;
    c.antialiasing = s.samples == 8 ? C::Antialiasing::MSAA8X : s.samples == 4 ? C::Antialiasing::MSAA4X : s.samples == 2 ? C::Antialiasing::MSAA2X : C::Antialiasing::None;
    c.filtering = s.filtering == 0 ? C::Filtering::Nearest : s.filtering == 1 ? C::Filtering::Linear : C::Filtering::AntiAliasedPixelScaling;
    c.internalColorFormat = s.precision == 0 ? C::InternalColorFormat::Automatic : s.precision == 1 ? C::InternalColorFormat::Standard : C::InternalColorFormat::High;
    c.displayBuffering = C::DisplayBuffering::Triple;
}
}
