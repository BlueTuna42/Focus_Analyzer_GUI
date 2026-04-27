#include "gui.h"
#include "bmp.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdio> // Required for std::remove
#include <strings.h>
#include <libraw/libraw.h>

struct ResultData {
    std::string filename;
    bool isBlurry;
    GdkPixbuf *thumbnail = nullptr;
};

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

enum class SortMode {
    Default = 0,
    SharpFirst,
    BlurryFirst,
    SharpOnly,
    BlurryOnly
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
    int summarySharp = 0;
    int summaryBlurry = 0;
    SortMode sortMode = SortMode::Default;
    std::vector<ResultData> results;
    
    std::mutex mtx;
    std::condition_variable cv;
    std::string selectedDir = "";
    std::string currentDir = "";
    bool dirSelected = false;
    bool windowClosed = false;
    bool scanInProgress = false;
    
    std::thread gtkThread;
};

static GUIContext* g_ctx = nullptr;

static void update_summary_bar(int sharpFiles, int blurryFiles);
static gboolean on_result_list_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);
static void on_result_delete_button_clicked(GtkButton* button, gpointer data);
static void on_result_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer data);
static std::vector<const ResultData*> sorted_visible_results();
static void on_delete_blurry_clicked(GtkButton* button, gpointer data);

static void install_app_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "button.delete-button, #delete-photo-button {"
        "  background-image: none;"
        "  background-color: #8b1e3f;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.delete-button:hover, #delete-photo-button:hover {"
        "  background-image: none;"
        "  background-color: #9f274c;"
        "}"
        "button.delete-button:active, #delete-photo-button:active {"
        "  background-image: none;"
        "  background-color: #6f1832;"
        "}"
        "button.delete-button:disabled, #delete-photo-button:disabled {"
        "  background-image: none;"
        "  background-color: #4a4a4a;"
        "  color: #b8b8b8;"
        "  border-color: #3a3a3a;"
        "}"
        "button.delete-button label, #delete-photo-button label {"
        "  color: white;"
        "}"
        "button.delete-button:disabled label, #delete-photo-button:disabled label {"
        "  color: #b8b8b8;"
        "}"
        "button.subtle-delete-button {"
        "  background-image: none;"
        "  background-color: transparent;"
        "  color: #8b1e3f;"
        "  border-color: #c8cdd2;"
        "}"
        "button.subtle-delete-button:hover {"
        "  background-image: none;"
        "  background-color: #8b1e3f;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.subtle-delete-button:active {"
        "  background-image: none;"
        "  background-color: #6f1832;"
        "  color: white;"
        "  border-color: #6f1832;"
        "}"
        "button.subtle-delete-button label {"
        "  color: #8b1e3f;"
        "}"
        "button.subtle-delete-button:hover label, button.subtle-delete-button:active label {"
        "  color: white;"
        "}"
        "list row:selected, list row:selected:focus {"
        "  background-image: none;"
        "  background-color: #eceff1;"
        "  color: inherit;"
        "}"
        "list row:focus {"
        "  outline-color: #9aa0a6;"
        "  outline-style: solid;"
        "  outline-width: 1px;"
        "  outline-offset: -1px;"
        "}"
        "progressbar text {"
        "  color: #f2f2f2;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}",
        -1,
        NULL);

    GdkScreen* screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(screen,
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_USER);
    }
    g_object_unref(provider);
}

static void clear_result_cache() {
    if (!g_ctx) {
        return;
    }

    for (ResultData& result : g_ctx->results) {
        if (result.thumbnail) {
            g_object_unref(result.thumbnail);
            result.thumbnail = nullptr;
        }
    }
    g_ctx->results.clear();
}

static std::string path_filename(const std::string& path) {
    const std::filesystem::path fsPath(path);
    const std::string filename = fsPath.filename().string();
    return filename.empty() ? path : filename;
}

static std::string directory_name(const std::string& path) {
    const std::filesystem::path fsPath(path);
    std::string name = fsPath.filename().string();
    if (name.empty() && fsPath.has_parent_path()) {
        name = fsPath.parent_path().filename().string();
    }
    return name.empty() ? path : name;
}

// Context to handle the individual image viewer window
struct ImageContext {
    std::string filename;
    bool isBlurry = false;
    GtkWidget *viewer_window;
    GtkWidget *image;
    GtkWidget *previous_button;
    GtkWidget *next_button;
};

static void free_pixbuf_pixels(guchar* pixels, gpointer data) {
    g_free(pixels);
}

