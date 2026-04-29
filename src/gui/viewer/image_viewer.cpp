#include "image_viewer.h"

#include "../utils/path_utils.h"
#include "preview_loader.h"

#include <algorithm>
#include <cmath>
#include <gdk/gdkkeysyms.h>
#include <utility>

struct ImageContext {
    std::string filename;
    bool isBlurry = false;
    GtkWidget *viewer_window = nullptr;
    GtkWidget *image = nullptr;
    GtkWidget *scrolled = nullptr; 
    GtkWidget *previous_button = nullptr;
    GtkWidget *next_button = nullptr;
    ImageViewerCallbacks callbacks;

    GdkPixbuf *original_pixbuf = nullptr; 
    GdkPixbuf *cached_scaled_pixbuf = nullptr; // Cache for cascaded downscaling
    double zoom_level = 1.0;
    double cached_zoom_level = -1.0; 
    guint zoom_idle_id = 0; 

    bool is_dragging = false;
    double drag_last_x = 0.0;
    double drag_last_y = 0.0;
};

struct ViewerBounds {
    int windowWidth;
    int windowHeight;
    int imageWidth;
    int imageHeight;
};

static void update_viewer_navigation_state(ImageContext* ctx);
static void load_current_image(ImageContext* ctx);

static ViewerBounds get_viewer_bounds(GtkWindow* parent) {
    GdkRectangle workarea{0, 0, 1024, 768};

    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GdkMonitor* monitor = nullptr;
        if (parent) {
            GdkWindow* parentWindow = gtk_widget_get_window(GTK_WIDGET(parent));
            if (parentWindow) {
                monitor = gdk_display_get_monitor_at_window(display, parentWindow);
            }
        }

        if (!monitor) {
            monitor = gdk_display_get_primary_monitor(display);
        }

        if (monitor) {
            gdk_monitor_get_workarea(monitor, &workarea);
        }
    }

    return {workarea.width, workarea.height, workarea.width - 100, workarea.height - 100};
}

static void apply_zoom_sync(ImageContext* ctx) {
    if (!ctx->original_pixbuf) return;

    if (std::abs(ctx->zoom_level - ctx->cached_zoom_level) < 0.001 && ctx->cached_scaled_pixbuf) {
        return;
    }

    int orig_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
    int orig_h = gdk_pixbuf_get_height(ctx->original_pixbuf);

    const int MAX_DIMENSION = 16384;
    double max_zoom_w = static_cast<double>(MAX_DIMENSION) / orig_w;
    double max_zoom_h = static_cast<double>(MAX_DIMENSION) / orig_h;
    double actual_max_zoom = std::min({20.0, max_zoom_w, max_zoom_h});

    if (ctx->zoom_level > actual_max_zoom) {
        ctx->zoom_level = actual_max_zoom;
    }

    int new_w = std::max(1, static_cast<int>(std::round(orig_w * ctx->zoom_level)));
    int new_h = std::max(1, static_cast<int>(std::round(orig_h * ctx->zoom_level)));

    GdkInterpType interp = GDK_INTERP_NEAREST;
    GdkPixbuf* source_pixbuf = ctx->original_pixbuf;

    if (ctx->zoom_level <= 1.0) {
        interp = GDK_INTERP_BILINEAR;
        if (ctx->cached_scaled_pixbuf && ctx->zoom_level < ctx->cached_zoom_level) {
            source_pixbuf = ctx->cached_scaled_pixbuf;
        }
    }

    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(source_pixbuf, new_w, new_h, interp);
    
    if (ctx->cached_scaled_pixbuf) {
        g_object_unref(ctx->cached_scaled_pixbuf);
    }
    
    ctx->cached_scaled_pixbuf = scaled;
    ctx->cached_zoom_level = ctx->zoom_level;

    gtk_image_set_from_pixbuf(GTK_IMAGE(ctx->image), scaled);
}

static gboolean apply_zoom_timeout(gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    ctx->zoom_idle_id = 0;
    apply_zoom_sync(ctx);
    return G_SOURCE_REMOVE;
}

static void request_zoom_update(ImageContext* ctx) {
    if (ctx->zoom_idle_id != 0) {
        g_source_remove(ctx->zoom_idle_id);
    }
    ctx->zoom_idle_id = g_timeout_add(15, apply_zoom_timeout, ctx); 
}

