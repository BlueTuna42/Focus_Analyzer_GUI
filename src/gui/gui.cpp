#include "gui.h"
#include "actions/delete_blurry_action.h"
#include "actions/photo_trash.h"
#include "gui_context.h"
#include "results/result_list_view.h"
#include "status/progress_summary.h"
#include "utils/path_utils.h"
#include "viewer/image_viewer.h"
#include "viewer/preview_loader.h"
#include "window/app_style.h"
#include "window/main_window_builder.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>

struct DirectoryData {
    std::string dirpath;
};

struct ProgressData {
    int processedFiles;
    int totalFiles;
};

struct SummaryData {
    int sharpFiles;
    int blurryFiles;
};

static GUIContext* g_ctx = nullptr;

static gboolean on_result_list_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);
static void on_result_delete_button_clicked(GtkButton* button, gpointer data);
static void on_result_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer data);
static void on_delete_blurry_clicked(GtkButton* button, gpointer data);
static void on_view_mode_combo_changed(GtkComboBox* combo, gpointer data);
static void on_flow_box_child_activated(GtkFlowBox* box, GtkFlowBoxChild* child, gpointer data);

// Result list helpers

static ResultListView current_result_list_view() {
    return {
        g_ctx->list_box,
        g_ctx->flow_box,
        g_ctx->list_scrolled_window,
        g_ctx->empty_results_label,
        G_CALLBACK(on_result_delete_button_clicked),
        static_cast<int>(g_ctx->viewMode),
        g_ctx->zoomLevel // <--- передаем уровень зума
    };
}

static void rebuild_result_list() {
    const std::vector<const ResultData*> visible = g_ctx->results.visible(g_ctx->sortMode);
    result_list_view_rebuild(current_result_list_view(), visible);
}

static void update_delete_blurry_button_state() {
    if (!g_ctx->button_delete_blurry) {
        return;
    }

    gtk_widget_set_sensitive(g_ctx->button_delete_blurry,
                             !g_ctx->scanInProgress && g_ctx->results.countBlurry() > 0);
}

// Scan UI state

static void set_scan_controls_busy(bool showProgressBar) {
    g_ctx->scanInProgress = true;
    result_list_view_set_empty_visible(current_result_list_view(), false);

    if (g_ctx->directory_box) {
        gtk_widget_set_sensitive(g_ctx->directory_box, FALSE);
    }
    gtk_widget_set_sensitive(g_ctx->button_select, FALSE);
    if (g_ctx->button_recheck) {
        gtk_widget_set_sensitive(g_ctx->button_recheck, FALSE);
    }
    gtk_button_set_label(GTK_BUTTON(g_ctx->button_select), "Analysis in progress...");
    if (g_ctx->list_scrolled_window) {
        gtk_widget_set_sensitive(g_ctx->list_scrolled_window, FALSE);
    }
    if (showProgressBar && g_ctx->progress_bar) {
        gtk_widget_show(g_ctx->progress_bar);
    }
}

static bool current_directory_available() {
    std::lock_guard<std::mutex> lock(g_ctx->mtx);
    return !g_ctx->currentDir.empty();
}

static void set_scan_controls_ready() {
    g_ctx->scanInProgress = false;

    gtk_button_set_label(GTK_BUTTON(g_ctx->button_select), "Select new folder for analysis");
    gtk_widget_set_sensitive(g_ctx->button_select, TRUE);
    if (g_ctx->button_recheck) {
        const bool hasCurrentDir = current_directory_available();
        gtk_widget_set_sensitive(g_ctx->button_recheck, hasCurrentDir);
        if (hasCurrentDir) {
            gtk_widget_show(g_ctx->button_recheck);
        }
    }
    if (g_ctx->directory_box) {
        gtk_widget_set_sensitive(g_ctx->directory_box, TRUE);
    }
    gtk_widget_set_sensitive(g_ctx->sort_combo, TRUE);
    if (g_ctx->list_scrolled_window) {
        gtk_widget_set_sensitive(g_ctx->list_scrolled_window, TRUE);
    }
    if (g_ctx->progress_bar) {
        gtk_widget_hide(g_ctx->progress_bar);
    }
    update_delete_blurry_button_state();
}