static GdkPixbuf* scale_pixbuf_to_fit(GdkPixbuf* pixbuf, int maxWidth, int maxHeight) {
    if (!pixbuf || maxWidth <= 0 || maxHeight <= 0) {
        return pixbuf;
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const double scale = std::min(static_cast<double>(maxWidth) / width,
                                  static_cast<double>(maxHeight) / height);
    if (scale >= 1.0) {
        return pixbuf;
    }

    const int scaledWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    const int scaledHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, scaledWidth, scaledHeight, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    return scaled;
}

static bool is_raw_file(const std::string& filename) {
    const size_t extPos = filename.find_last_of('.');
    const std::string ext = (extPos == std::string::npos) ? "" : filename.substr(extPos);

    return strcasecmp(ext.c_str(), ".CR2") == 0 || strcasecmp(ext.c_str(), ".NEF") == 0 ||
           strcasecmp(ext.c_str(), ".ARW") == 0 || strcasecmp(ext.c_str(), ".DNG") == 0 ||
           strcasecmp(ext.c_str(), ".RW2") == 0 || strcasecmp(ext.c_str(), ".RAF") == 0;
}

static GdkPixbuf* create_pixbuf_from_rgb_data(const libraw_processed_image_t& image, int maxWidth, int maxHeight) {
    if (image.width <= 0 || image.height <= 0 || image.colors < 3) {
        return nullptr;
    }

    const int channels = 3;
    const int rowstride = image.width * channels;
    guchar* pixels = static_cast<guchar*>(g_malloc(static_cast<gsize>(rowstride) * image.height));

    if (image.bits == 16) {
        const unsigned short* source = reinterpret_cast<const unsigned short*>(image.data);
        for (unsigned int y = 0; y < image.height; ++y) {
            for (unsigned int x = 0; x < image.width; ++x) {
                const unsigned int sourceIndex = (y * image.width + x) * image.colors;
                const unsigned int targetIndex = y * rowstride + x * channels;
                pixels[targetIndex] = static_cast<guchar>(source[sourceIndex] >> 8);
                pixels[targetIndex + 1] = static_cast<guchar>(source[sourceIndex + 1] >> 8);
                pixels[targetIndex + 2] = static_cast<guchar>(source[sourceIndex + 2] >> 8);
            }
        }
    } else {
        for (unsigned int y = 0; y < image.height; ++y) {
            for (unsigned int x = 0; x < image.width; ++x) {
                const unsigned int sourceIndex = (y * image.width + x) * image.colors;
                const unsigned int targetIndex = y * rowstride + x * channels;
                pixels[targetIndex] = image.data[sourceIndex];
                pixels[targetIndex + 1] = image.data[sourceIndex + 1];
                pixels[targetIndex + 2] = image.data[sourceIndex + 2];
            }
        }
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(pixels,
                                                 GDK_COLORSPACE_RGB,
                                                 FALSE,
                                                 8,
                                                 image.width,
                                                 image.height,
                                                 rowstride,
                                                 free_pixbuf_pixels,
                                                 NULL);

    return scale_pixbuf_to_fit(pixbuf, maxWidth, maxHeight);
}

static GdkPixbuf* load_raw_preview_pixbuf(const std::string& filename, int maxWidth, int maxHeight) {
    libraw_data_t *lr = libraw_init(0);
    if (!lr) {
        return nullptr;
    }

    lr->params.half_size = 1;
    lr->params.output_bps = 8;

    GdkPixbuf* pixbuf = nullptr;
    if (libraw_open_file(lr, filename.c_str()) == LIBRAW_SUCCESS &&
        libraw_unpack(lr) == LIBRAW_SUCCESS &&
        libraw_dcraw_process(lr) == LIBRAW_SUCCESS) {
        int err = 0;
        libraw_processed_image_t *image = libraw_dcraw_make_mem_image(lr, &err);
        if (image && image->type == LIBRAW_IMAGE_BITMAP) {
            pixbuf = create_pixbuf_from_rgb_data(*image, maxWidth, maxHeight);
        }
        if (image) {
            libraw_dcraw_clear_mem(image);
        }
    }

    libraw_close(lr);
    return pixbuf;
}

static GdkPixbuf* create_pixbuf_from_grayscale(const GrayscaleImage& image, int maxWidth, int maxHeight) {
    const int channels = 3;
    const int rowstride = image.width * channels;
    guchar* pixels = static_cast<guchar*>(g_malloc(static_cast<gsize>(rowstride) * image.height));

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int sourceIndex = y * image.width + x;
            const int targetIndex = y * rowstride + x * channels;
            float sourceValue = image.data[sourceIndex];
            if (!std::isfinite(sourceValue)) {
                sourceValue = 0.0f;
            }

            const auto value = static_cast<guchar>(std::clamp(static_cast<int>(std::lround(sourceValue)), 0, 255));
            pixels[targetIndex] = value;
            pixels[targetIndex + 1] = value;
            pixels[targetIndex + 2] = value;
        }
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(pixels,
                                                 GDK_COLORSPACE_RGB,
                                                 FALSE,
                                                 8,
                                                 image.width,
                                                 image.height,
                                                 rowstride,
                                                 free_pixbuf_pixels,
                                                 NULL);

    return scale_pixbuf_to_fit(pixbuf, maxWidth, maxHeight);
}

static GdkPixbuf* load_preview_pixbuf(const std::string& filename, int maxWidth, int maxHeight) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(filename.c_str(), maxWidth, maxHeight, TRUE, &error);
    if (pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return pixbuf;
    }

    if (error) {
        g_error_free(error);
    }

    if (is_raw_file(filename)) {
        pixbuf = load_raw_preview_pixbuf(filename, maxWidth, maxHeight);
        if (pixbuf) {
            return pixbuf;
        }
    }

    auto image = ImageIO::readImage(filename, true);
    if (!image) {
        return nullptr;
    }

    return create_pixbuf_from_grayscale(*image, maxWidth, maxHeight);
}

