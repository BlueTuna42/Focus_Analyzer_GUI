#include "result_list_view.h"

#include "../utils/path_utils.h"
#include "../viewer/preview_loader.h"

static GtkWidget* create_result_row(const ResultListView& view, const ResultData& result) {
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
    if (view.deleteCallback) {
        g_signal_connect(btn_delete, "clicked", view.deleteCallback, NULL);
    }
    gtk_box_pack_start(GTK_BOX(box), btn_delete, FALSE, FALSE, 0);

    if (fallbackPixbuf) {
        g_object_unref(fallbackPixbuf);
    }

    return row;
}

static GtkWidget* create_result_grid_item(const ResultListView& view, const ResultData& result) {
    const char* color = result.isBlurry ? "red" : "forestgreen";
    const std::string displayName = path_filename(result.filename);

    GdkPixbuf *fallbackPixbuf = nullptr;
    GdkPixbuf *pixbuf = result.thumbnail;
    if (!pixbuf) {
        fallbackPixbuf = add_status_border(load_preview_pixbuf(result.filename, 150, 150), result.isBlurry);
        pixbuf = fallbackPixbuf;
    }

    GtkWidget* child = gtk_flow_box_child_new();
    g_object_set_data_full(G_OBJECT(child), "filename", g_strdup(result.filename.c_str()), g_free);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_container_add(GTK_CONTAINER(child), box);

    GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_size_request(image, 150, 150);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 15);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gchar* labelMarkup = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", color, displayName.c_str());
    gtk_label_set_markup(GTK_LABEL(label), labelMarkup);
    g_free(labelMarkup);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    if (fallbackPixbuf) {
        g_object_unref(fallbackPixbuf);
    }
    return child;
}

static void scroll_results_to_bottom(const ResultListView& view) {
    if (!view.scrolledWindow) {
        return;
    }

    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view.scrolledWindow));
    if (!adjustment) {
        return;
    }

    gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
}

void result_list_view_set_empty_visible(const ResultListView& view, bool visible) {
    if (!view.emptyLabel) {
        return;
    }

    if (visible) {
        gtk_widget_show(view.emptyLabel);
    } else {
        gtk_widget_hide(view.emptyLabel);
    }
}

void result_list_view_append(const ResultListView& view, const ResultData& result, bool autoscroll) {
    if (!view.listBox || !view.flowBox) return;
    result_list_view_set_empty_visible(view, false);

    GtkWidget* list_row = create_result_row(view, result);
    gtk_list_box_insert(GTK_LIST_BOX(view.listBox), list_row, -1);
    
    GtkWidget* grid_item = create_result_grid_item(view, result);
    gtk_flow_box_insert(GTK_FLOW_BOX(view.flowBox), grid_item, -1);

    if (view.viewMode == 0) {
        gtk_widget_show_all(list_row);
        gtk_widget_hide(grid_item);
    } else {
        gtk_widget_show_all(grid_item);
        gtk_widget_hide(list_row);
    }

    if (autoscroll) {
        scroll_results_to_bottom(view);
    }
}

void result_list_view_clear(const ResultListView& view) {
    if (view.listBox) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(view.listBox));
        for (GList* child = children; child != NULL; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
        g_list_free(children);
    }
    if (view.flowBox) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(view.flowBox));
        for (GList* child = children; child != NULL; child = child->next) gtk_widget_destroy(GTK_WIDGET(child->data));
        g_list_free(children);
    }
}

void result_list_view_rebuild(const ResultListView& view, const std::vector<const ResultData*>& visible) {
    result_list_view_clear(view);

    for (const ResultData* result : visible) {
        result_list_view_append(view, *result, false);
    }
}

bool result_list_view_focus_first_row(const ResultListView& view) {
    if (!view.listBox) {
        return false;
    }

    GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(view.listBox), 0);
    if (!row) {
        return false;
    }

    gtk_list_box_select_row(GTK_LIST_BOX(view.listBox), row);
    gtk_widget_grab_focus(GTK_WIDGET(row));
    return true;
}