static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    
    // Left mouse button
    if (event->button == 1) {
        // Double click resets zoom to "fit"
        if (event->type == GDK_2BUTTON_PRESS) {
            if (ctx->original_pixbuf) {
                ViewerBounds bounds = get_viewer_bounds(GTK_WINDOW(ctx->viewer_window));
                int orig_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
                int orig_h = gdk_pixbuf_get_height(ctx->original_pixbuf);

                double scale_x = static_cast<double>(bounds.imageWidth) / orig_w;
                double scale_y = static_cast<double>(bounds.imageHeight) / orig_h;
                ctx->zoom_level = std::min({1.0, scale_x, scale_y});
                
                request_zoom_update(ctx);
            }
            ctx->is_dragging = false; // Cancel potential drag on double click
            return TRUE;
        }

        // Single click starts panning
        ctx->is_dragging = true;
        ctx->drag_last_x = event->x_root;
        ctx->drag_last_y = event->y_root;
        
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) {
            GdkDisplay *display = gdk_window_get_display(window);
            GdkCursor *cursor = gdk_cursor_new_from_name(display, "grabbing");
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
        }
        return TRUE;
    }

    return FALSE;
}

static gboolean on_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    
    if (event->button == 1) {
        ctx->is_dragging = false;
        GdkWindow *window = gtk_widget_get_window(widget);
        if (window) {
            gdk_window_set_cursor(window, NULL);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_motion_notify(GtkWidget* widget, GdkEventMotion* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    
    if (ctx->is_dragging) {
        double dx = event->x_root - ctx->drag_last_x;
        double dy = event->y_root - ctx->drag_last_y;

        GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ctx->scrolled));

        gtk_adjustment_set_value(hadj, gtk_adjustment_get_value(hadj) - dx);
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_value(vadj) - dy);

        ctx->drag_last_x = event->x_root;
        ctx->drag_last_y = event->y_root;
        
        return TRUE;
    }
    return FALSE;
}

static gboolean on_viewer_scroll(GtkWidget* widget, GdkEventScroll* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (event->state & GDK_CONTROL_MASK) {
        double zoom_factor = 1.25; 
        
        if (event->direction == GDK_SCROLL_UP) {
            ctx->zoom_level *= zoom_factor;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            ctx->zoom_level /= zoom_factor;
        } else if (event->direction == GDK_SCROLL_SMOOTH) {
            if (event->delta_y < 0) ctx->zoom_level *= zoom_factor;
            else if (event->delta_y > 0) ctx->zoom_level /= zoom_factor;
        }

        ctx->zoom_level = std::max(0.05, std::min(ctx->zoom_level, 20.0));
        request_zoom_update(ctx); 
        
        return TRUE; 
    }
    
    return FALSE; 
}

static void on_previous_clicked(GtkButton* button, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    if (currentIndex > 0) {
        const ResultData* prevResult = ctx->callbacks.visibleAt(currentIndex - 1);
        if (prevResult) {
            ctx->filename = prevResult->filename;
            ctx->isBlurry = prevResult->isBlurry;
            load_current_image(ctx);
            if (ctx->callbacks.selectVisibleRow) {
                ctx->callbacks.selectVisibleRow(currentIndex - 1);
            }
        }
    }
}

static void on_next_clicked(GtkButton* button, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    if (currentIndex >= 0) {
        const ResultData* nextResult = ctx->callbacks.visibleAt(currentIndex + 1);
        if (nextResult) {
            ctx->filename = nextResult->filename;
            ctx->isBlurry = nextResult->isBlurry;
            load_current_image(ctx);
            if (ctx->callbacks.selectVisibleRow) {
                ctx->callbacks.selectVisibleRow(currentIndex + 1);
            }
        }
    }
}

static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(ctx->viewer_window);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Left) {
        if (gtk_widget_get_sensitive(ctx->previous_button)) {
            on_previous_clicked(GTK_BUTTON(ctx->previous_button), ctx);
        }
        return TRUE;
    } else if (event->keyval == GDK_KEY_Right) {
        if (gtk_widget_get_sensitive(ctx->next_button)) {
            on_next_clicked(GTK_BUTTON(ctx->next_button), ctx);
        }
        return TRUE;
    } else if (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_equal || event->keyval == GDK_KEY_KP_Add) {
        ctx->zoom_level *= 1.25;
        ctx->zoom_level = std::min(ctx->zoom_level, 20.0);
        request_zoom_update(ctx);
        return TRUE;
    } else if (event->keyval == GDK_KEY_minus || event->keyval == GDK_KEY_KP_Subtract) {
        ctx->zoom_level /= 1.25;
        ctx->zoom_level = std::max(ctx->zoom_level, 0.05);
        request_zoom_update(ctx);
        return TRUE;
    }

    return FALSE;
}