static GdkPixbuf* add_status_border(GdkPixbuf* pixbuf, bool isBlurry) {
    if (!pixbuf) {
        return nullptr;
    }

    constexpr int borderWidth = 4;
    const int sourceWidth = gdk_pixbuf_get_width(pixbuf);
    const int sourceHeight = gdk_pixbuf_get_height(pixbuf);
    GdkPixbuf* framed = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                       gdk_pixbuf_get_has_alpha(pixbuf),
                                       8,
                                       sourceWidth + borderWidth * 2,
                                       sourceHeight + borderWidth * 2);
    if (!framed) {
        return pixbuf;
    }

    const guint32 borderColor = isBlurry ? 0xd62728ff : 0x2e7d32ff;
    gdk_pixbuf_fill(framed, borderColor);
    gdk_pixbuf_copy_area(pixbuf, 0, 0, sourceWidth, sourceHeight, framed, borderWidth, borderWidth);
    g_object_unref(pixbuf);
    return framed;
}

// --- Image Viewer Callbacks ---

static bool result_visible_for_sort(const ResultData& result) {
    if (g_ctx->sortMode == SortMode::SharpOnly) {
        return !result.isBlurry;
    }

    if (g_ctx->sortMode == SortMode::BlurryOnly) {
        return result.isBlurry;
    }

    return true;
}

static GtkWidget* create_result_row(const ResultData& result) {
    const char* color = result.isBlurry ? "red" : "forestgreen";
    const char* badgeText = result.isBlurry ? "Blurry" : "Sharp";
    const char* badgeForeground = result.isBlurry ? "#8b1e3f" : "#1f7a3f";
    const std::string displayName = path_filename(result.filename);

    GdkPixbuf *fallbackPixbuf = nullptr;
    GdkPixbuf *pixbuf = result.thumbnail;
    if (!pixbuf) {
        fallbackPixbuf = add_status_border(load_preview_pixbuf(result.filename, 100, 100), result.isBlurry);
        pixbuf = fallbackPixbuf;
    }

    GtkWidget* row = gtk_list_box_row_new();
    gtk_widget_set_can_focus(row, TRUE);
    g_object_set_data_full(G_OBJECT(row), "filename", g_strdup(result.filename.c_str()), g_free);
    g_object_set_data(G_OBJECT(row), "is-blurry", GINT_TO_POINTER(result.isBlurry ? 1 : 0));

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_container_add(GTK_CONTAINER(row), box);

    GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_size_request(image, 108, 108);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    GtkWidget* textBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(textBox, TRUE);
    gtk_widget_set_valign(textBox, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), textBox, TRUE, TRUE, 0);

    GtkWidget* badge = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(badge), 0.0);
    gchar* badgeMarkup = g_markup_printf_escaped(
        "<span foreground=\"%s\" weight=\"bold\" size=\"small\">%s</span>",
        badgeForeground,
        badgeText);
    gtk_label_set_markup(GTK_LABEL(badge), badgeMarkup);
    g_free(badgeMarkup);
    gtk_box_pack_start(GTK_BOX(textBox), badge, FALSE, FALSE, 0);

    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(label, TRUE);
    gchar* labelMarkup = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>",
                                                 color,
                                                 displayName.c_str());
    gtk_label_set_markup(GTK_LABEL(label), labelMarkup);
    g_free(labelMarkup);
    gtk_box_pack_start(GTK_BOX(textBox), label, FALSE, TRUE, 0);

    GtkWidget* btn_delete = gtk_button_new_with_label("Delete");
    gtk_widget_set_valign(btn_delete, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(btn_delete, 72, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_delete),
                                result.isBlurry ? "delete-button" : "subtle-delete-button");
    gtk_widget_set_tooltip_text(btn_delete, result.isBlurry ? "Delete blurry photo" : "Delete photo");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_result_delete_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), btn_delete, FALSE, FALSE, 0);

    if (fallbackPixbuf) {
        g_object_unref(fallbackPixbuf);
    }

    return row;
}

static void scroll_results_to_bottom() {
    if (!g_ctx->list_scrolled_window) {
        return;
    }

    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(g_ctx->list_scrolled_window));
    if (!adjustment) {
        return;
    }

    gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
}

static void set_empty_results_visible(bool visible) {
    if (!g_ctx->empty_results_label) {
        return;
    }

    if (visible) {
        gtk_widget_show(g_ctx->empty_results_label);
    } else {
        gtk_widget_hide(g_ctx->empty_results_label);
    }
}

static void append_result_row(const ResultData& result, bool autoscroll) {
    set_empty_results_visible(false);
    GtkWidget* row = create_result_row(result);
    gtk_list_box_insert(GTK_LIST_BOX(g_ctx->list_box), row, -1);
    gtk_widget_show_all(row);

    if (!autoscroll) {
        return;
    }

    scroll_results_to_bottom();
}

static void clear_result_rows() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(g_ctx->list_box));
    for (GList* child = children; child != NULL; child = child->next) {
        gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);
}

