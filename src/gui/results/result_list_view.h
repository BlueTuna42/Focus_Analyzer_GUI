#ifndef RESULT_LIST_VIEW_H
#define RESULT_LIST_VIEW_H

#include "result_store.h"

#include <gtk/gtk.h>
#include <vector>

struct ResultListView {
    GtkWidget* listBox = nullptr;
    GtkWidget* flowBox = nullptr; 
    GtkWidget* scrolledWindow = nullptr;
    GtkWidget* emptyLabel = nullptr;
    GCallback deleteCallback = nullptr;
    int viewMode = 0;
};

void result_list_view_set_empty_visible(const ResultListView& view, bool visible);
void result_list_view_append(const ResultListView& view, const ResultData& result, bool autoscroll);
void result_list_view_clear(const ResultListView& view);
void result_list_view_rebuild(const ResultListView& view, const std::vector<const ResultData*>& visible);
bool result_list_view_focus_first_row(const ResultListView& view);

#endif