static void show_directory_controls(const std::string& dirpath) {
    const std::string labelText = "Current Folder: " + directory_name(dirpath);
    gtk_label_set_text(GTK_LABEL(g_ctx->folder_label), labelText.c_str());
    gtk_widget_show(g_ctx->folder_label);
    gtk_widget_show(g_ctx->sort_combo);
    gtk_widget_show(g_ctx->button_delete_blurry);
    gtk_widget_show(g_ctx->directory_box);
    gtk_widget_show(g_ctx->view_mode_combo);
    if (g_ctx->button_recheck) {
        gtk_widget_show(g_ctx->button_recheck);
    }
}

// Delete actions

static void update_summary_after_delete(bool wasBlurry) {
    if (!g_ctx->summary_box || !gtk_widget_get_visible(g_ctx->summary_box)) {
        return;
    }

    if (wasBlurry) {
        g_ctx->summaryBlurry = std::max(0, g_ctx->summaryBlurry - 1);
    } else {
        g_ctx->summarySharp = std::max(0, g_ctx->summarySharp - 1);
    }

    update_summary_bar(*g_ctx, g_ctx->summarySharp, g_ctx->summaryBlurry);
}

static bool delete_result_by_filename(const std::string& filename, GtkWindow* parent) {
    if (!confirm_and_trash_photo(parent, filename)) {
        return false;
    }

    bool removedWasBlurry = false;
    if (g_ctx->results.removeByFilename(filename, &removedWasBlurry)) {
        update_summary_after_delete(removedWasBlurry);
    }

    rebuild_result_list();
    update_delete_blurry_button_state();
    return true;
}

static bool delete_selected_result(GtkWindow* parent) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(g_ctx->list_box));
    if (!row) {
        return false;
    }

    const char* filename = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "filename"));
    if (!filename) {
        return false;
    }

    return delete_result_by_filename(filename, parent);
}

static void refresh_summary_from_results_if_visible() {
    if (!g_ctx->summary_box || !gtk_widget_get_visible(g_ctx->summary_box)) {
        return;
    }

    update_summary_bar(*g_ctx, g_ctx->results.countSharp(), g_ctx->results.countBlurry());
}

static void on_delete_blurry_clicked(GtkButton* button, gpointer data) {
    std::vector<std::string> blurryFiles = g_ctx->results.blurryFilenames();

    if (blurryFiles.empty()) {
        update_delete_blurry_button_state();
        return;
    }

    const DeleteBlurryResult deleteResult = delete_blurry_photos(GTK_WINDOW(g_ctx->window), blurryFiles);
    if (!deleteResult.confirmed) {
        return;
    }

    for (const std::string& filename : deleteResult.trashedFiles) {
        g_ctx->results.removeByFilename(filename);
    }

    rebuild_result_list();
    refresh_summary_from_results_if_visible();
    update_delete_blurry_button_state();
}

// Viewer bridge

static void select_visible_result_row(int index) {
    GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_ctx->list_box), index);
    if (row) {
        gtk_list_box_select_row(GTK_LIST_BOX(g_ctx->list_box), row);
        gtk_widget_grab_focus(GTK_WIDGET(row));
    }
}

static void open_viewer_for_result(const ResultData& result) {
    open_image_viewer(GTK_WINDOW(g_ctx->window), result, {
        [](const std::string& filename) {
            return g_ctx->results.visibleIndexForFilename(g_ctx->sortMode, filename);
        },
        [](int index) {
            return g_ctx->results.visibleAt(g_ctx->sortMode, index);
        },
        [](const std::string& filename, GtkWindow* parent) {
            return delete_result_by_filename(filename, parent);
        },
        [](int index) {
            select_visible_result_row(index);
        }
    });
}

static void open_result_row(GtkListBoxRow* row) {
    if (!row) {
        return;
    }

    const char* filename = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "filename"));
    if (!filename) {
        return;
    }

    if (g_ctx->scanInProgress) {
        return;
    }

    const int index = gtk_list_box_row_get_index(row);
    const ResultData* result = g_ctx->results.visibleAt(g_ctx->sortMode, index);
    if (result && result->filename == filename) {
        open_viewer_for_result(*result);
    }
}

// Scan callbacks

static void on_result_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer data) {
    open_result_row(row);
}