static void rebuild_result_list() {
    clear_result_rows();

    const std::vector<const ResultData*> visible = sorted_visible_results();
    for (const ResultData* result : visible) {
        append_result_row(*result, false);
    }
}

static int count_blurry_results() {
    int count = 0;
    for (const ResultData& result : g_ctx->results) {
        if (result.isBlurry) {
            ++count;
        }
    }
    return count;
}

static void update_delete_blurry_button_state() {
    if (!g_ctx->button_delete_blurry) {
        return;
    }

    gtk_widget_set_sensitive(g_ctx->button_delete_blurry,
                             !g_ctx->scanInProgress && count_blurry_results() > 0);
}

static std::vector<const ResultData*> sorted_visible_results() {
    std::vector<const ResultData*> visible;
    visible.reserve(g_ctx->results.size());

    for (const ResultData& result : g_ctx->results) {
        if (result_visible_for_sort(result)) {
            visible.push_back(&result);
        }
    }

    if (g_ctx->sortMode == SortMode::SharpFirst || g_ctx->sortMode == SortMode::BlurryFirst) {
        const bool blurryFirst = g_ctx->sortMode == SortMode::BlurryFirst;
        std::stable_sort(visible.begin(), visible.end(), [blurryFirst](const ResultData* left, const ResultData* right) {
            if (left->isBlurry == right->isBlurry) {
                return false;
            }

            return blurryFirst ? left->isBlurry : !left->isBlurry;
        });
    }

    return visible;
}

static int visible_index_for_filename(const std::string& filename) {
    const std::vector<const ResultData*> visible = sorted_visible_results();
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->filename == filename) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static const ResultData* visible_result_at(int index) {
    if (index < 0) {
        return nullptr;
    }

    const std::vector<const ResultData*> visible = sorted_visible_results();
    if (static_cast<size_t>(index) >= visible.size()) {
        return nullptr;
    }

    return visible[static_cast<size_t>(index)];
}

static void update_summary_after_delete(bool wasBlurry) {
    if (!g_ctx->summary_box || !gtk_widget_get_visible(g_ctx->summary_box)) {
        return;
    }

    if (wasBlurry) {
        g_ctx->summaryBlurry = std::max(0, g_ctx->summaryBlurry - 1);
    } else {
        g_ctx->summarySharp = std::max(0, g_ctx->summarySharp - 1);
    }

    update_summary_bar(g_ctx->summarySharp, g_ctx->summaryBlurry);
}

static bool remove_result_by_filename(const std::string& filename, bool* removedWasBlurry = nullptr) {
    bool removed = false;
    auto newEnd = std::remove_if(g_ctx->results.begin(), g_ctx->results.end(),
                                 [&filename, &removed, removedWasBlurry](ResultData& result) {
                                     if (result.filename != filename) {
                                         return false;
                                     }

                                     if (!removed && removedWasBlurry) {
                                         *removedWasBlurry = result.isBlurry;
                                     }
                                     removed = true;
                                     if (result.thumbnail) {
                                         g_object_unref(result.thumbnail);
                                         result.thumbnail = nullptr;
                                     }
                                     return true;
                                 });
    g_ctx->results.erase(newEnd, g_ctx->results.end());
    return removed;
}

static bool confirm_and_trash_photo(GtkWindow* parent, const std::string& filename) {
    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Move photo to trash?\n%s",
                                               filename.c_str());

    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        return false;
    }

    GFile *file = g_file_new_for_path(filename.c_str());
    GError *error = NULL;
    const gboolean trashed = g_file_trash(file, NULL, &error);
    g_object_unref(file);

    if (trashed) {
        return true;
    }

    GtkWidget *err_dialog = gtk_message_dialog_new(parent,
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "Failed to move file to trash.\n%s",
                                                   error ? error->message : "Unknown error");
    gtk_dialog_run(GTK_DIALOG(err_dialog));
    gtk_widget_destroy(err_dialog);
    if (error) {
        g_error_free(error);
    }
    return false;
}

static bool trash_photo(GtkWindow* parent, const std::string& filename, std::string* errorMessage = nullptr) {
    GFile *file = g_file_new_for_path(filename.c_str());
    GError *error = NULL;
    const gboolean trashed = g_file_trash(file, NULL, &error);
    g_object_unref(file);

    if (trashed) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = error ? error->message : "Unknown error";
    }

    if (error) {
        g_error_free(error);
    }
    return false;
}

