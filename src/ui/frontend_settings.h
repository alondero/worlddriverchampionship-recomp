#pragma once
namespace wdc::ui {
// Called only before renderer startup or on the presentation thread.
void create_settings();
void refresh_settings();
// Modal tab index, in create_settings order. Non-settings tabs are ignored.
void stage_defaults(int tab_index);
}