static gboolean on_file_processed(gpointer data) {
    ResultData* res = static_cast<ResultData*>(data);

    ResultData result = *res;
    result.thumbnail = add_status_border(load_preview_pixbuf(result.filename, 150, 150), result.isBlurry);
    const ResultData& storedResult = g_ctx->results.add(result);
    if (g_ctx->sortMode == SortMode::Default) {
        result_list_view_append(current_result_list_view(), storedResult, true);
    } else {
        rebuild_result_list();
    }
    update_delete_blurry_button_state();

    delete res;
    return G_SOURCE_REMOVE;
}

static gboolean on_directory_changed(gpointer data) {
    DirectoryData* directory = static_cast<DirectoryData*>(data);
    {
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        g_ctx->currentDir = directory->dirpath;
    }
    show_directory_controls(directory->dirpath);
    delete directory;
    return G_SOURCE_REMOVE;
}

static gboolean on_progress_reset(gpointer data) {
    ProgressData* progress = static_cast<ProgressData*>(data);
    set_scan_controls_busy(true);
    set_progress_bar(g_ctx->progress_bar, 0, progress->totalFiles);
    delete progress;
    return G_SOURCE_REMOVE;
}

static gboolean on_progress_updated(gpointer data) {
    ProgressData* progress = static_cast<ProgressData*>(data);
    set_progress_bar(g_ctx->progress_bar, progress->processedFiles, progress->totalFiles);
    delete progress;
    return G_SOURCE_REMOVE;
}

static bool focus_first_result_row();

static gboolean on_scan_finished(gpointer data) {
    SummaryData* summary = static_cast<SummaryData*>(data);
    set_scan_controls_ready();
    update_summary_bar(*g_ctx, summary->sharpFiles, summary->blurryFiles);
    result_list_view_set_empty_visible(current_result_list_view(), summary->sharpFiles + summary->blurryFiles == 0);
    focus_first_result_row();
    delete summary;
    return G_SOURCE_REMOVE;
}

static bool focus_first_result_row() {
    return result_list_view_focus_first_row(current_result_list_view());
}

// Input callbacks

static gboolean on_result_list_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
        return delete_selected_result(GTK_WINDOW(g_ctx->window)) ? TRUE : FALSE;
    }

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(g_ctx->list_box));
        open_result_row(row);
        return row ? TRUE : FALSE;
    }

    if (event->keyval != GDK_KEY_Up && event->keyval != GDK_KEY_KP_Up) {
        return FALSE;
    }

    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(g_ctx->list_box));
    const gboolean isFirstRow = row && gtk_list_box_row_get_index(row) == 0;
    if (!isFirstRow) {
        return FALSE;
    }

    gtk_widget_grab_focus(g_ctx->button_select);
    return TRUE;
}

static void on_result_delete_button_clicked(GtkButton* button, gpointer data) {
    GtkWidget* rowWidget = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_LIST_BOX_ROW);
    if (!rowWidget) {
        return;
    }

    GtkListBoxRow* row = GTK_LIST_BOX_ROW(rowWidget);
    gtk_list_box_select_row(GTK_LIST_BOX(g_ctx->list_box), row);

    const char* filename = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "filename"));
    if (!filename) {
        return;
    }

    delete_result_by_filename(filename, GTK_WINDOW(g_ctx->window));
}

static gboolean on_select_button_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down) {
        return focus_first_result_row() ? TRUE : FALSE;
    }

    return FALSE;
}

static void on_sort_combo_changed(GtkComboBox* combo, gpointer data) {
    const gint active = gtk_combo_box_get_active(combo);
    if (active < 0) {
        return;
    }

    g_ctx->sortMode = static_cast<SortMode>(active);
    rebuild_result_list();
    focus_first_result_row();
}

static void start_analysis_for_directory(const std::string& dirpath) {
    if (dirpath.empty() || g_ctx->scanInProgress) {
        return;
    }

    set_scan_controls_busy(false);
    g_ctx->results.clear();
    g_ctx->sortMode = SortMode::Default;
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_ctx->sort_combo), static_cast<gint>(g_ctx->sortMode));
    gtk_widget_set_sensitive(g_ctx->sort_combo, FALSE);
    update_delete_blurry_button_state();
    result_list_view_clear(current_result_list_view());

    show_directory_controls(dirpath);
    if (g_ctx->summary_box) {
        gtk_widget_hide(g_ctx->summary_box);
    }

    std::string title = "Focus Analyzer - Analysis: " + dirpath;
    gtk_window_set_title(GTK_WINDOW(g_ctx->window), title.c_str());

    std::lock_guard<std::mutex> lock(g_ctx->mtx);
    g_ctx->currentDir = dirpath;
    g_ctx->selectedDir = dirpath;
    g_ctx->dirSelected = true;
    g_ctx->cv.notify_all();
}

