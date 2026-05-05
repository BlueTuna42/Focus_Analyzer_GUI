#ifndef MAIN_WINDOW_BUILDER_H
#define MAIN_WINDOW_BUILDER_H

#include "../gui_context.h"

#include <gtk/gtk.h>

struct MainWindowCallbacks {
    GCallback windowDestroy = nullptr;
    GCallback selectClicked = nullptr;
    GCallback selectKeyPress = nullptr;
    GCallback recheckClicked = nullptr;
    GCallback sortChanged = nullptr;
    GCallback deleteBlurryClicked = nullptr;
    GCallback resultRowActivated = nullptr;
    GCallback resultListKeyPress = nullptr;
    GCallback summaryDraw = nullptr;
    GCallback settingsClicked = nullptr;
    GCallback viewModeChanged = nullptr;
    GCallback flowBoxChildActivated = nullptr;
    GCallback listScrollEvent = nullptr;
};

void build_main_window(GUIContext& ctx, const MainWindowCallbacks& callbacks);

#endif