static bool delete_result_by_filename(const std::string& filename, GtkWindow* parent) {
    if (!confirm_and_trash_photo(parent, filename)) {
        return false;
    }

    bool removedWasBlurry = false;
    if (remove_result_by_filename(filename, &removedWasBlurry)) {
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

    int sharpFiles = 0;
    int blurryFiles = 0;
    for (const ResultData& result : g_ctx->results) {
        if (result.isBlurry) {
            ++blurryFiles;
        } else {
            ++sharpFiles;
        }
    }

    update_summary_bar(sharpFiles, blurryFiles);
}

static void on_delete_blurry_clicked(GtkButton* button, gpointer data) {
    std::vector<std::string> blurryFiles;
    for (const ResultData& result : g_ctx->results) {
        if (result.isBlurry) {
            blurryFiles.push_back(result.filename);
        }
    }

    if (blurryFiles.empty()) {
        update_delete_blurry_button_state();
        return;
    }

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_ctx->window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Move blurry photos to trash?\n%d file(s) will be moved.",
                                               static_cast<int>(blurryFiles.size()));
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        return;
    }

    int failedCount = 0;
    std::string firstError;
    for (const std::string& filename : blurryFiles) {
        std::string errorMessage;
        if (trash_photo(GTK_WINDOW(g_ctx->window), filename, &errorMessage)) {
            remove_result_by_filename(filename);
        } else {
            ++failedCount;
            if (firstError.empty()) {
                firstError = errorMessage;
            }
        }
    }

    rebuild_result_list();
    refresh_summary_from_results_if_visible();
    update_delete_blurry_button_state();

    if (failedCount > 0) {
        GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(g_ctx->window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       "Failed to move %d blurry photo(s) to trash.\n%s",
                                                       failedCount,
                                                       firstError.empty() ? "Unknown error" : firstError.c_str());
        gtk_dialog_run(GTK_DIALOG(err_dialog));
        gtk_widget_destroy(err_dialog);
    }
}

static void update_viewer_navigation_state(ImageContext* ctx) {
    if (!ctx) {
        return;
    }

    const int currentIndex = visible_index_for_filename(ctx->filename);
    const gboolean hasPrevious = currentIndex > 0;
    const gboolean hasNext = visible_result_at(currentIndex + 1) != nullptr;
    gtk_widget_set_sensitive(ctx->previous_button, hasPrevious);
    gtk_widget_set_sensitive(ctx->next_button, hasNext);
}

struct ViewerBounds {
    int windowWidth;
    int windowHeight;
    int imageWidth;
    int imageHeight;
};

static ViewerBounds get_viewer_bounds() {
    GdkRectangle workarea{0, 0, 1024, 768};

    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkMonitor* monitor = nullptr;
        if (g_ctx && g_ctx->window) {
            GdkWindow* mainWindow = gtk_widget_get_window(g_ctx->window);
            if (mainWindow) {
                monitor = gdk_display_get_monitor_at_window(display, mainWindow);
            }
        }

        if (!monitor) {
            monitor = gdk_display_get_primary_monitor(display);
        }

        if (monitor) {
            gdk_monitor_get_workarea(monitor, &workarea);
        }
    }

    const int windowWidth = std::max(640, static_cast<int>(std::lround(workarea.width * 0.90)));
    const int windowHeight = std::max(480, static_cast<int>(std::lround(workarea.height * 0.86)));

    return {
        windowWidth,
        windowHeight,
        std::max(320, windowWidth - 48),
        std::max(240, windowHeight - 120)
    };
}

static void update_viewer_image(ImageContext* ctx) {
    const ViewerBounds bounds = get_viewer_bounds();
    GdkPixbuf *pixbuf = add_status_border(load_preview_pixbuf(ctx->filename, bounds.imageWidth, bounds.imageHeight),
                                          ctx->isBlurry);
    gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->image), pixbuf);
    if (pixbuf) {
        g_object_unref(pixbuf);
    }

    const std::string displayName = path_filename(ctx->filename);
    const std::string statusText = ctx->isBlurry ? "Blurry" : "Sharp";
    const std::string title = displayName + " - " + statusText;

    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), title.c_str());
    gtk_window_resize(GTK_WINDOW(ctx->viewer_window), bounds.windowWidth, bounds.windowHeight);
    update_viewer_navigation_state(ctx);
}

static bool move_viewer_to_adjacent(ImageContext* ctx, bool forward) {
    if (!ctx) {
        return false;
    }

    const int currentIndex = visible_index_for_filename(ctx->filename);
    if (currentIndex < 0) {
        return false;
    }

    const int targetIndex = forward ? currentIndex + 1 : currentIndex - 1;
    const ResultData* target = visible_result_at(targetIndex);
    if (!target) {
        return false;
    }

    ctx->filename = target->filename;
    ctx->isBlurry = target->isBlurry;
    update_viewer_image(ctx);

    GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_ctx->list_box), targetIndex);
    if (row) {
        gtk_list_box_select_row(GTK_LIST_BOX(g_ctx->list_box), row);
        gtk_widget_grab_focus(GTK_WIDGET(row));
    }

    return true;
}

static void on_previous_clicked(GtkWidget* widget, gpointer data) {
    move_viewer_to_adjacent(static_cast<ImageContext*>(data), false);
}

static void on_next_clicked(GtkWidget* widget, gpointer data) {
    move_viewer_to_adjacent(static_cast<ImageContext*>(data), true);
}

static void on_viewer_destroy(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    delete ctx;
}

static void on_delete_clicked(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (!ctx) {
        return;
    }

    if (delete_result_by_filename(ctx->filename, GTK_WINDOW(ctx->viewer_window))) {
        gtk_widget_destroy(ctx->viewer_window);
    }
}

static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(ctx->viewer_window);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Page_Up) {
        return move_viewer_to_adjacent(ctx, false) ? TRUE : FALSE;
    }

    if (event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_space) {
        return move_viewer_to_adjacent(ctx, true) ? TRUE : FALSE;
    }

    if (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) {
        on_delete_clicked(widget, data);
        return TRUE;
    }

    return FALSE;
}

