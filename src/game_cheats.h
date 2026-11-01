#pragma once

#include <array>
#include <cstddef>

namespace wdc::cheats {
enum class Cheat {
    Tracks, Gt1Cars, Gt2Cars, Challenger, Interceptor, Gt1Events, Gt2Events,
    InfiniteRestarts, MirrorChampionship, RaceSelect, Ending,
    PinkCars, BlackCars, SmallCars, InvisibleCars, RedSkidmarks, RetroGraphics,
    SuperBlur, ClearScene, EvilRob, TopDown, Cinematic,
    Hawaii, LesGets, LasVegas, NewZealand, Lisbon, Rome, Sydney, Zurich, Kyoto, BlackForest,
    Count
};
struct Description {
    Cheat cheat;
    const char* id;
    const char* name;
    const char* description;
};
// Session-only. The settings tab publishes these flags; game-thread hooks read them.
// Nothing here is stored in a config file or in the Controller Pak save.
inline constexpr std::array<Description, static_cast<std::size_t>(Cheat::Count)> catalog{{
    {Cheat::Tracks, "unlock_tracks", "Unlock all tracks",
     "Show every track as available the next time a track list is opened. Turning this off brings the saved locks back. Resets when the application closes."},
    {Cheat::Gt1Cars, "unlock_gt1_cars", "Unlock all GT1 cars",
     "Show every GT1 car and its teams as available the next time those lists are opened. Saved progress is not changed, and turning this off brings the saved locks back. The Falcon Interceptor stays on its own unlock. Resets when the application closes."},
    {Cheat::Gt2Cars, "unlock_gt2_cars", "Unlock all GT2 cars",
     "Show every GT2 car and its teams as available the next time those lists are opened. Saved progress is not changed, and turning this off brings the saved locks back. Resets when the application closes."},
    {Cheat::Challenger, "unlock_challenger", "Unlock Falcon Challenger",
     "Make the Falcon Challenger available when car lists are reopened. Saved locks return when switched off."},
    {Cheat::Interceptor, "unlock_interceptor", "Unlock Falcon Interceptor",
     "Make the Falcon Interceptor and Boss team available when lists are reopened. Saved locks return when switched off."},
    {Cheat::Gt1Events, "unlock_gt1_events", "Unlock all GT1 events",
     "Make GT1 Championship events available when the event list is reopened. Race results can still advance your saved career."},
    {Cheat::Gt2Events, "unlock_gt2_events", "Unlock all GT2 events",
     "Make GT2 Championship events available when the event list is reopened. Race results can still advance your saved career."},
    {Cheat::InfiniteRestarts, "infinite_restarts", "Infinite restart credits",
     "Show nine restart credits and restart without spending saved credits. Turning this off restores the earned balance."},
    {Cheat::MirrorChampionship, "mirror_championship", "Mirror Championship tracks",
     "Start subsequent Championship races on mirrored tracks. Takes effect when a race is loaded."},
    {Cheat::RaceSelect, "race_select", "Championship race select",
     "Enable the original race-selection option when the Championship menu is reopened."},
    {Cheat::Ending, "show_ending", "Show Championship ending",
     "Show the original ending when the Championship event menu is next opened. Switch off to cancel; resets after that menu reads the request."},
    {Cheat::PinkCars, "pink_cars", "Pink cars (IGN64)",
     "Apply the IGN64 pink paint effect when cars are next loaded. Black paint takes priority."},
    {Cheat::BlackCars, "black_cars", "Black cars",
     "Apply the original black paint effect when cars are next loaded. Takes priority over pink paint."},
    {Cheat::SmallCars, "small_cars", "Small cars",
     "Use the original miniature car rendering. Handling and collision sizes stay the same."},
    {Cheat::InvisibleCars, "invisible_cars", "Invisible cars",
     "Hide car bodies using the original rendering switch. Racing and collisions continue."},
    {Cheat::RedSkidmarks, "red_skidmarks", "Red skid marks",
     "Draw new skid marks using the original red color effect."},
    {Cheat::RetroGraphics, "retro_graphics", "Retro graphics",
     "Use the original alternate track-material rendering mode."},
    {Cheat::SuperBlur, "super_blur", "Super blurry mode",
     "Enable the original full-screen blur effect."},
    {Cheat::ClearScene, "clear_scene", "Clear scene before HUD",
     "Enable the original debug screen clear before the HUD is drawn. This can obscure the race view."},
    {Cheat::EvilRob, "evilrob", "EVILROB McCloud tune",
     "Load the original lighter, more powerful McCloud F-Type A setup. Select that car and load a new race after changing this option."},
    {Cheat::TopDown, "top_down_camera", "Bird's-eye camera",
     "Use the original elevated camera position during racing. Turn off to restore the selected camera's normal position."},
    {Cheat::Cinematic, "cinematic_camera", "Trackside cameras",
     "Use the original trackside camera sequence during racing. You still control the car. Takes priority over Bird's-eye camera."},
    {Cheat::Hawaii, "unlock_hawaii", "Hawaii tracks (WEBSTEIN)", "Make all Hawaii layouts available when track lists are reopened."},
    {Cheat::LesGets, "unlock_les_gets", "Les Gets tracks (FRITZ)", "Make all Les Gets layouts available when track lists are reopened."},
    {Cheat::LasVegas, "unlock_las_vegas", "Las Vegas tracks (UNCANNY)", "Make all Las Vegas layouts available when track lists are reopened."},
    {Cheat::NewZealand, "unlock_new_zealand", "New Zealand tracks (VORTEX)", "Make all New Zealand layouts available when track lists are reopened."},
    {Cheat::Lisbon, "unlock_lisbon", "Lisbon tracks (JIGGLYPUFF)", "Make all Lisbon layouts available when track lists are reopened."},
    {Cheat::Rome, "unlock_rome", "Rome tracks (SKATEORDIE)", "Make all Rome layouts available when track lists are reopened."},
    {Cheat::Sydney, "unlock_sydney", "Sydney tracks (FROZENSKY)", "Make all Sydney layouts available when track lists are reopened."},
    {Cheat::Zurich, "unlock_zurich", "Zurich tracks (JACQUELINE)", "Make all Zurich layouts available when track lists are reopened."},
    {Cheat::Kyoto, "unlock_kyoto", "Kyoto tracks (STUFFY)", "Make all Kyoto layouts available when track lists are reopened."},
    {Cheat::BlackForest, "unlock_black_forest", "Black Forest tracks (JACKEDUP)", "Make all Black Forest layouts available when track lists are reopened."},
}};

bool enabled(Cheat cheat);
void set_enabled(Cheat cheat, bool value);
// Atomically clear the one-shot request without changing native guest state.
bool consume_ending();
}