static void on_view_mode_combo_changed(GtkComboBox* combo, gpointer data) {
    const gint active = gtk_combo_box_get_active(combo);
    if (active < 0) return;

    g_ctx->viewMode = static_cast<ViewMode>(active);

    if (g_ctx->viewMode == ViewMode::List) {
        gtk_widget_show(g_ctx->list_box);
        gtk_widget_hide(g_ctx->flow_box);
    } else {
        gtk_widget_hide(g_ctx->list_box);
        gtk_widget_show(g_ctx->flow_box);
    }
    
    rebuild_result_list();
}

static void on_flow_box_child_activated(GtkFlowBox* box, GtkFlowBoxChild* child, gpointer data) {
    if (!child || g_ctx->scanInProgress) return;
    
    const char* filename = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "filename"));
    if (!filename) return;

    const int index = gtk_flow_box_child_get_index(child);
    const ResultData* result = g_ctx->results.visibleAt(g_ctx->sortMode, index);
    if (result && result->filename == filename) {
        open_viewer_for_result(*result);
    }
}

static gboolean on_list_scroll_event(GtkWidget* widget, GdkEventScroll* event, gpointer data) {
    if (event->state & GDK_CONTROL_MASK) { // Если зажат Ctrl
        double delta = 0.0;
        
        if (event->direction == GDK_SCROLL_UP) {
            delta = 0.1;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            delta = -0.1;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            if (event->delta_y < 0) delta = 0.1;
            else if (event->delta_y > 0) delta = -0.1;
        }

        if (delta != 0.0) {
            double newZoom = std::max(0.2, std::min(4.0, g_ctx->zoomLevel + delta));
            if (std::abs(newZoom - g_ctx->zoomLevel) > 0.01) {
                g_ctx->zoomLevel = newZoom;
                rebuild_result_list(); // Перерисовываем список
            }
            return TRUE; // Блокируем стандартную прокрутку
        }
    }
    return FALSE; // Разрешаем стандартную прокрутку
}

// Window callbacks

static void on_window_destroy(GtkWidget* widget, gpointer data) {
    std::lock_guard<std::mutex> lock(g_ctx->mtx);
    g_ctx->windowClosed = true;
    g_ctx->dirSelected = true;
    g_ctx->selectedDir.clear();
    g_ctx->cv.notify_all();
    gtk_main_quit();
}

static void on_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Folder", GTK_WINDOW(g_ctx->window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Select", GTK_RESPONSE_ACCEPT, NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        start_analysis_for_directory(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_recheck_button_clicked(GtkWidget *widget, gpointer data) {
    std::string dirpath;
    {
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        dirpath = g_ctx->currentDir;
    }

    start_analysis_for_directory(dirpath);
}

// Settings action

static void on_settings_clicked(GtkWidget* widget, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings",
        GTK_WINDOW(g_ctx->window),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_OK", GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL,
        NULL);
        
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 15);
    gtk_box_pack_start(GTK_BOX(content_area), grid, TRUE, TRUE, 0);
    
    // Theme options
    GtkWidget *theme_label = gtk_label_new("Theme:");
    gtk_widget_set_halign(theme_label, GTK_ALIGN_START);
    GtkWidget *theme_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "System");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo), "Dark");
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo), g_ctx->settings.themeMode);
    
    gtk_grid_attach(GTK_GRID(grid), theme_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, 0, 1, 1);
    
    // EXIF toggle
    GtkWidget *exif_check = gtk_check_button_new_with_label("Write EXIF rating based on sharpness");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(exif_check), g_ctx->settings.writeExif);
    gtk_grid_attach(GTK_GRID(grid), exif_check, 0, 1, 2, 1);
    
    // RAW open mode
    GtkWidget *raw_label = gtk_label_new("Open RAW images:");
    gtk_widget_set_halign(raw_label, GTK_ALIGN_START);
    GtkWidget *raw_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(raw_combo), "Half size");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(raw_combo), "Full size");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(raw_combo), "Preview");
    gtk_combo_box_set_active(GTK_COMBO_BOX(raw_combo), g_ctx->settings.rawMode);
    
    gtk_grid_attach(GTK_GRID(grid), raw_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), raw_combo, 1, 2, 1, 1);
    
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        g_ctx->settings.themeMode = gtk_combo_box_get_active(GTK_COMBO_BOX(theme_combo));
        g_ctx->settings.writeExif = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(exif_check));
        g_ctx->settings.rawMode = gtk_combo_box_get_active(GTK_COMBO_BOX(raw_combo));
        
        GtkSettings *gtk_settings = gtk_settings_get_default();
        if (g_ctx->settings.themeMode == 1) { // Light
            g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
        } else if (g_ctx->settings.themeMode == 2) { // Dark
            g_object_set(gtk_settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
        } else { // System
            gtk_settings_reset_property(gtk_settings, "gtk-application-prefer-dark-theme");
        }
    }
    gtk_widget_destroy(dialog);
}