static void open_viewer_for_result(const ResultData& result) {
    ImageContext* ctx = new ImageContext();
    ctx->filename = result.filename;
    ctx->isBlurry = result.isBlurry;

    // Create the viewer window
    ctx->viewer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), path_filename(ctx->filename).c_str());
    const ViewerBounds bounds = get_viewer_bounds();
    gtk_window_set_default_size(GTK_WINDOW(ctx->viewer_window), bounds.windowWidth, bounds.windowHeight);
    gtk_window_set_position(GTK_WINDOW(ctx->viewer_window), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_transient_for(GTK_WINDOW(ctx->viewer_window), GTK_WINDOW(g_ctx->window));

    // Ensure memory is freed when window is closed
    g_signal_connect(ctx->viewer_window, "destroy", G_CALLBACK(on_viewer_destroy), ctx);
    g_signal_connect(ctx->viewer_window, "key-press-event", G_CALLBACK(on_viewer_key_press), ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(ctx->viewer_window), vbox);

    ctx->image = gtk_image_new();
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), ctx->image);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    ctx->previous_button = gtk_button_new_with_label("Previous");
    g_signal_connect(ctx->previous_button, "clicked", G_CALLBACK(on_previous_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->previous_button, TRUE, TRUE, 0);

    ctx->next_button = gtk_button_new_with_label("Next");
    g_signal_connect(ctx->next_button, "clicked", G_CALLBACK(on_next_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->next_button, TRUE, TRUE, 0);

    GtkWidget *btn_delete = gtk_button_new_with_label("Delete");
    gtk_widget_set_name(btn_delete, "delete-photo-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_delete), "delete-button");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), btn_delete, TRUE, TRUE, 0);

    update_viewer_image(ctx);

    gtk_widget_show_all(ctx->viewer_window);
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
    const ResultData* result = visible_result_at(index);
    if (result && result->filename == filename) {
        open_viewer_for_result(*result);
    }
}

// Opens the full-size viewer when a row is double-clicked or activated by keyboard
static void on_result_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer data) {
    open_result_row(row);
}

// --- Main GUI Callbacks ---

static gboolean on_file_processed(gpointer data) {
    ResultData* res = static_cast<ResultData*>(data);

    ResultData result = *res;
    result.thumbnail = add_status_border(load_preview_pixbuf(result.filename, 100, 100), result.isBlurry);
    g_ctx->results.push_back(result);

    const ResultData& storedResult = g_ctx->results.back();
    if (g_ctx->sortMode == SortMode::Default && result_visible_for_sort(storedResult)) {
        append_result_row(storedResult, true);
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
    const std::string labelText = "Current Folder: " + directory_name(directory->dirpath);
    gtk_label_set_text(GTK_LABEL(g_ctx->folder_label), labelText.c_str());
    gtk_widget_show(g_ctx->folder_label);
    gtk_widget_show(g_ctx->sort_combo);
    gtk_widget_show(g_ctx->button_delete_blurry);
    gtk_widget_show(g_ctx->directory_box);
    if (g_ctx->button_recheck) {
        gtk_widget_show(g_ctx->button_recheck);
    }
    delete directory;
    return G_SOURCE_REMOVE;
}

static void set_progress_bar(int processedFiles, int totalFiles) {
    if (!g_ctx->progress_bar) return;

    const double fraction = totalFiles > 0
        ? static_cast<double>(processedFiles) / totalFiles
        : 0.0;
    const int percent = static_cast<int>(std::lround(fraction * 100.0));
    const std::string text = totalFiles > 0
        ? std::to_string(processedFiles) + " / " + std::to_string(totalFiles) + " (" + std::to_string(percent) + "%)"
        : "0 / 0";

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_ctx->progress_bar), std::clamp(fraction, 0.0, 1.0));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_ctx->progress_bar), text.c_str());
}

static gboolean on_progress_reset(gpointer data) {
    ProgressData* progress = static_cast<ProgressData*>(data);
    g_ctx->scanInProgress = true;
    set_empty_results_visible(false);
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
    if (g_ctx->progress_bar) {
        gtk_widget_show(g_ctx->progress_bar);
    }
    set_progress_bar(0, progress->totalFiles);
    delete progress;
    return G_SOURCE_REMOVE;
}

static gboolean on_progress_updated(gpointer data) {
    ProgressData* progress = static_cast<ProgressData*>(data);
    set_progress_bar(progress->processedFiles, progress->totalFiles);
    delete progress;
    return G_SOURCE_REMOVE;
}

static gboolean on_summary_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    const int width = gtk_widget_get_allocated_width(widget);
    const int height = gtk_widget_get_allocated_height(widget);
    const int barHeight = std::min(8, height);
    const int barY = (height - barHeight) / 2;
    const int total = g_ctx->summarySharp + g_ctx->summaryBlurry;
    if (total <= 0) {
        cairo_set_source_rgb(cr, 0.74, 0.74, 0.74);
        cairo_rectangle(cr, 0, barY, width, barHeight);
        cairo_fill(cr);

        return FALSE;
    }

    const double sharpFraction = total > 0
        ? static_cast<double>(g_ctx->summarySharp) / total
        : 0.0;
    const int sharpWidth = static_cast<int>(std::lround(width * sharpFraction));

    cairo_set_source_rgb(cr, 0.18, 0.58, 0.24);
    cairo_rectangle(cr, 0, barY, sharpWidth, barHeight);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.82, 0.16, 0.16);
    cairo_rectangle(cr, sharpWidth, barY, width - sharpWidth, barHeight);
    cairo_fill(cr);

    return FALSE;
}

