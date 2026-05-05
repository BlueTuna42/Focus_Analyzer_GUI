#ifndef GUI_CONTEXT_H
#define GUI_CONTEXT_H

#include "results/result_store.h"

#include <condition_variable>
#include <gtk/gtk.h>
#include "gui.h"
#include <mutex>
#include <string>
#include <thread>

enum class ViewMode {
    List = 0,
    Grid
};

struct GUIContext {
    GtkWidget *window = nullptr;
    GtkWidget *top_button_box = nullptr;
    GtkWidget *list_box = nullptr;
    GtkWidget *list_overlay = nullptr;
    GtkWidget *list_scrolled_window = nullptr;
    GtkWidget *empty_results_label = nullptr;
    GtkWidget *button_select = nullptr;
    GtkWidget *button_recheck = nullptr;
    GtkWidget *directory_box = nullptr;
    GtkWidget *folder_label = nullptr;
    GtkWidget *sort_combo = nullptr;
    GtkWidget *button_delete_blurry = nullptr;
    GtkWidget *progress_bar = nullptr;
    GtkWidget *summary_box = nullptr;
    GtkWidget *summary_sharp_label = nullptr;
    GtkWidget *summary_blurry_label = nullptr;
    GtkWidget *summary_bar = nullptr;
    GtkWidget *button_settings = nullptr;
    AppSettings settings;
    GtkWidget *flow_box = nullptr;
    GtkWidget *view_mode_combo = nullptr;
    ViewMode viewMode = ViewMode::List;
    int summarySharp = 0;
    int summaryBlurry = 0;
    SortMode sortMode = SortMode::Default;
    ResultStore results;

    std::mutex mtx;
    std::condition_variable cv;
    std::string selectedDir = "";
    std::string currentDir = "";
    bool dirSelected = false;
    bool windowClosed = false;
    bool scanInProgress = false;

    std::thread gtkThread;
};

#endif