static void on_viewer_destroy(GtkWidget* widget, gpointer data) {
    ImageContext* ctx = static_cast<ImageContext*>(data);
    
    if (ctx->zoom_idle_id != 0) {
        g_source_remove(ctx->zoom_idle_id);
        ctx->zoom_idle_id = 0;
    }
    
    if (ctx->cached_scaled_pixbuf) {
        g_object_unref(ctx->cached_scaled_pixbuf);
        ctx->cached_scaled_pixbuf = nullptr;
    }

    if (ctx->original_pixbuf) {
        g_object_unref(ctx->original_pixbuf);
        ctx->original_pixbuf = nullptr;
    }
    delete ctx;
}

static void load_current_image(ImageContext* ctx) {
    if (ctx->zoom_idle_id != 0) {
        g_source_remove(ctx->zoom_idle_id);
        ctx->zoom_idle_id = 0;
    }

    if (ctx->cached_scaled_pixbuf) {
        g_object_unref(ctx->cached_scaled_pixbuf);
        ctx->cached_scaled_pixbuf = nullptr;
    }

    if (ctx->original_pixbuf) {
        g_object_unref(ctx->original_pixbuf);
        ctx->original_pixbuf = nullptr;
    }

    ctx->cached_zoom_level = -1.0; 

    ctx->original_pixbuf = load_preview_pixbuf(ctx->filename, 8192, 8192);

    if (ctx->original_pixbuf) {
        ViewerBounds bounds = get_viewer_bounds(GTK_WINDOW(ctx->viewer_window));
        int orig_w = gdk_pixbuf_get_width(ctx->original_pixbuf);
        int orig_h = gdk_pixbuf_get_height(ctx->original_pixbuf);

        double scale_x = static_cast<double>(bounds.imageWidth) / orig_w;
        double scale_y = static_cast<double>(bounds.imageHeight) / orig_h;
        ctx->zoom_level = std::min({1.0, scale_x, scale_y});

        apply_zoom_sync(ctx);
    } else {
        gtk_image_clear(GTK_IMAGE(ctx->image));
    }

    std::string title = path_filename(ctx->filename) + (ctx->isBlurry ? " (Blurry)" : " (Sharp)");
    gtk_window_set_title(GTK_WINDOW(ctx->viewer_window), title.c_str());

    update_viewer_navigation_state(ctx);
}

static void update_viewer_navigation_state(ImageContext* ctx) {
    int currentIndex = ctx->callbacks.visibleIndexForFilename(ctx->filename);
    bool hasPrevious = currentIndex > 0;
    bool hasNext = currentIndex >= 0 && ctx->callbacks.visibleAt(currentIndex + 1) != nullptr;
    
    gtk_widget_set_sensitive(ctx->previous_button, hasPrevious);
    gtk_widget_set_sensitive(ctx->next_button, hasNext);
}

void open_image_viewer(GtkWindow* parent, const ResultData& result, ImageViewerCallbacks callbacks) {
    ImageContext* ctx = new ImageContext();
    ctx->filename = result.filename;
    ctx->isBlurry = result.isBlurry;
    ctx->callbacks = callbacks;

    ctx->viewer_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(ctx->viewer_window), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ctx->viewer_window), TRUE);
    gtk_window_set_modal(GTK_WINDOW(ctx->viewer_window), TRUE);

    ViewerBounds bounds = get_viewer_bounds(parent);
    gtk_window_set_default_size(GTK_WINDOW(ctx->viewer_window), bounds.imageWidth, bounds.imageHeight);

    g_signal_connect(ctx->viewer_window, "destroy", G_CALLBACK(on_viewer_destroy), ctx);
    g_signal_connect(ctx->viewer_window, "key-press-event", G_CALLBACK(on_viewer_key_press), ctx);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(ctx->viewer_window), vbox);

    ctx->image = gtk_image_new();

    GtkWidget *event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), ctx->image);
    gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);

    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_button_press), ctx);
    g_signal_connect(event_box, "button-release-event", G_CALLBACK(on_button_release), ctx);
    g_signal_connect(event_box, "motion-notify-event", G_CALLBACK(on_motion_notify), ctx);
    g_signal_connect(event_box, "scroll-event", G_CALLBACK(on_viewer_scroll), ctx);

    ctx->scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(ctx->scrolled), event_box);
    gtk_box_pack_start(GTK_BOX(vbox), ctx->scrolled, TRUE, TRUE, 0);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 5);

    ctx->previous_button = gtk_button_new_with_label("Previous");
    g_signal_connect(ctx->previous_button, "clicked", G_CALLBACK(on_previous_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->previous_button, TRUE, TRUE, 0);

    ctx->next_button = gtk_button_new_with_label("Next");
    g_signal_connect(ctx->next_button, "clicked", G_CALLBACK(on_next_clicked), ctx);
    gtk_box_pack_start(GTK_BOX(button_box), ctx->next_button, TRUE, TRUE, 0);

    load_current_image(ctx);

    gtk_widget_show_all(ctx->viewer_window);
}