static void update_summary_bar(int sharpFiles, int blurryFiles) {
    g_ctx->summarySharp = sharpFiles;
    g_ctx->summaryBlurry = blurryFiles;

    const std::string sharpText = "<span foreground=\"forestgreen\"><b>" + std::to_string(sharpFiles) + "</b></span>";
    const std::string blurryText = "<span foreground=\"red\"><b>" + std::to_string(blurryFiles) + "</b></span>";

    gtk_label_set_markup(GTK_LABEL(g_ctx->summary_sharp_label), sharpText.c_str());
    gtk_label_set_markup(GTK_LABEL(g_ctx->summary_blurry_label), blurryText.c_str());
    gtk_widget_queue_draw(g_ctx->summary_bar);
    gtk_widget_show(g_ctx->summary_sharp_label);
    gtk_widget_show(g_ctx->summary_bar);
    gtk_widget_show(g_ctx->summary_blurry_label);
    gtk_widget_show(g_ctx->summary_box);
}

static bool focus_first_result_row();

static gboolean on_scan_finished(gpointer data) {
    SummaryData* summary = static_cast<SummaryData*>(data);
    g_ctx->scanInProgress = false;

    gtk_button_set_label(GTK_BUTTON(g_ctx->button_select), "Select new folder for analysis");
    gtk_widget_set_sensitive(g_ctx->button_select, TRUE);
    if (g_ctx->button_recheck) {
        bool hasCurrentDir = false;
        {
            std::lock_guard<std::mutex> lock(g_ctx->mtx);
            hasCurrentDir = !g_ctx->currentDir.empty();
        }
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
    update_delete_blurry_button_state();
    if (g_ctx->progress_bar) {
        gtk_widget_hide(g_ctx->progress_bar);
    }
    update_summary_bar(summary->sharpFiles, summary->blurryFiles);
    set_empty_results_visible(summary->sharpFiles + summary->blurryFiles == 0);
    focus_first_result_row();
    delete summary;
    return G_SOURCE_REMOVE;
}

static bool focus_first_result_row() {
    GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_ctx->list_box), 0);
    if (!row) {
        return false;
    }

    gtk_list_box_select_row(GTK_LIST_BOX(g_ctx->list_box), row);
    gtk_widget_grab_focus(GTK_WIDGET(row));
    return true;
}

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

    g_ctx->scanInProgress = true;
    if (g_ctx->list_scrolled_window) {
        gtk_widget_set_sensitive(g_ctx->list_scrolled_window, FALSE);
    }
    if (g_ctx->directory_box) {
        gtk_widget_set_sensitive(g_ctx->directory_box, FALSE);
    }
    gtk_widget_set_sensitive(g_ctx->button_select, FALSE);
    if (g_ctx->button_recheck) {
        gtk_widget_set_sensitive(g_ctx->button_recheck, FALSE);
    }
    gtk_button_set_label(GTK_BUTTON(g_ctx->button_select), "Analysis in progress...");
    set_empty_results_visible(false);
    clear_result_cache();
    g_ctx->sortMode = SortMode::Default;
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_ctx->sort_combo), static_cast<gint>(g_ctx->sortMode));
    gtk_widget_set_sensitive(g_ctx->sort_combo, FALSE);
    update_delete_blurry_button_state();
    clear_result_rows();

    const std::string labelText = "Current Folder: " + directory_name(dirpath);
    gtk_label_set_text(GTK_LABEL(g_ctx->folder_label), labelText.c_str());
    gtk_widget_show(g_ctx->folder_label);
    gtk_widget_show(g_ctx->sort_combo);
    gtk_widget_show(g_ctx->button_delete_blurry);
    gtk_widget_show(g_ctx->directory_box);
    if (g_ctx->button_recheck) {
        gtk_widget_show(g_ctx->button_recheck);
    }
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