// GTK thread

static void run_gtk_thread() {
    int argc = 0; 
    char **argv = nullptr;
    gtk_init(&argc, &argv);
    install_app_css();

    build_main_window(*g_ctx, {
        G_CALLBACK(on_window_destroy),
        G_CALLBACK(on_button_clicked),
        G_CALLBACK(on_select_button_key_press),
        G_CALLBACK(on_recheck_button_clicked),
        G_CALLBACK(on_sort_combo_changed),
        G_CALLBACK(on_delete_blurry_clicked),
        G_CALLBACK(on_result_row_activated),
        G_CALLBACK(on_result_list_key_press),
        G_CALLBACK(on_summary_draw),
        G_CALLBACK(on_settings_clicked),
        G_CALLBACK(on_view_mode_combo_changed),
        G_CALLBACK(on_flow_box_child_activated),
        G_CALLBACK(on_list_scroll_event)
    });

    gtk_widget_show_all(g_ctx->window);
    gtk_main();
}

// VisualGUI API

VisualGUI::VisualGUI() {
    g_ctx = new GUIContext();
    g_ctx->gtkThread = std::thread(run_gtk_thread);
}

VisualGUI::~VisualGUI() {
    if (g_ctx && g_ctx->gtkThread.joinable()) {
        g_ctx->gtkThread.join();
    }
    g_ctx->results.clear();
    delete g_ctx;
}

std::string VisualGUI::SelectDirectory() {
    if (!g_ctx) return "";
    std::unique_lock<std::mutex> lock(g_ctx->mtx);
    g_ctx->dirSelected = false;
    g_ctx->selectedDir.clear();
    g_ctx->cv.wait(lock, []{ return g_ctx->dirSelected || g_ctx->windowClosed; });
    return g_ctx->selectedDir;
}

void VisualGUI::SetCurrentDirectory(const std::string& dirpath) {
    if (!g_ctx || !g_ctx->window) return;
    {
        std::lock_guard<std::mutex> lock(g_ctx->mtx);
        g_ctx->currentDir = dirpath;
    }
    DirectoryData* directory = new DirectoryData{dirpath};
    g_idle_add(on_directory_changed, directory);
}

void VisualGUI::AddResult(const std::string& filename, bool isBlurry) {
    if (!g_ctx || !g_ctx->window) return;
    ResultData* res = new ResultData{filename, isBlurry};
    g_idle_add(on_file_processed, res); 
}

void VisualGUI::ResetProgress(int totalFiles) {
    if (!g_ctx || !g_ctx->window) return;
    ProgressData* progress = new ProgressData{0, totalFiles};
    g_idle_add(on_progress_reset, progress);
}

void VisualGUI::UpdateProgress(int processedFiles, int totalFiles) {
    if (!g_ctx || !g_ctx->window) return;
    ProgressData* progress = new ProgressData{processedFiles, totalFiles};
    g_idle_add(on_progress_updated, progress);
}

void VisualGUI::ShowFinished(int sharpFiles, int blurryFiles) {
    if (!g_ctx || !g_ctx->window) return;
    SummaryData* summary = new SummaryData{sharpFiles, blurryFiles};
    g_idle_add(on_scan_finished, summary);
}

bool VisualGUI::IsClosed() const {
    if (!g_ctx) return true;
    std::lock_guard<std::mutex> lock(g_ctx->mtx);
    return g_ctx->windowClosed;
}

AppSettings VisualGUI::GetSettings() const {
    if (!g_ctx) return AppSettings();
    std::lock_guard<std::mutex> lock(g_ctx->mtx);
    return g_ctx->settings;
}