static void GtkThreadLoop() {
    int argc = 0; 
    char **argv = nullptr;
    gtk_init(&argc, &argv);
    install_app_css();
    
    g_ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_ctx->window), "Focus Analyzer");
    gtk_window_set_default_size(GTK_WINDOW(g_ctx->window), 800, 600);
    g_signal_connect(g_ctx->window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(g_ctx->window), vbox);

    g_ctx->top_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->top_button_box, FALSE, FALSE, 5);

    g_ctx->button_select = gtk_button_new_with_label("Select folder for analysis");
    gtk_widget_set_hexpand(g_ctx->button_select, TRUE);
    g_signal_connect(g_ctx->button_select, "clicked", G_CALLBACK(on_button_clicked), NULL);
    g_signal_connect(g_ctx->button_select, "key-press-event", G_CALLBACK(on_select_button_key_press), NULL);
    gtk_box_pack_start(GTK_BOX(g_ctx->top_button_box), g_ctx->button_select, TRUE, TRUE, 0);

    g_ctx->button_recheck = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(g_ctx->button_recheck, "Recheck the same folder");
    gtk_widget_set_no_show_all(g_ctx->button_recheck, TRUE);
    gtk_widget_set_sensitive(g_ctx->button_recheck, FALSE);
    g_signal_connect(g_ctx->button_recheck, "clicked", G_CALLBACK(on_recheck_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(g_ctx->top_button_box), g_ctx->button_recheck, FALSE, FALSE, 0);
    gtk_widget_hide(g_ctx->button_recheck);

    g_ctx->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(g_ctx->progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_ctx->progress_bar), "0 / 0");
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->progress_bar, FALSE, FALSE, 5);
    gtk_widget_set_no_show_all(g_ctx->progress_bar, TRUE);
    gtk_widget_hide(g_ctx->progress_bar);

    g_ctx->directory_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_no_show_all(g_ctx->directory_box, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->directory_box, FALSE, FALSE, 0);

    g_ctx->folder_label = gtk_label_new(NULL);
    gtk_widget_set_halign(g_ctx->folder_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(g_ctx->folder_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(g_ctx->folder_label, TRUE);
    gtk_widget_set_no_show_all(g_ctx->folder_label, TRUE);
    gtk_box_pack_start(GTK_BOX(g_ctx->directory_box), g_ctx->folder_label, TRUE, TRUE, 0);

    g_ctx->sort_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_ctx->sort_combo), "Default");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_ctx->sort_combo), "Sharp First");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_ctx->sort_combo), "Blurry First");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_ctx->sort_combo), "Sharp Only");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_ctx->sort_combo), "Blurry Only");
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_ctx->sort_combo), static_cast<gint>(g_ctx->sortMode));
    gtk_widget_set_no_show_all(g_ctx->sort_combo, TRUE);
    g_signal_connect(g_ctx->sort_combo, "changed", G_CALLBACK(on_sort_combo_changed), NULL);
    gtk_box_pack_start(GTK_BOX(g_ctx->directory_box), g_ctx->sort_combo, FALSE, FALSE, 0);

    g_ctx->button_delete_blurry = gtk_button_new_with_label("Delete blurry");
    gtk_widget_set_sensitive(g_ctx->button_delete_blurry, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_ctx->button_delete_blurry), "delete-button");
    g_signal_connect(g_ctx->button_delete_blurry, "clicked", G_CALLBACK(on_delete_blurry_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(g_ctx->directory_box), g_ctx->button_delete_blurry, FALSE, FALSE, 0);

    gtk_widget_hide(g_ctx->directory_box);

    g_ctx->list_overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->list_overlay, TRUE, TRUE, 0);

    g_ctx->list_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_ctx->list_scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(g_ctx->list_overlay), g_ctx->list_scrolled_window);

    g_ctx->list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_ctx->list_box), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(g_ctx->list_box), FALSE);
    g_signal_connect(g_ctx->list_box, "row-activated", G_CALLBACK(on_result_row_activated), NULL);
    g_signal_connect(g_ctx->list_box, "key-press-event", G_CALLBACK(on_result_list_key_press), NULL);

    gtk_container_add(GTK_CONTAINER(g_ctx->list_scrolled_window), g_ctx->list_box);

    g_ctx->empty_results_label = gtk_label_new("No photos found in this folder");
    gtk_widget_set_halign(g_ctx->empty_results_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(g_ctx->empty_results_label, GTK_ALIGN_CENTER);
    gtk_widget_set_no_show_all(g_ctx->empty_results_label, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(g_ctx->list_overlay), g_ctx->empty_results_label);
    gtk_widget_hide(g_ctx->empty_results_label);

    g_ctx->summary_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_no_show_all(g_ctx->summary_box, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), g_ctx->summary_box, FALSE, FALSE, 5);

    g_ctx->summary_sharp_label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(g_ctx->summary_sharp_label), 5);
    gtk_box_pack_start(GTK_BOX(g_ctx->summary_box), g_ctx->summary_sharp_label, FALSE, FALSE, 0);

    g_ctx->summary_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_ctx->summary_bar, -1, 8);
    g_signal_connect(g_ctx->summary_bar, "draw", G_CALLBACK(on_summary_draw), NULL);
    gtk_box_pack_start(GTK_BOX(g_ctx->summary_box), g_ctx->summary_bar, TRUE, TRUE, 0);

    g_ctx->summary_blurry_label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(g_ctx->summary_blurry_label), 5);
    gtk_box_pack_start(GTK_BOX(g_ctx->summary_box), g_ctx->summary_blurry_label, FALSE, FALSE, 0);
    gtk_widget_hide(g_ctx->summary_box);

    gtk_widget_show_all(g_ctx->window);
    gtk_main();
}

VisualGUI::VisualGUI() {
    g_ctx = new GUIContext();
    g_ctx->gtkThread = std::thread(GtkThreadLoop);
}

VisualGUI::~VisualGUI() {
    if (g_ctx && g_ctx->gtkThread.joinable()) {
        g_ctx->gtkThread.join();
    }
    clear_result_cache();
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
