/* -*- Mode: C; c-file-style: "gnu"; tab-width: 8 -*- */
/* GTK - The GIMP Toolkit
 * gtkfilechooserwidget.c: Embeddable file selector widget
 * Copyright (C) 2003, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gtkfilechooserwidget.h"
#include "gtkfilechooserwidgetprivate.h"

#include "gtkbookmarksmanagerprivate.h"
#include "gtkbutton.h"
#include "gtkcelllayout.h"
#include "gtkcellrendererpixbuf.h"
#include "gtkcellrenderertext.h"
#include "gtkdropdown.h"
#include "gtkcssnumbervalueprivate.h"
#include "gtkdragsource.h"
#include "gtkdroptarget.h"
#include "gtkentry.h"
#include "gtkfilechooserprivate.h"
#include "gtkfilechooserdialog.h"
#include "gtkfilechooserentry.h"
#include "gtkfilechooserutils.h"
#include "gtkfilechooser.h"
#include "gtkfilesystemmodel.h"
#include "gtkgrid.h"
#include "gtkicontheme.h"
#include "gtklabel.h"
#include "gtkmarshalers.h"
#include "gtkmessagedialog.h"
#include "gtkmountoperation.h"
#include "gtkpaned.h"
#include "gtkpathbar.h"
#include "gtkplacessidebarprivate.h"
#include "gtkplacesviewprivate.h"
#include "gtkprivate.h"
#include "gtkrecentmanager.h"
#include "gtksearchentryprivate.h"
#include "gtksettings.h"
#include "gtksizegroup.h"
#include "gtksizerequest.h"
#include "gtkstack.h"
#include "gtktooltip.h"
#include "gtktreednd.h"
#include "gtktreeprivate.h"
#include "gtktreeselection.h"
#include "gtkbox.h"
#include "gtkcheckbutton.h"
#include "gtkwindowgroup.h"
#include "gtkintl.h"
#include "gtkshow.h"
#include "gtkmain.h"
#include "gtkscrollable.h"
#include "gtkpopover.h"
#include "gtkrevealer.h"
#include "gtkspinner.h"
#include "gtkseparator.h"
#include "gtkmodelbuttonprivate.h"
#include "gtkgesturelongpress.h"
#include "gtkgestureclick.h"
#include "gtkeventcontrollerkey.h"
#include "gtkdebug.h"
#include "gtkfilechoosererrorstackprivate.h"
#include "gtkentryprivate.h"
#include "gtkroot.h"
#include "gtkbinlayout.h"
#include "gtkwidgetprivate.h"
#include "gtkpopovermenuprivate.h"
#include "gtknative.h"
#include "gtkshortcutcontroller.h"
#include "gtkshortcuttrigger.h"
#include "gtkshortcutaction.h"
#include "gtkshortcut.h"
#include "gtkstringlist.h"

#include <cairo-gobject.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef G_OS_WIN32
#include <io.h>
#endif

/**
 * GtkFileChooserWidget:
 *
 * `GtkFileChooserWidget` is a widget for choosing files.
 *
 * It exposes the [iface@Gtk.FileChooser] interface, and you should
 * use the methods of this interface to interact with the
 * widget.
 *
 * # CSS nodes
 *
 * `GtkFileChooserWidget` has a single CSS node with name filechooser.
 */

/* 150 mseconds of delay */
#define LOCATION_CHANGED_TIMEOUT 150

enum {
  PROP_SEARCH_MODE = 1,
  PROP_SUBTITLE
};

typedef enum {
  LOAD_EMPTY,                   /* There is no model */
  LOAD_PRELOAD,                 /* Model is loading and a timer is running; model isn't inserted into the tree yet */
  LOAD_LOADING,                 /* Timeout expired, model is inserted into the tree, but not fully loaded yet */
  LOAD_FINISHED                 /* Model is fully loaded and inserted into the tree */
} LoadState;

typedef enum {
  RELOAD_EMPTY,                 /* No folder has been set */
  RELOAD_HAS_FOLDER             /* We have a folder, although it may not be completely loaded yet; no need to reload */
} ReloadState;

typedef enum {
  LOCATION_MODE_PATH_BAR,
  LOCATION_MODE_FILENAME_ENTRY
} LocationMode;

typedef enum {
  OPERATION_MODE_BROWSE,
  OPERATION_MODE_SEARCH,
  OPERATION_MODE_ENTER_LOCATION,
  OPERATION_MODE_OTHER_LOCATIONS,
  OPERATION_MODE_RECENT
} OperationMode;

typedef enum {
  STARTUP_MODE_RECENT,
  STARTUP_MODE_CWD
} StartupMode;

typedef enum {
  CLOCK_FORMAT_24,
  CLOCK_FORMAT_12
} ClockFormat;

typedef enum {
  DATE_FORMAT_REGULAR,
  DATE_FORMAT_WITH_TIME
} DateFormat;

typedef enum {
  TYPE_FORMAT_MIME,
  TYPE_FORMAT_DESCRIPTION,
  TYPE_FORMAT_CATEGORY
} TypeFormat;

typedef struct _GtkFileChooserWidgetClass   GtkFileChooserWidgetClass;

struct _GtkFileChooserWidget
{
  GtkWidget parent_instance;

  GtkFileChooserAction action;

  GtkWidget *box;

  GActionGroup *item_actions;

  /* Save mode widgets */
  GtkWidget *save_widgets;
  GtkWidget *save_widgets_table;

  /* The file browsing widgets */
  GtkWidget *browse_widgets_hpaned;
  GtkWidget *browse_header_revealer;
  GtkWidget *browse_header_stack;
  GtkWidget *browse_files_stack;
  GtkWidget *browse_files_swin;
  GtkWidget *browse_files_tree_view;
  GtkWidget *remote_warning_bar;

  GtkWidget *browse_files_popover;

  GtkWidget *browse_new_folder_button;
  GtkSizeGroup *browse_path_bar_size_group;
  GtkWidget *browse_path_bar;
  GtkWidget *new_folder_name_entry;
  GtkWidget *new_folder_create_button;
  GtkWidget *new_folder_error_stack;
  GtkWidget *new_folder_popover;
  GtkWidget *rename_file_name_entry;
  GtkWidget *rename_file_rename_button;
  GtkWidget *rename_file_error_stack;
  GtkWidget *rename_file_popover;
  GFile *rename_file_source_file;

  GtkFileSystemModel *browse_files_model;
  char *browse_files_last_selected_name;

  GtkWidget *places_sidebar;
  GtkWidget *places_view;
  StartupMode startup_mode;

  /* OPERATION_MODE_SEARCH */
  GtkWidget *search_entry;
  GtkWidget *search_spinner;
  guint show_progress_timeout;
  GtkSearchEngine *search_engine;
  GtkQuery *search_query;
  GtkFileSystemModel *search_model;
  GtkFileSystemModel *model_for_search;

  /* OPERATION_MODE_RECENT */
  GtkRecentManager *recent_manager;
  GtkFileSystemModel *recent_model;

  GtkWidget *extra_and_filters;
  GtkWidget *filter_combo_hbox;
  GtkWidget *filter_combo;
  GtkWidget *extra_align;
  GtkWidget *extra_widget;

  GtkWidget *location_entry_box;
  GtkWidget *location_entry;
  LocationMode location_mode;

  GtkWidget *external_entry;
  GtkEventController *external_entry_controller;

  GtkWidget *choice_box;
  GHashTable *choices;

  /* Handles */
  GCancellable *file_list_drag_data_received_cancellable;
  GCancellable *update_current_folder_cancellable;
  GCancellable *should_respond_get_info_cancellable;
  GCancellable *file_exists_get_info_cancellable;

  LoadState load_state;
  ReloadState reload_state;
  guint load_timeout_id;

  OperationMode operation_mode;

  GSList *pending_select_files;

  GtkFileFilter *current_filter;
  GListStore *filters;

  GtkBookmarksManager *bookmarks_manager;

  GFile *current_folder;
  GFile *renamed_file;

  GtkTreeViewColumn *list_name_column;
  GtkCellRenderer *list_name_renderer;
  GtkCellRenderer *list_pixbuf_renderer;
  GtkTreeViewColumn *list_time_column;
  GtkCellRenderer *list_date_renderer;
  GtkCellRenderer *list_time_renderer;
  GtkTreeViewColumn *list_size_column;
  GtkCellRenderer *list_size_renderer;
  GtkTreeViewColumn *list_type_column;
  GtkCellRenderer *list_type_renderer;
  GtkTreeViewColumn *list_location_column;
  GtkCellRenderer *list_location_renderer;

  guint location_changed_id;

  gulong settings_signal_id;

  GSource *focus_entry_idle;

  gulong toplevel_set_focus_id;
  GtkWidget *toplevel_current_focus_widget;
  GtkWidget *toplevel_last_focus_widget;

  int sort_column;
  GtkSortType sort_order;

  ClockFormat clock_format;

  TypeFormat type_format;

  /* Flags */

  guint select_multiple : 1;
  guint show_hidden : 1;
  guint sort_directories_first : 1;
  guint show_time : 1;
  guint list_sort_ascending : 1;
  guint shortcuts_current_folder_active : 1;
  guint show_size_column : 1;
  guint show_type_column : 1;
  guint create_folders : 1;
  guint auto_selecting_first_row : 1;
  guint starting_search : 1;
  guint browse_files_interaction_frozen : 1;
};

struct _GtkFileChooserWidgetClass
{
  GtkWidgetClass parent_class;
};

#define MAX_LOADING_TIME 500

#define DEFAULT_NEW_FOLDER_NAME _("Type name of new folder")

/* Signal IDs */
enum {
  LOCATION_POPUP,
  LOCATION_POPUP_ON_PASTE,
  UP_FOLDER,
  DOWN_FOLDER,
  HOME_FOLDER,
  DESKTOP_FOLDER,
  QUICK_BOOKMARK,
  LOCATION_TOGGLE_POPUP,
  SHOW_HIDDEN,
  SEARCH_SHORTCUT,
  RECENT_SHORTCUT,
  PLACES_SHORTCUT,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define MODEL_ATTRIBUTES "standard::name,standard::type,standard::display-name," \
                         "standard::is-hidden,standard::is-backup,standard::size," \
                         "standard::content-type,standard::fast-content-type,time::modified,time::access," \
                         "access::can-rename,access::can-delete,access::can-trash," \
                         "standard::target-uri"
enum {
  /* the first 4 must be these due to settings caching sort column */
  MODEL_COL_NAME,
  MODEL_COL_SIZE,
  MODEL_COL_TYPE,
  MODEL_COL_TIME,
  MODEL_COL_FILE,
  MODEL_COL_NAME_COLLATED,
  MODEL_COL_IS_FOLDER,
  MODEL_COL_IS_SENSITIVE,
  MODEL_COL_ICON,
  MODEL_COL_SIZE_TEXT,
  MODEL_COL_DATE_TEXT,
  MODEL_COL_TIME_TEXT,
  MODEL_COL_LOCATION_TEXT,
  MODEL_COL_ELLIPSIZE,
  MODEL_COL_NUM_COLUMNS
};

/* This list of types is passed to _gtk_file_system_model_new*() */
#define MODEL_COLUMN_TYPES                                      \
        MODEL_COL_NUM_COLUMNS,                                  \
        G_TYPE_STRING,            /* MODEL_COL_NAME */          \
        G_TYPE_INT64,             /* MODEL_COL_SIZE */          \
        G_TYPE_STRING,            /* MODEL_COL_TYPE */          \
        G_TYPE_LONG,              /* MODEL_COL_TIME */          \
        G_TYPE_FILE,              /* MODEL_COL_FILE */          \
        G_TYPE_STRING,            /* MODEL_COL_NAME_COLLATED */ \
        G_TYPE_BOOLEAN,           /* MODEL_COL_IS_FOLDER */     \
        G_TYPE_BOOLEAN,           /* MODEL_COL_IS_SENSITIVE */  \
        G_TYPE_ICON,              /* MODEL_COL_ICON */          \
        G_TYPE_STRING,            /* MODEL_COL_SIZE_TEXT */     \
        G_TYPE_STRING,            /* MODEL_COL_DATE_TEXT */     \
        G_TYPE_STRING,            /* MODEL_COL_TIME_TEXT */     \
        G_TYPE_STRING,            /* MODEL_COL_LOCATION_TEXT */ \
        PANGO_TYPE_ELLIPSIZE_MODE /* MODEL_COL_ELLIPSIZE */

#define DEFAULT_RECENT_FILES_LIMIT 50

#define ICON_SIZE 16

static void gtk_file_chooser_widget_iface_init       (GtkFileChooserIface        *iface);

static void     gtk_file_chooser_widget_constructed  (GObject               *object);
static void     gtk_file_chooser_widget_finalize     (GObject               *object);
static void     gtk_file_chooser_widget_set_property (GObject               *object,
                                                       guint                  prop_id,
                                                       const GValue          *value,
                                                       GParamSpec            *pspec);
static void     gtk_file_chooser_widget_get_property (GObject               *object,
                                                       guint                  prop_id,
                                                       GValue                *value,
                                                       GParamSpec            *pspec);
static void     gtk_file_chooser_widget_dispose      (GObject               *object);
static void     gtk_file_chooser_widget_map            (GtkWidget             *widget);
static void     gtk_file_chooser_widget_unmap          (GtkWidget             *widget);
static void     gtk_file_chooser_widget_root           (GtkWidget             *widget);
static void     gtk_file_chooser_widget_unroot         (GtkWidget             *widget);
static void     gtk_file_chooser_widget_css_changed    (GtkWidget             *widget,
                                                        GtkCssStyleChange     *change);

static gboolean       gtk_file_chooser_widget_set_current_folder           (GtkFileChooser    *chooser,
                                                                            GFile             *folder,
                                                                            GError           **error);
static gboolean       gtk_file_chooser_widget_update_current_folder        (GtkFileChooser    *chooser,
                                                                            GFile             *folder,
                                                                            gboolean           keep_trail,
                                                                            gboolean           clear_entry,
                                                                            GError           **error);
static GFile *        gtk_file_chooser_widget_get_current_folder           (GtkFileChooser    *chooser);
static void           gtk_file_chooser_widget_set_current_name             (GtkFileChooser    *chooser,
                                                                            const char        *name);
static char *        gtk_file_chooser_widget_get_current_name             (GtkFileChooser    *chooser);
static gboolean       gtk_file_chooser_widget_select_file                  (GtkFileChooser    *chooser,
                                                                            GFile             *file,
                                                                            GError           **error);
static void           gtk_file_chooser_widget_unselect_file                (GtkFileChooser    *chooser,
                                                                            GFile             *file);
static void           gtk_file_chooser_widget_select_all                   (GtkFileChooser    *chooser);
static void           gtk_file_chooser_widget_unselect_all                 (GtkFileChooser    *chooser);
static GListModel *   gtk_file_chooser_widget_get_files                    (GtkFileChooser    *chooser);
static void           gtk_file_chooser_widget_add_filter                   (GtkFileChooser    *chooser,
                                                                            GtkFileFilter     *filter);
static void           gtk_file_chooser_widget_remove_filter                (GtkFileChooser    *chooser,
                                                                            GtkFileFilter     *filter);
static GListModel *   gtk_file_chooser_widget_get_filters                  (GtkFileChooser    *chooser);
static gboolean       gtk_file_chooser_widget_add_shortcut_folder    (GtkFileChooser    *chooser,
                                                                       GFile             *file,
                                                                       GError           **error);
static gboolean       gtk_file_chooser_widget_remove_shortcut_folder (GtkFileChooser    *chooser,
                                                                       GFile             *file,
                                                                       GError           **error);
static GListModel *   gtk_file_chooser_widget_get_shortcut_folders   (GtkFileChooser    *chooser);

static void        gtk_file_chooser_widget_add_choice    (GtkFileChooser  *chooser,
                                                          const char      *id,
                                                          const char      *label,
                                                          const char     **options,
                                                          const char     **option_labels);
static void        gtk_file_chooser_widget_remove_choice (GtkFileChooser  *chooser,
                                                          const char      *id);
static void        gtk_file_chooser_widget_set_choice    (GtkFileChooser  *chooser,
                                                          const char      *id,
                                                          const char      *option);
static const char *gtk_file_chooser_widget_get_choice    (GtkFileChooser  *chooser,
                                                          const char      *id);


static void add_selection_to_recent_list (GtkFileChooserWidget *impl);

static void location_popup_handler  (GtkFileChooserWidget *impl,
                                     const char            *path);
static void location_popup_on_paste_handler (GtkFileChooserWidget *impl);
static void location_toggle_popup_handler   (GtkFileChooserWidget *impl);
static void up_folder_handler       (GtkFileChooserWidget *impl);
static void down_folder_handler     (GtkFileChooserWidget *impl);
static void home_folder_handler     (GtkFileChooserWidget *impl);
static void desktop_folder_handler  (GtkFileChooserWidget *impl);
static void quick_bookmark_handler  (GtkFileChooserWidget *impl,
                                     int                    bookmark_index);
static void show_hidden_handler     (GtkFileChooserWidget *impl);
static void search_shortcut_handler (GtkFileChooserWidget *impl);
static void recent_shortcut_handler (GtkFileChooserWidget *impl);
static void places_shortcut_handler (GtkFileChooserWidget *impl);
static void update_appearance       (GtkFileChooserWidget *impl);
static void check_icon_theme (GtkFileChooserWidget *impl);

static void operation_mode_set (GtkFileChooserWidget *impl, OperationMode mode);
static void location_mode_set  (GtkFileChooserWidget *impl, LocationMode new_mode);

static void set_current_filter   (GtkFileChooserWidget *impl,
                                  GtkFileFilter         *filter);

static void filter_combo_changed       (GtkDropDown          *dropdown,
                                        GParamSpec           *pspec,
                                        GtkFileChooserWidget *impl);

static gboolean list_select_func   (GtkTreeSelection      *selection,
                                    GtkTreeModel          *model,
                                    GtkTreePath           *path,
                                    gboolean               path_currently_selected,
                                    gpointer               data);

static void list_selection_changed     (GtkTreeSelection      *tree_selection,
                                        GtkFileChooserWidget  *impl);
static void list_row_activated         (GtkTreeView           *tree_view,
                                        GtkTreePath           *path,
                                        GtkTreeViewColumn     *column,
                                        GtkFileChooserWidget  *impl);

static void path_bar_clicked (GtkPathBar            *path_bar,
                              GFile                 *file,
                              GFile                 *child,
                              gboolean               child_is_hidden,
                              GtkFileChooserWidget *impl);

static void update_cell_renderer_attributes (GtkFileChooserWidget *impl);

static void load_remove_timer (GtkFileChooserWidget *impl, LoadState new_load_state);
static void browse_files_center_selected_row (GtkFileChooserWidget *impl);

static void location_switch_to_path_bar (GtkFileChooserWidget *impl);

static void stop_loading_and_clear_list_model (GtkFileChooserWidget *impl,
                                               gboolean remove_from_treeview);

static GSList  *get_selected_files           (GtkFileChooserWidget *impl);
static GSList  *get_selected_infos           (GtkFileChooserWidget *impl);

static void     search_setup_widgets         (GtkFileChooserWidget *impl);
static void     search_stop_searching        (GtkFileChooserWidget *impl,
                                              gboolean               remove_query);
static void     search_clear_model           (GtkFileChooserWidget *impl,
                                              gboolean               remove_from_treeview);
static void     search_entry_activate_cb     (GtkFileChooserWidget *impl);
static void     search_entry_stop_cb         (GtkFileChooserWidget *impl);
static void     settings_load                (GtkFileChooserWidget *impl);

static void     show_filters                 (GtkFileChooserWidget *impl,
                                              gboolean               show);

static gboolean recent_files_setting_is_enabled (GtkFileChooserWidget *impl);
static void     recent_start_loading         (GtkFileChooserWidget *impl);
static void     recent_clear_model           (GtkFileChooserWidget *impl,
                                              gboolean               remove_from_treeview);
static gboolean recent_should_respond        (GtkFileChooserWidget *impl);
static void     clear_model_cache            (GtkFileChooserWidget *impl,
                                              int                   column);
static void     set_model_filter             (GtkFileChooserWidget *impl,
                                              GtkFileFilter        *filter);
static void     switch_to_home_dir           (GtkFileChooserWidget *impl);
static void     set_show_hidden              (GtkFileChooserWidget *impl,
                                              gboolean              show_hidden);


G_DEFINE_TYPE_WITH_CODE (GtkFileChooserWidget, gtk_file_chooser_widget, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_FILE_CHOOSER,
                                                gtk_file_chooser_widget_iface_init))

static void
gtk_file_chooser_widget_iface_init (GtkFileChooserIface *iface)
{
  iface->select_file = gtk_file_chooser_widget_select_file;
  iface->unselect_file = gtk_file_chooser_widget_unselect_file;
  iface->select_all = gtk_file_chooser_widget_select_all;
  iface->unselect_all = gtk_file_chooser_widget_unselect_all;
  iface->get_files = gtk_file_chooser_widget_get_files;
  iface->set_current_folder = gtk_file_chooser_widget_set_current_folder;
  iface->get_current_folder = gtk_file_chooser_widget_get_current_folder;
  iface->set_current_name = gtk_file_chooser_widget_set_current_name;
  iface->get_current_name = gtk_file_chooser_widget_get_current_name;
  iface->add_filter = gtk_file_chooser_widget_add_filter;
  iface->remove_filter = gtk_file_chooser_widget_remove_filter;
  iface->get_filters = gtk_file_chooser_widget_get_filters;
  iface->add_shortcut_folder = gtk_file_chooser_widget_add_shortcut_folder;
  iface->remove_shortcut_folder = gtk_file_chooser_widget_remove_shortcut_folder;
  iface->get_shortcut_folders = gtk_file_chooser_widget_get_shortcut_folders;
  iface->add_choice = gtk_file_chooser_widget_add_choice;
  iface->remove_choice = gtk_file_chooser_widget_remove_choice;
  iface->set_choice = gtk_file_chooser_widget_set_choice;
  iface->get_choice = gtk_file_chooser_widget_get_choice;
}

static void
pending_select_files_free (GtkFileChooserWidget *impl)
{
  g_slist_free_full (impl->pending_select_files, g_object_unref);
  impl->pending_select_files = NULL;
}

static void
pending_select_files_add (GtkFileChooserWidget *impl,
                          GFile                *file)
{
  impl->pending_select_files =
    g_slist_prepend (impl->pending_select_files, g_object_ref (file));
}

static void
gtk_file_chooser_widget_finalize (GObject *object)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (object);

  g_clear_object (&impl->item_actions);

  g_clear_pointer (&impl->choices, g_hash_table_unref);

  if (impl->location_changed_id > 0)
    g_source_remove (impl->location_changed_id);

  g_free (impl->browse_files_last_selected_name);

  g_clear_object (&impl->filters);
  g_clear_object (&impl->current_filter);
  g_clear_object (&impl->current_folder);
  g_clear_object (&impl->browse_path_bar_size_group);
  g_clear_object (&impl->renamed_file);

  /* Free all the Models we have */
  stop_loading_and_clear_list_model (impl, FALSE);
  search_clear_model (impl, FALSE);
  recent_clear_model (impl, FALSE);
  g_clear_object (&impl->model_for_search);

  /* stopping the load above should have cleared this */
  g_assert (impl->load_timeout_id == 0);

  G_OBJECT_CLASS (gtk_file_chooser_widget_parent_class)->finalize (object);
}

/* Returns a toplevel GtkWindow, or NULL if none */
static GtkWindow *
get_toplevel (GtkWidget *widget)
{
  GtkRoot *root;

  root = gtk_widget_get_root (widget);
  if (root && GTK_IS_WINDOW (root))
    return GTK_WINDOW (root);
  else
    return NULL;
}

/* Extracts the parent folders out of the supplied list of GtkRecentInfo* items, and returns
 * a list of GFile* for those unique parents.
 */
static GList *
_gtk_file_chooser_extract_recent_folders (GList *infos)
{
  GList *l;
  GList *result;
  GHashTable *folders;

  result = NULL;

  folders = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);

  for (l = infos; l; l = l->next)
    {
      GtkRecentInfo *info = l->data;
      const char *uri;
      GFile *parent;
      GFile *file;

      uri = gtk_recent_info_get_uri (info);

      file = g_file_new_for_uri (uri);
      parent = g_file_get_parent (file);
      g_object_unref (file);

      if (parent)
        {
          if (!g_hash_table_lookup (folders, parent))
            {
              g_hash_table_insert (folders, parent, (gpointer) 1);
              result = g_list_prepend (result, g_object_ref (parent));
            }

          g_object_unref (parent);
        }
    }

  result = g_list_reverse (result);

  g_hash_table_destroy (folders);

  return result;
}

/* Shows an error dialog for the file chooser */
static void
error_message (GtkFileChooserWidget *impl,
               const char            *msg,
               const char            *detail)
{
  GtkWindow *parent = get_toplevel (GTK_WIDGET (impl));
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (parent,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_OK,
                                   "%s",
                                   msg);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            "%s", detail);

  if (parent && gtk_window_has_group (parent))
    gtk_window_group_add_window (gtk_window_get_group (parent),
                                 GTK_WINDOW (dialog));

  gtk_window_present (GTK_WINDOW (dialog));

  g_signal_connect (dialog, "response",
                    G_CALLBACK (gtk_window_destroy),
                    NULL);
}

/* Shows a simple error dialog relative to a path.  Frees the GError as well. */
static void
error_dialog (GtkFileChooserWidget *impl,
              const char            *msg,
              GError                *error)
{
  if (error)
    {
      error_message (impl, msg, error->message);
      g_error_free (error);
    }
}

/* Shows an error dialog about not being able to create a folder */
static void
error_creating_folder_dialog (GtkFileChooserWidget *impl,
                              GFile                 *file,
                              GError                *error)
{
  error_dialog (impl,
                _("The folder could not be created"),
                error);
}

static void
error_with_file_under_nonfolder (GtkFileChooserWidget *impl,
                                 GFile *parent_file)
{
  GError *error;
  char *uri, *msg;

  error = NULL;
  g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
                       _("You need to choose a valid filename."));

  uri = g_file_get_uri (parent_file);
  msg = g_strdup_printf (_("Cannot create a file under %s as it is not a folder"), uri);
  g_free (uri);
  error_dialog (impl, msg, error);
  g_free (msg);
}

static void
error_filename_to_long_dialog (GtkFileChooserWidget *impl)
{
  error_message (impl,
                 _("Cannot create file as the filename is too long"),
                 _("Try using a shorter name."));
}

/* Shows an error about not being able to select a folder because a file with
 * the same name is already there.
 */
static void
error_selecting_folder_over_existing_file_dialog (GtkFileChooserWidget *impl)
{
  error_message (impl,
                 _("You may only select folders"),
                 _("The item that you selected is not a folder try using a different item."));
}

/* Shows an error dialog about not being able to create a filename */
static void
error_building_filename_dialog (GtkFileChooserWidget *impl,
                                GError                *error)
{
  error_dialog (impl, _("Invalid file name"), error);
}

/* Shows an error dialog when we cannot switch to a folder */
static void
error_changing_folder_dialog (GtkFileChooserWidget *impl,
                              GFile                 *file,
                              GError                *error)
{
  error_dialog (impl, _("The folder contents could not be displayed"), error);
}

static void
error_deleting_file (GtkFileChooserWidget *impl,
                     GFile                *file,
                     GError               *error)
{
  error_dialog (impl, _("The file could not be deleted"), error);
}

static void
error_trashing_file (GtkFileChooserWidget *impl,
                     GFile                *file,
                     GError               *error)
{
  error_dialog (impl, _("The file could not be moved to the Trash"), error);
}

/* Changes folders, displaying an error dialog if this fails */
static gboolean
change_folder_and_display_error (GtkFileChooserWidget *impl,
                                 GFile                 *file,
                                 gboolean               clear_entry)
{
  GError *error;
  gboolean result;

  g_return_val_if_fail (G_IS_FILE (file), FALSE);

  /* We copy the path because of this case:
   *
   * list_row_activated()
   *   fetches path from model; path belongs to the model (*)
   *   calls change_folder_and_display_error()
   *     calls gtk_file_chooser_set_current_folder()
   *       changing folders fails, sets model to NULL, thus freeing the path in (*)
   */

  error = NULL;
  result = gtk_file_chooser_widget_update_current_folder (GTK_FILE_CHOOSER (impl), file, TRUE, clear_entry, &error);

  if (!result)
    error_changing_folder_dialog (impl, file, error);

  return result;
}

static void
new_folder_popover_active (GtkWidget            *button,
                           GParamSpec           *pspec,
                           GtkFileChooserWidget *impl)
{
  gtk_editable_set_text (GTK_EDITABLE (impl->new_folder_name_entry), "");
  gtk_widget_set_sensitive (impl->new_folder_create_button, FALSE);
  gtk_file_chooser_error_stack_set_error (GTK_FILE_CHOOSER_ERROR_STACK (impl->new_folder_error_stack),
                                          FALSE,
                                          "no-error");
}

struct FileExistsData
{
  GtkFileChooserWidget *impl;
  gboolean file_exists_and_is_not_folder;
  GFile *parent_file;
  GFile *file;
  GtkWidget *error_stack;
  GtkWidget *button;
};

static void
name_exists_get_info_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct FileExistsData *data = user_data;
  GFileInfo *info;
  GtkFileChooserWidget *impl = data->impl;

  g_clear_object (&impl->file_exists_get_info_cancellable);

  info = g_file_query_info_finish (file, result, NULL);
  if (info != NULL)
    {
      gtk_widget_set_sensitive (data->button, FALSE);
      gtk_file_chooser_error_stack_set_error (GTK_FILE_CHOOSER_ERROR_STACK (data->error_stack),
                                              _gtk_file_info_consider_as_directory (info),
                                              "name-already-exists");
    }
  else
    {
      gtk_widget_set_sensitive (data->button, TRUE);
      /* Don't clear the label here, it may contain a warning */
    }

  g_object_unref (impl);
  g_object_unref (data->file);
  g_free (data);
  g_clear_object (&info);
}

static void
check_valid_child_name (GtkFileChooserWidget *impl,
                        GFile                *parent,
                        const char           *name,
                        gboolean              is_folder,
                        GFile                *original,
                        GtkWidget            *error_stack,
                        GtkWidget            *button)
{
  GtkFileChooserErrorStack *stack = GTK_FILE_CHOOSER_ERROR_STACK (error_stack);

  gtk_widget_set_sensitive (button, FALSE);

  if (name[0] == '\0')
    gtk_file_chooser_error_stack_set_error (stack, FALSE, "no-error");
  else if (strcmp (name, ".") == 0)
    gtk_file_chooser_error_stack_set_error (stack, is_folder, "cannot-be-called-dot");
  else if (strcmp (name, "..") == 0)
    gtk_file_chooser_error_stack_set_error (stack, is_folder, "cannot-be-called-dot-dot");
  else if (strchr (name, '/') != NULL)
    gtk_file_chooser_error_stack_set_error (stack, is_folder, "name-cannot-contain-slash");
  else
    {
      GFile *file;
      GError *error = NULL;

      file = g_file_get_child_for_display_name (parent, name, &error);
      if (file == NULL)
        {
          gtk_file_chooser_error_stack_set_custom_error (stack, error->message);
          g_error_free (error);
        }
      else if (original && g_file_equal (original, file))
        {
          gtk_widget_set_sensitive (button, TRUE);
          g_object_unref (file);
        }
      else
        {
          struct FileExistsData *data;

          /* Warn the user about questionable names that are technically valid */
          if (g_ascii_isspace (name[0]))
            gtk_file_chooser_error_stack_set_error (stack, is_folder, "name-should-not-begin-with-space");
          else if (g_ascii_isspace (name[strlen (name) - 1]))
            gtk_file_chooser_error_stack_set_error (stack, is_folder, "name-should-not-end-with-space");
          else if (name[0] == '.')
            gtk_file_chooser_error_stack_set_error (stack, is_folder, "name-with-dot-is-hidden");
          else
            gtk_file_chooser_error_stack_set_error (stack, FALSE, "no-error");

          data = g_new0 (struct FileExistsData, 1);
          data->impl = g_object_ref (impl);
          data->file = g_object_ref (file);
          data->error_stack = error_stack;
          data->button = button;

          if (impl->file_exists_get_info_cancellable)
            g_cancellable_cancel (impl->file_exists_get_info_cancellable);
          g_clear_object (&impl->file_exists_get_info_cancellable);

          impl->file_exists_get_info_cancellable = g_cancellable_new ();
          g_file_query_info_async (file,
                                   "standard::type",
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT,
                                   impl->file_exists_get_info_cancellable,
                                   name_exists_get_info_cb,
                                   data);

          g_object_unref (file);
        }
    }
}

static void
new_folder_name_changed (GtkEditable          *editable,
                         GtkFileChooserWidget *impl)
{
  check_valid_child_name (impl,
                          impl->current_folder,
                          gtk_editable_get_text (editable),
                          TRUE,
                          NULL,
                          impl->new_folder_error_stack,
                          impl->new_folder_create_button);
}

static void
new_folder_create_clicked (GtkButton            *button,
                           GtkFileChooserWidget *impl)
{
  GError *error = NULL;
  GFile *file;
  const char *name;

  name = gtk_editable_get_text (GTK_EDITABLE (impl->new_folder_name_entry));
  file = g_file_get_child_for_display_name (impl->current_folder, name, &error);

  gtk_popover_popdown (GTK_POPOVER (impl->new_folder_popover));

  if (file)
    {
      if (g_file_make_directory (file, NULL, &error))
        change_folder_and_display_error (impl, file, FALSE);
      else
        error_creating_folder_dialog (impl, file, error);
      g_object_unref (file);
    }
  else
    error_creating_folder_dialog (impl, file, error);
}

struct selection_check_closure {
  GtkFileChooserWidget *impl;
  int num_selected;
  gboolean all_files;
  gboolean all_folders;
};

/* Used from gtk_tree_selection_selected_foreach() */
static void
selection_check_foreach_cb (GtkTreeModel *model,
                            GtkTreePath  *path,
                            GtkTreeIter  *iter,
                            gpointer      data)
{
  struct selection_check_closure *closure;
  gboolean is_folder;
  GFile *file;

  gtk_tree_model_get (model, iter,
                      MODEL_COL_FILE, &file,
                      MODEL_COL_IS_FOLDER, &is_folder,
                      -1);

  if (file == NULL)
    return;

  g_object_unref (file);

  closure = data;
  closure->num_selected++;

  closure->all_folders = closure->all_folders && is_folder;
  closure->all_files = closure->all_files && !is_folder;
}

/* Checks whether the selected items in the file list are all files or all folders */
static void
selection_check (GtkFileChooserWidget *impl,
                 int                   *num_selected,
                 gboolean              *all_files,
                 gboolean              *all_folders)
{
  struct selection_check_closure closure;
  GtkTreeSelection *selection;

  closure.impl = impl;
  closure.num_selected = 0;
  closure.all_files = TRUE;
  closure.all_folders = TRUE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection,
                                       selection_check_foreach_cb,
                                       &closure);

  g_assert (closure.num_selected == 0 || !(closure.all_files && closure.all_folders));

  if (num_selected)
    *num_selected = closure.num_selected;

  if (all_files)
    *all_files = closure.all_files;

  if (all_folders)
    *all_folders = closure.all_folders;
}

static gboolean
file_is_recent_uri (GFile *file)
{
  GFile *recent;
  gboolean same;

  recent = g_file_new_for_uri ("recent:///");
  same = g_file_equal (file, recent);
  g_object_unref (recent);

  return same;
}

static void
places_sidebar_open_location_cb (GtkPlacesSidebar     *sidebar,
                                 GFile                *location,
                                 GtkPlacesOpenFlags    open_flags,
                                 GtkFileChooserWidget *impl)
{
  gboolean clear_entry;

  /* In the Save modes, we want to preserve what the user typed in the filename
   * entry, so that he may choose another folder without erasing his typed name.
   */
  if (impl->location_entry && impl->action != GTK_FILE_CHOOSER_ACTION_SAVE)
    clear_entry = TRUE;
  else
    clear_entry = FALSE;

  location_mode_set (impl, LOCATION_MODE_PATH_BAR);

  if (file_is_recent_uri (location))
    operation_mode_set (impl, OPERATION_MODE_RECENT);
  else
    change_folder_and_display_error (impl, location, clear_entry);
}

/* Callback used when the places sidebar needs us to display an error message */
static void
places_sidebar_show_error_message_cb (GtkPlacesSidebar *sidebar,
                                      const char       *primary,
                                      const char       *secondary,
                                      GtkFileChooserWidget *impl)
{
  error_message (impl, primary, secondary);
}

static gboolean
trigger_location_entry (GtkWidget *widget,
                        GVariant  *arguments,
                        gpointer   unused)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);

  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    return FALSE;

  if (impl->action != GTK_FILE_CHOOSER_ACTION_OPEN &&
      impl->action != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    return FALSE;

  location_popup_handler (impl, g_variant_get_string (arguments, NULL));
  return TRUE;
}

/* Callback used from gtk_tree_selection_selected_foreach(); adds a bookmark for
 * each selected item in the file list.
 */
static void
add_bookmark_foreach_cb (GtkTreeModel *model,
                         GtkTreePath  *path,
                         GtkTreeIter  *iter,
                         gpointer      data)
{
  GtkFileChooserWidget *impl = (GtkFileChooserWidget *) data;
  GFile *file;

  gtk_tree_model_get (model, iter,
                      MODEL_COL_FILE, &file,
                      -1);

  _gtk_bookmarks_manager_insert_bookmark (impl->bookmarks_manager, file, 0, NULL); /* NULL-GError */

  g_object_unref (file);
}

/* Callback used when the "Add to Bookmarks" menu item is activated */
static void
add_to_shortcuts_cb (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));

  gtk_tree_selection_selected_foreach (selection,
                                       add_bookmark_foreach_cb,
                                       impl);
}

typedef struct {
  GtkFileChooserWidget *impl;
  GFile *file;
} ConfirmDeleteData;

static void
on_confirm_delete_response (GtkWidget *dialog,
                            int        response,
                            gpointer   user_data)
{
  ConfirmDeleteData *data = user_data;

  gtk_window_destroy (GTK_WINDOW (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      GError *error = NULL;

      if (!g_file_delete (data->file, NULL, &error))
        error_deleting_file (data->impl, data->file, error);
    }

  g_free (data);
}

static void
confirm_delete (GtkFileChooserWidget *impl,
                GFile                *file,
                GFileInfo            *info)
{
  GtkWindow *toplevel;
  GtkWidget *dialog;
  const char *name;
  ConfirmDeleteData *data;

  name = g_file_info_get_display_name (info);

  toplevel = get_toplevel (GTK_WIDGET (impl));

  dialog = gtk_message_dialog_new (toplevel,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("Are you sure you want to permanently delete “%s”?"),
                                   name);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("If you delete an item, it will be permanently lost."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Delete"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  if (gtk_window_has_group (toplevel))
    gtk_window_group_add_window (gtk_window_get_group (toplevel), GTK_WINDOW (dialog));

  gtk_widget_show (dialog);

  data = g_new (ConfirmDeleteData, 1);
  data->impl = impl;
  data->file = file;

  g_signal_connect (dialog, "response",
                    G_CALLBACK (on_confirm_delete_response),
                    data);
}

static void
delete_selected_cb (GtkTreeModel *model,
                    GtkTreePath  *path,
                    GtkTreeIter  *iter,
                    gpointer      data)
{
  GtkFileChooserWidget *impl = data;
  GFile *file;
  GFileInfo *info;

  file = _gtk_file_system_model_get_file (GTK_FILE_SYSTEM_MODEL (model), iter);
  info = _gtk_file_system_model_get_info (GTK_FILE_SYSTEM_MODEL (model), iter);

  confirm_delete (impl, file, info);
}

static void
delete_file_cb (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, delete_selected_cb, impl);
}

static void
trash_selected_cb (GtkTreeModel *model,
                   GtkTreePath  *path,
                   GtkTreeIter  *iter,
                   gpointer      data)
{
  GtkFileChooserWidget *impl = data;
  GFile *file;
  GError *error = NULL;

  file = _gtk_file_system_model_get_file (GTK_FILE_SYSTEM_MODEL (model), iter);

  if (!g_file_trash (file, NULL, &error))
    error_trashing_file (impl, file, error);
}


static void
trash_file_cb (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, trash_selected_cb, impl);
}

static void
rename_file_name_changed (GtkEntry             *entry,
                          GtkFileChooserWidget *impl)
{
  GFileType file_type;

  file_type = g_file_query_file_type (impl->rename_file_source_file,
                                      G_FILE_QUERY_INFO_NONE, NULL);

  check_valid_child_name (impl,
                          impl->current_folder,
                          gtk_editable_get_text (GTK_EDITABLE (entry)),
                          file_type == G_FILE_TYPE_DIRECTORY,
                          impl->rename_file_source_file,
                          impl->rename_file_error_stack,
                          impl->rename_file_rename_button);
}

static void
rename_file_end (GtkPopover           *popover,
                 GtkFileChooserWidget *impl)
{
  g_object_unref (impl->rename_file_source_file);
}

static void
rename_file_rename_clicked (GtkButton            *button,
                            GtkFileChooserWidget *impl)
{
  GFile *dest;
  const char * new_name;

  gtk_popover_popdown (GTK_POPOVER (impl->rename_file_popover));

  new_name = gtk_editable_get_text (GTK_EDITABLE (impl->rename_file_name_entry));
  dest = g_file_get_parent (impl->rename_file_source_file);

  g_clear_object (&impl->renamed_file);

  if (dest)
    {
      GFile *child;
      GError *error = NULL;

      child = g_file_get_child (dest, new_name);
      if (child)
        {
          if (!g_file_move (impl->rename_file_source_file, child, G_FILE_COPY_NONE,
                            NULL, NULL, NULL, &error))
            error_dialog (impl, _("The file could not be renamed"), error);
          else
            {
              /* Rename succeeded, save renamed file so it will
               * be revealed by our "row-changed" handler */
              impl->renamed_file = g_object_ref (child);
            }

          g_object_unref (child);
        }

      g_object_unref (dest);
    }
}

static void
rename_selected_cb (GtkTreeModel *model,
                    GtkTreePath  *path,
                    GtkTreeIter  *iter,
                    gpointer      data)
{
  GtkFileChooserWidget *impl = data;
  GdkRectangle rect;
  char *filename;
  double x, y;

  gtk_tree_model_get (model, iter,
                      MODEL_COL_FILE, &impl->rename_file_source_file,
                      -1);

  gtk_tree_view_get_cell_area (GTK_TREE_VIEW (impl->browse_files_tree_view),
                               path, impl->list_name_column, &rect);

  gtk_tree_view_convert_bin_window_to_widget_coords (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                                     rect.x, rect.y,
                                                     &rect.x, &rect.y);
  gtk_widget_translate_coordinates (impl->browse_files_tree_view,
                                    GTK_WIDGET (impl),
                                    rect.x, rect.y,
                                    &x, &y);
  rect.x = x;
  rect.y = y;

  filename = g_file_get_basename (impl->rename_file_source_file);
  gtk_editable_set_text (GTK_EDITABLE (impl->rename_file_name_entry), filename);
  g_free (filename);

  gtk_popover_set_pointing_to (GTK_POPOVER (impl->rename_file_popover), &rect);
  gtk_popover_popup (GTK_POPOVER (impl->rename_file_popover));
  gtk_widget_grab_focus (impl->rename_file_popover);
}

static void
rename_file_cb (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkTreeSelection *selection;

  /* insensitive until we change the name */
  gtk_widget_set_sensitive (impl->rename_file_rename_button, FALSE);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, rename_selected_cb, impl);
}

/* Callback used when the "Copy file’s location" menu item is activated */
static void
copy_file_location_cb (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GSList *selected_files = NULL;

  selected_files = get_selected_files (impl);

  if (selected_files)
    {
      GdkClipboard *clipboard;

      clipboard = gtk_widget_get_clipboard (GTK_WIDGET (impl));
      gdk_clipboard_set (clipboard, GDK_TYPE_FILE_LIST, selected_files);
      g_slist_free_full (selected_files, g_object_unref);
    }
}

/* Callback used when the "Visit this file" menu item is activated */
static void
visit_file_cb (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GSList *files;

  files = get_selected_files (impl);

  /* Sigh, just use the first one */
  if (files)
    {
      GFile *file = files->data;

      gtk_file_chooser_widget_select_file (GTK_FILE_CHOOSER (impl), file, NULL); /* NULL-GError */
    }

  g_slist_free_full (files, g_object_unref);
}

/* Callback used when the "Open this folder" menu item is activated */
static void
open_folder_cb (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkWidget *toplevel = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (impl)));
  GSList *files;

  files = get_selected_files (impl);

  /* Sigh, just use the first one */
  if (files && GTK_IS_WINDOW (toplevel))
    {
      GFile *file = files->data;
      char *uri;

      uri = g_file_get_uri (file);
      gtk_show_uri (GTK_WINDOW (toplevel), uri, GDK_CURRENT_TIME);
      g_free (uri);
    }

  g_slist_free_full (files, g_object_unref);
}

/* callback used when the "Show Hidden Files" menu item is toggled */
static void
change_show_hidden_state (GSimpleAction *action,
                          GVariant      *state,
                          gpointer       data)
{
  GtkFileChooserWidget *impl = data;

  g_simple_action_set_state (action, state);
  set_show_hidden (impl, g_variant_get_boolean (state));
}

/* Callback used when the "Show Size Column" menu item is toggled */
static void
change_show_size_state (GSimpleAction *action,
                        GVariant      *state,
                        gpointer       data)
{
  GtkFileChooserWidget *impl = data;

  g_simple_action_set_state (action, state);
  impl->show_size_column = g_variant_get_boolean (state);

  gtk_tree_view_column_set_visible (impl->list_size_column,
                                    impl->show_size_column);
}

/* Callback used when the "Show Type Column" menu item is toggled */
static void
change_show_type_state (GSimpleAction *action,
                        GVariant      *state,
                        gpointer       data)
{
  GtkFileChooserWidget *impl = data;

  g_simple_action_set_state (action, state);
  impl->show_type_column = g_variant_get_boolean (state);

  gtk_tree_view_column_set_visible (impl->list_type_column,
                                    impl->show_type_column);
}

static void
change_sort_directories_first_state (GSimpleAction *action,
                                     GVariant      *state,
                                     gpointer       data)
{
  GtkFileChooserWidget *impl = data;
  GtkTreeSortable *sortable;

  g_simple_action_set_state (action, state);
  impl->sort_directories_first = g_variant_get_boolean (state);

  /* force resorting */
  sortable = GTK_TREE_SORTABLE (impl->browse_files_model);
  if (sortable == NULL)
    return;

  gtk_tree_sortable_set_sort_column_id (sortable,
                                        GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                        impl->sort_order);
  gtk_tree_sortable_set_sort_column_id (sortable,
                                        impl->sort_column,
                                        impl->sort_order);
}

static void
clear_model_cache (GtkFileChooserWidget *impl,
                   int                   column)
{
  if (impl->browse_files_model)
    _gtk_file_system_model_clear_cache (impl->browse_files_model, column);

  if (impl->search_model)
    _gtk_file_system_model_clear_cache (impl->search_model, column);

  if (impl->recent_model)
    _gtk_file_system_model_clear_cache (impl->recent_model, column);
}

static void
set_model_filter (GtkFileChooserWidget *impl,
                  GtkFileFilter        *filter)
{
  if (impl->browse_files_model)
    _gtk_file_system_model_set_filter (impl->browse_files_model, filter);

  if (impl->search_model)
    _gtk_file_system_model_set_filter (impl->search_model, filter);

  if (impl->recent_model)
    _gtk_file_system_model_set_filter (impl->recent_model, filter);
}

static void
update_time_renderer_visible (GtkFileChooserWidget *impl)
{
  g_object_set (impl->list_time_renderer,
                "visible", impl->show_time,
                NULL);
  clear_model_cache (impl, MODEL_COL_DATE_TEXT);
  clear_model_cache (impl, MODEL_COL_TIME_TEXT);
  gtk_widget_queue_draw (impl->browse_files_tree_view);
}

static void
change_show_time_state (GSimpleAction *action,
                        GVariant      *state,
                        gpointer       data)
{
  GtkFileChooserWidget *impl = data;

  g_simple_action_set_state (action, state);
  impl->show_time = g_variant_get_boolean (state);
  update_time_renderer_visible (impl);
}

/* Shows an error dialog about not being able to select a dragged file */
static void
error_selecting_dragged_file_dialog (GtkFileChooserWidget *impl,
                                     GFile                 *file,
                                     GError                *error)
{
  error_dialog (impl,
                _("Could not select file"),
                error);
}

static void
file_list_drag_data_select_files (GtkFileChooserWidget  *impl,
                                  GSList                *files)
{
  GtkFileChooser *chooser = GTK_FILE_CHOOSER (impl);
  GSList *l;

  for (l = files; l; l = l->next)
    {
      GFile *file = l->data;
      GError *error = NULL;

      gtk_file_chooser_widget_select_file (chooser, file, &error);
      if (error)
        error_selecting_dragged_file_dialog (impl, file, error);
    }
}

typedef struct
{
  GtkFileChooserWidget *impl;
  GSList *files;
} FileListDragData;

static void
file_list_drag_data_received_get_info_cb (GObject      *source,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
  GFile *file = G_FILE (source);
  FileListDragData *data = user_data;
  GFileInfo *info;
  GtkFileChooserWidget *impl = data->impl;
  GtkFileChooser *chooser = GTK_FILE_CHOOSER (impl);

  g_clear_object (&impl->file_list_drag_data_received_cancellable);

  info = g_file_query_info_finish (file, result, NULL);
  if (!info)
    goto out;

  if ((impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
       impl->action == GTK_FILE_CHOOSER_ACTION_SAVE) &&
      data->files->next == NULL && _gtk_file_info_consider_as_directory (info))
    change_folder_and_display_error (data->impl, data->files->data, FALSE);
  else
    {
      GError *local_error = NULL;

      gtk_file_chooser_widget_unselect_all (chooser);
      gtk_file_chooser_widget_select_file (chooser, data->files->data, &local_error);
      if (local_error)
        error_selecting_dragged_file_dialog (data->impl, data->files->data, local_error);
      else
        browse_files_center_selected_row (data->impl);
    }

  if (impl->select_multiple)
    file_list_drag_data_select_files (data->impl, data->files->next);

out:
  g_object_unref (data->impl);
  g_slist_free_full (data->files, g_object_unref);
  g_free (data);

  g_clear_object (&info);
}

static gboolean
file_list_drag_drop_cb (GtkDropTarget        *dest,
                        const GValue         *value,
                        double                x,
                        double                y,
                        GtkFileChooserWidget *impl)
{
  GSList *files;
  FileListDragData *data;

  files = g_value_get_boxed (value);

  data = g_new0 (FileListDragData, 1);
  data->impl = g_object_ref (impl);
  data->files = g_slist_copy_deep (files, (GCopyFunc) g_object_ref, NULL);

  if (impl->file_list_drag_data_received_cancellable)
    g_cancellable_cancel (impl->file_list_drag_data_received_cancellable);
  g_clear_object (&impl->file_list_drag_data_received_cancellable);

  impl->file_list_drag_data_received_cancellable = g_cancellable_new ();
  g_file_query_info_async (data->files->data,
                           "standard::type",
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           impl->file_list_drag_data_received_cancellable,
                           file_list_drag_data_received_get_info_cb,
                           data);

  return TRUE;
}

/* Sensitizes the "Copy file’s location" and other context menu items if there is actually
 * a selection active.
 */
static void
check_file_list_popover_sensitivity (GtkFileChooserWidget *impl)
{
  int num_selected;
  gboolean all_files;
  gboolean all_folders;
  gboolean active;
  GAction *action, *action2;

  selection_check (impl, &num_selected, &all_files, &all_folders);

  active = (num_selected != 0);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "copy-location");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), active);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "add-shortcut");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), active && all_folders);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "visit");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), active);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "open");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), (num_selected == 1) && all_folders);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "rename");
  if (num_selected == 1)
    {
      GSList *infos;
      GFileInfo *info;

      infos = get_selected_infos (impl);
      info = G_FILE_INFO (infos->data);

      g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                                   g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME));

      g_slist_free_full (infos, g_object_unref);
    }
  else
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "delete");
  action2 = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "trash");

  if (num_selected == 1)
    {
      GSimpleAction *delete_action = G_SIMPLE_ACTION (action);
      GSimpleAction *trash_action = G_SIMPLE_ACTION (action2);
      GSList *infos;
      GFileInfo *info;

      infos = get_selected_infos (impl);
      info = G_FILE_INFO (infos->data);

      if (g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH))
        {
          g_simple_action_set_enabled (trash_action, TRUE);
          g_simple_action_set_enabled (delete_action, FALSE);
        }
      else if (g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE))
        {
          g_simple_action_set_enabled (delete_action, TRUE);
          g_simple_action_set_enabled (trash_action, FALSE);
        }
      else
        {
          g_simple_action_set_enabled (trash_action, FALSE);
          g_simple_action_set_enabled (delete_action, FALSE);
        }

      g_slist_free_full (infos, g_object_unref);
    }
  else
    {
      g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
      g_simple_action_set_enabled (G_SIMPLE_ACTION (action2), FALSE);
    }
}

static GActionEntry entries[] = {
  { "visit", visit_file_cb, NULL, NULL, NULL },
  { "open", open_folder_cb, NULL, NULL, NULL },
  { "copy-location", copy_file_location_cb, NULL, NULL, NULL },
  { "add-shortcut", add_to_shortcuts_cb, NULL, NULL, NULL },
  { "rename", rename_file_cb, NULL, NULL, NULL },
  { "delete", delete_file_cb, NULL, NULL, NULL },
  { "trash", trash_file_cb, NULL, NULL, NULL },
  { "toggle-show-hidden", NULL, NULL, "false", change_show_hidden_state },
  { "toggle-show-size", NULL, NULL, "false", change_show_size_state },
  { "toggle-show-type", NULL, NULL, "false", change_show_type_state },
  { "toggle-show-time", NULL, NULL, "false", change_show_time_state },
  { "toggle-sort-dirs-first", NULL, NULL, "false", change_sort_directories_first_state }
};

static void
file_list_build_popover (GtkFileChooserWidget *impl)
{
  GMenu *menu, *section;
  GMenuItem *item;

  if (impl->browse_files_popover)
    return;

  menu = g_menu_new ();
  section = g_menu_new ();
  item = g_menu_item_new (_("_Visit File"), "item.visit");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Open With File Manager"), "item.open");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Copy Location"), "item.copy-location");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Add to Bookmarks"), "item.add-shortcut");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Rename"), "item.rename");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Delete"), "item.delete");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("_Move to Trash"), "item.trash");
  g_menu_append_item (section, item);
  g_object_unref (item);

  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  g_object_unref (section);


  section = g_menu_new ();
  item = g_menu_item_new (_("Show _Hidden Files"), "item.toggle-show-hidden");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("Show _Size Column"), "item.toggle-show-size");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("Show T_ype Column"), "item.toggle-show-type");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("Show _Time"), "item.toggle-show-time");
  g_menu_append_item (section, item);
  g_object_unref (item);

  item = g_menu_item_new (_("Sort _Folders Before Files"), "item.toggle-sort-dirs-first");
  g_menu_append_item (section, item);
  g_object_unref (item);

  g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  g_object_unref (section);

  impl->browse_files_popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_widget_set_parent (impl->browse_files_popover, GTK_WIDGET (impl));
  g_object_unref (menu);
}

/* Updates the popover for the file list, creating it if necessary */
static void
file_list_update_popover (GtkFileChooserWidget *impl)
{
  GAction *action;
  gboolean state;

  file_list_build_popover (impl);
  check_file_list_popover_sensitivity (impl);

  /* The sensitivity of the Add to Bookmarks item is set in
   * bookmarks_check_add_sensitivity()
   */
  state = impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
          impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER ||
          impl->operation_mode != OPERATION_MODE_BROWSE;

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "rename");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !state);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "delete");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !state);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "trash");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !state);

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "visit");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), (impl->operation_mode != OPERATION_MODE_BROWSE));

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "toggle-show-hidden");
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (impl->show_hidden));

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "toggle-show-size");
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (impl->show_size_column));

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "toggle-show-type");
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (impl->show_type_column));

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "toggle-show-time");
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (impl->show_time));

  action = g_action_map_lookup_action (G_ACTION_MAP (impl->item_actions), "toggle-sort-dirs-first");
  g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (impl->sort_directories_first));
}

static void
file_list_show_popover (GtkFileChooserWidget *impl,
                        double                x,
                        double                y)
{
  GdkRectangle rect;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GList *list;
  GtkTreePath *path;
  graphene_rect_t bounds;

  if (!gtk_widget_compute_bounds (impl->browse_files_tree_view,
                                  impl->browse_files_tree_view,
                                  &bounds))
    return;

  file_list_update_popover (impl);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  list = gtk_tree_selection_get_selected_rows (selection, &model);
  if (list)
    {
      path = list->data;
      gtk_tree_view_get_cell_area (GTK_TREE_VIEW (impl->browse_files_tree_view), path, NULL, &rect);
      gtk_tree_view_convert_bin_window_to_widget_coords (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                                         rect.x, rect.y,
                                                         &rect.x, &rect.y);
      gtk_widget_translate_coordinates (impl->browse_files_tree_view,
                                        GTK_WIDGET (impl),
                                        rect.x, rect.y,
                                        &x, &y);

      rect.x = CLAMP (x - 20, 0, bounds.size.width - 40);
      rect.y = y;
      rect.width = 40;

      g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
    }
  else
    {
      rect.x = x;
      rect.y = y;
      rect.width = 1;
      rect.height = 1;
    }

  gtk_popover_set_pointing_to (GTK_POPOVER (impl->browse_files_popover), &rect);
  gtk_popover_popup (GTK_POPOVER (impl->browse_files_popover));
}

static gboolean
list_popup_menu_cb (GtkWidget *widget,
                    GVariant  *args,
                    gpointer   user_data)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (user_data);
  graphene_rect_t bounds;

  if (gtk_widget_compute_bounds (impl->browse_files_tree_view,
                                 impl->browse_files_tree_view,
                                 &bounds))
    {
      file_list_show_popover (impl, 0.5 * bounds.size.width, 0.5 * bounds.size.height);
      return TRUE;
    }

  return FALSE;
}

static void
files_list_clicked (GtkGesture           *gesture,
                    int                   n_press,
                    double                x,
                    double                y,
                    GtkFileChooserWidget *impl)
{
  list_popup_menu_cb (NULL, NULL, impl);
}

static void
files_list_restrict_clicking (GtkGestureClick      *gesture,
                              int                   n_press,
                              double                x,
                              double                y,
                              GtkFileChooserWidget *impl)
{
  if (impl->browse_files_interaction_frozen)
    gtk_event_controller_reset (GTK_EVENT_CONTROLLER (gesture));
}

static gboolean
files_list_restrict_key_presses (GtkEventControllerKey *controller,
                                 guint                  keyval,
                                 guint                  keycode,
                                 GdkModifierType        state,
                                 GtkFileChooserWidget  *impl)
{
  if (impl->browse_files_interaction_frozen)
    {
      gtk_event_controller_reset (GTK_EVENT_CONTROLLER (controller));
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

/* Callback used when a button is pressed on the file list.  We trap button 3 to
 * bring up a popup menu.
 */

typedef struct {
  GtkFileChooserWidget *impl;
  double x;
  double y;
} PopoverData;

static gboolean
file_list_show_popover_in_idle (gpointer data)
{
  PopoverData *pd = data;

  file_list_show_popover (pd->impl, pd->x, pd->y);
  g_free (data);

  return G_SOURCE_REMOVE;
}

static void
click_cb (GtkGesture           *gesture,
          int                   n_press,
          double                x,
          double                y,
          GtkFileChooserWidget *impl)
{
  PopoverData *pd;

  pd = g_new (PopoverData, 1);
  pd->impl = impl;
  gtk_widget_translate_coordinates (impl->browse_files_tree_view,
                                    GTK_WIDGET (impl),
                                    x, y, &x, &y);
  pd->x = x;
  pd->y = y;

  g_idle_add (file_list_show_popover_in_idle, pd);
}

static void
long_press_cb (GtkGesture           *gesture,
               double                x,
               double                y,
               GtkFileChooserWidget *impl)
{
  file_list_show_popover (impl, x, y);
}

typedef struct {
  OperationMode operation_mode;
  int general_column;
  int model_column;
} ColumnMap;

/* Sets the sort column IDs for the file list; needs to be done whenever we
 * change the model on the treeview.
 */
static void
file_list_set_sort_column_ids (GtkFileChooserWidget *impl)
{

  gtk_tree_view_set_search_column (GTK_TREE_VIEW (impl->browse_files_tree_view), -1);

  gtk_tree_view_column_set_sort_column_id (impl->list_name_column, MODEL_COL_NAME);
  gtk_tree_view_column_set_sort_column_id (impl->list_time_column, MODEL_COL_TIME);
  gtk_tree_view_column_set_sort_column_id (impl->list_size_column, MODEL_COL_SIZE);
  gtk_tree_view_column_set_sort_column_id (impl->list_type_column, MODEL_COL_TYPE);
  gtk_tree_view_column_set_sort_column_id (impl->list_location_column, MODEL_COL_LOCATION_TEXT);
}

static gboolean
file_list_query_tooltip_cb (GtkWidget  *widget,
                            int         x,
                            int         y,
                            gboolean    keyboard_tip,
                            GtkTooltip *tooltip,
                            gpointer    user_data)
{
  GtkFileChooserWidget *impl = user_data;
  GtkTreeModel *model;
  GtkTreePath *path;
  GtkTreeIter iter;
  GFile *file;
  char *filename;

  if (impl->operation_mode == OPERATION_MODE_BROWSE)
    return FALSE;


  if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                          x, y,
                                          keyboard_tip,
                                          &model, &path, &iter))
    return FALSE;

  gtk_tree_model_get (model, &iter,
                      MODEL_COL_FILE, &file,
                      -1);

  if (file == NULL)
    {
      gtk_tree_path_free (path);
      return FALSE;
    }

  filename = g_file_get_path (file);
  gtk_tooltip_set_text (tooltip, filename);
  gtk_tree_view_set_tooltip_row (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                 tooltip,
                                 path);

  g_free (filename);
  g_object_unref (file);
  gtk_tree_path_free (path);

  return TRUE;
}

static void
set_icon_cell_renderer_fixed_size (GtkFileChooserWidget *impl)
{
  int xpad, ypad;

  gtk_cell_renderer_get_padding (impl->list_pixbuf_renderer, &xpad, &ypad);
  gtk_cell_renderer_set_fixed_size (impl->list_pixbuf_renderer,
                                    xpad * 2 + ICON_SIZE,
                                    ypad * 2 + ICON_SIZE);
}

static GtkWidget *
get_accept_action_widget (GtkDialog *dialog,
                          gboolean   sensitive_only)
{
  int response[] = {
    GTK_RESPONSE_ACCEPT,
    GTK_RESPONSE_OK,
    GTK_RESPONSE_YES,
    GTK_RESPONSE_APPLY
  };
  int i;
  GtkWidget *widget;

  for (i = 0; i < G_N_ELEMENTS (response); i++)
    {
      widget = gtk_dialog_get_widget_for_response (dialog, response[i]);
      if (widget)
        {
          if (!sensitive_only)
            return widget;

          if (gtk_widget_is_sensitive (widget))
            return widget;
        }
    }

  return NULL;
}

static void
update_default (GtkFileChooserWidget *impl)
{
  GtkWidget *dialog;
  GtkWidget *button;
  GListModel *files;
  gboolean sensitive;

  dialog = gtk_widget_get_ancestor (GTK_WIDGET (impl), GTK_TYPE_DIALOG);
  if (dialog == NULL)
    return;

  button = get_accept_action_widget (GTK_DIALOG (dialog), FALSE);
  if (button == NULL)
    return;

  files = gtk_file_chooser_get_files (GTK_FILE_CHOOSER (impl));
  sensitive = (g_list_model_get_n_items (files) > 0 ||
               impl->action == GTK_FILE_CHOOSER_ACTION_SAVE ||
               impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_widget_set_sensitive (button, sensitive);

  g_object_unref (files);
}

static gboolean
location_changed_timeout_cb (gpointer user_data)
{
  GtkFileChooserWidget *impl = user_data;

  gtk_file_chooser_unselect_all (GTK_FILE_CHOOSER (impl));

  update_default (impl);
  impl->location_changed_id = 0;

  return G_SOURCE_REMOVE;
}

static void
location_entry_changed_cb (GtkEditable          *editable,
                           GtkFileChooserWidget *impl)
{
  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    {
      operation_mode_set (impl, OPERATION_MODE_BROWSE);
      if (impl->current_folder)
        change_folder_and_display_error (impl, impl->current_folder, FALSE);
      else
        switch_to_home_dir (impl);
    }

  if (impl->action != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      /* Reset location timeout */
      if (impl->location_changed_id > 0)
        g_source_remove (impl->location_changed_id);

      impl->location_changed_id = g_timeout_add (LOCATION_CHANGED_TIMEOUT,
                                                location_changed_timeout_cb,
                                                impl);
      gdk_source_set_static_name_by_id (impl->location_changed_id, "[gtk] location_changed_timeout_cb");
    }
}

static void
location_entry_close_clicked (GtkFileChooserWidget *impl)
{
  location_mode_set (impl, LOCATION_MODE_PATH_BAR);
  gtk_widget_grab_focus (GTK_WIDGET (impl));
}

static void
location_entry_setup (GtkFileChooserWidget *impl)
{
  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
      impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    gtk_entry_set_placeholder_text (GTK_ENTRY (impl->location_entry), _("Location"));

  g_signal_connect (impl->location_entry, "changed",
                    G_CALLBACK (location_entry_changed_cb), impl);
  g_signal_connect_swapped (impl->location_entry, "hide-entry",
                            G_CALLBACK (location_entry_close_clicked), impl);

  _gtk_file_chooser_entry_set_action (GTK_FILE_CHOOSER_ENTRY (impl->location_entry), impl->action);
  _gtk_file_chooser_entry_set_file_filter (GTK_FILE_CHOOSER_ENTRY (impl->location_entry),
                                           impl->current_filter);
  gtk_editable_set_width_chars (GTK_EDITABLE (impl->location_entry), 45);
  gtk_entry_set_activates_default (GTK_ENTRY (impl->location_entry), TRUE);
  gtk_widget_set_hexpand (impl->location_entry, TRUE);
}

static void
location_entry_disconnect (GtkFileChooserWidget *impl)
{
  if (impl->location_entry)
    g_signal_handlers_disconnect_by_func (impl->location_entry, location_entry_changed_cb, impl);
}

static void
location_entry_create (GtkFileChooserWidget *impl)
{
  if (!impl->location_entry)
    {
      gboolean eat_escape;

      eat_escape = impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
                   impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

      impl->location_entry = _gtk_file_chooser_entry_new (TRUE, eat_escape);
      location_entry_setup (impl);
    }
}

static gboolean
forward_key (GtkEventControllerKey *key,
             guint                  keyval,
             guint                  keycode,
             GdkModifierType        modifiers,
             GtkFileChooserWidget  *impl)
{
  /* Since the entry is not a descendent of the file chooser widget
   * in this case, we need to manually make our bindings apply.
   */
  return gtk_event_controller_key_forward (key, GTK_WIDGET (impl));
}

static void
external_entry_setup (GtkFileChooserWidget *impl)
{
  /* Make keybindings (for example, Ctrl+H to toggle showing hidden files)
   * work even when the focus is on the external entry (which is outside
   * the hierarchy of GtkFileChooserWidget) */

  impl->external_entry_controller = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (impl->external_entry_controller,
                                              GTK_PHASE_BUBBLE);
  g_signal_connect (impl->external_entry_controller, "key-pressed",
                    G_CALLBACK (forward_key), impl);
  gtk_widget_add_controller (impl->external_entry, impl->external_entry_controller);
}

static void
external_entry_disconnect (GtkFileChooserWidget *impl)
{
  gtk_widget_remove_controller (impl->external_entry, impl->external_entry_controller);
  impl->external_entry_controller = NULL;
}

/* Creates the widgets specific to Save mode */
static void
save_widgets_create (GtkFileChooserWidget *impl)
{
  GtkWidget *vbox;
  GtkWidget *widget;

  if (impl->save_widgets != NULL ||
      (impl->external_entry && impl->location_entry == impl->external_entry))
    return;

  location_switch_to_path_bar (impl);

  gtk_places_sidebar_set_location (GTK_PLACES_SIDEBAR (impl->places_sidebar), impl->current_folder);

  if (impl->external_entry)
    {
      location_entry_disconnect (impl);
      impl->location_entry = impl->external_entry;
      g_object_add_weak_pointer (G_OBJECT (impl->external_entry), (gpointer *)&impl->location_entry);
      location_entry_setup (impl);
      external_entry_setup (impl);
      return;
    }

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class (vbox, "search-bar");

  impl->save_widgets_table = gtk_grid_new ();
  gtk_box_append (GTK_BOX (vbox), impl->save_widgets_table);
  gtk_grid_set_row_spacing (GTK_GRID (impl->save_widgets_table), 12);
  gtk_grid_set_column_spacing (GTK_GRID (impl->save_widgets_table), 12);

  /* Label */

  widget = gtk_label_new_with_mnemonic (_("_Name:"));
  gtk_widget_set_halign (widget, GTK_ALIGN_START);
  gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
  gtk_grid_attach (GTK_GRID (impl->save_widgets_table), widget, 0, 0, 1, 1);

  /* Location entry */

  location_entry_create (impl);
  gtk_widget_set_hexpand (impl->location_entry, TRUE);
  gtk_grid_attach (GTK_GRID (impl->save_widgets_table), impl->location_entry, 1, 0, 1, 1);
  gtk_widget_show (impl->location_entry);
  gtk_label_set_mnemonic_widget (GTK_LABEL (widget), impl->location_entry);

  impl->save_widgets = vbox;
  gtk_box_insert_child_after (GTK_BOX (impl->box), impl->save_widgets, NULL);
}

/* Destroys the widgets specific to Save mode */
static void
save_widgets_destroy (GtkFileChooserWidget *impl)
{
  if (impl->external_entry && impl->external_entry == impl->location_entry)
    {
      external_entry_disconnect (impl);
      location_entry_disconnect (impl);
      impl->location_entry = NULL;
    }

  if (impl->save_widgets == NULL)
    return;

  gtk_box_remove (GTK_BOX (impl->box), impl->save_widgets);
  impl->save_widgets = NULL;
  impl->save_widgets_table = NULL;
  impl->location_entry = NULL;
}

/* Turns on the path bar widget.  Can be called even if we are already in that
 * mode.
 */
static void
location_switch_to_path_bar (GtkFileChooserWidget *impl)
{
  g_clear_pointer (&impl->location_entry, gtk_widget_unparent);
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "pathbar");
}

/* Turns on the location entry.  Can be called even if we are already in that
 * mode.
 */
static void
location_switch_to_filename_entry (GtkFileChooserWidget *impl)
{
  /* when in search mode, we are not showing the
   * browse_header_box container, so there's no point in switching
   * to it.
   */
  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    return;

  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), TRUE);

  if (!impl->location_entry)
    {
      location_entry_create (impl);
      gtk_box_append (GTK_BOX (impl->location_entry_box), impl->location_entry);
    }

  _gtk_file_chooser_entry_set_base_folder (GTK_FILE_CHOOSER_ENTRY (impl->location_entry), impl->current_folder);

  gtk_widget_show (impl->location_entry);

  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "location");

  gtk_widget_grab_focus (impl->location_entry);
}

/* Sets a new location mode.
 */
static void
location_mode_set (GtkFileChooserWidget *impl,
                   LocationMode new_mode)
{
  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
      impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      GtkWindow *toplevel;
      GtkWidget *current_focus;
      gboolean switch_to_file_list;

      switch (new_mode)
        {
        case LOCATION_MODE_PATH_BAR:

          /* The location_entry will disappear when we switch to path bar mode.  So,
           * we'll focus the file list in that case, to avoid having a window with
           * no focused widget.
           */
          toplevel = get_toplevel (GTK_WIDGET (impl));
          switch_to_file_list = FALSE;
          if (toplevel)
            {
              current_focus = gtk_root_get_focus (GTK_ROOT (toplevel));
              if (!current_focus || current_focus == impl->location_entry)
                switch_to_file_list = TRUE;
            }

          location_switch_to_path_bar (impl);

          if (switch_to_file_list)
            gtk_widget_grab_focus (impl->browse_files_tree_view);

          break;

        case LOCATION_MODE_FILENAME_ENTRY:
          location_switch_to_filename_entry (impl);
          break;

        default:
          g_assert_not_reached ();
          return;
        }
    }

  impl->location_mode = new_mode;
  g_object_notify (G_OBJECT (impl), "subtitle");
}

/* Callback used when the places sidebar asks us to show other locations */
static void
places_sidebar_show_other_locations_with_flags_cb (GtkPlacesSidebar     *sidebar,
                                                   GtkPlacesOpenFlags    open_flags,
                                                   GtkFileChooserWidget *impl)
{
  operation_mode_set (impl, OPERATION_MODE_OTHER_LOCATIONS);
}

static void
location_toggle_popup_handler (GtkFileChooserWidget *impl)
{
  if ((impl->operation_mode == OPERATION_MODE_RECENT ||
       impl->operation_mode == OPERATION_MODE_SEARCH) &&
      (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
       impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER))
    operation_mode_set (impl, OPERATION_MODE_BROWSE);

  /* If the file entry is not visible, show it (it is _always_
   * visible in save modes, handle these first).
   * If it is visible, turn it off only if it is focused.
   * Otherwise, switch to the entry.
   */
  if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
      gtk_widget_grab_focus (impl->location_entry);
    }
  else if (impl->location_mode == LOCATION_MODE_PATH_BAR)
    {
      location_mode_set (impl, LOCATION_MODE_FILENAME_ENTRY);
    }
  else if (impl->location_mode == LOCATION_MODE_FILENAME_ENTRY)
    {
      if (gtk_widget_has_focus (impl->location_entry))
        {
          location_mode_set (impl, LOCATION_MODE_PATH_BAR);
        }
      else
        {
          gtk_widget_grab_focus (impl->location_entry);
        }
    }
}

static void
gtk_file_chooser_widget_constructed (GObject *object)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (object);

  G_OBJECT_CLASS (gtk_file_chooser_widget_parent_class)->constructed (object);

  update_appearance (impl);
}

static void
update_extra_and_filters (GtkFileChooserWidget *impl)
{
  gtk_widget_set_visible (impl->extra_and_filters,
                          gtk_widget_get_visible (impl->extra_align) ||
                          gtk_widget_get_visible (impl->filter_combo_hbox));
}

/* Sets the extra_widget by packing it in the appropriate place */
static void
set_extra_widget (GtkFileChooserWidget *impl,
                  GtkWidget             *extra_widget)
{
  if (extra_widget)
    g_object_ref (extra_widget);

  if (impl->extra_widget)
    {
      gtk_box_remove (GTK_BOX (impl->extra_align), impl->extra_widget);
      g_object_unref (impl->extra_widget);
    }

  impl->extra_widget = extra_widget;
  if (impl->extra_widget)
    {
      gtk_box_append (GTK_BOX (impl->extra_align), impl->extra_widget);
      gtk_widget_show (impl->extra_align);
    }
  else
    gtk_widget_hide (impl->extra_align);

  /* Calls update_extra_and_filters */
  show_filters (impl, impl->filters != NULL);
}

static void
switch_to_home_dir (GtkFileChooserWidget *impl)
{
  const char *home = g_get_home_dir ();
  GFile *home_file;

  if (home == NULL)
    return;

  home_file = g_file_new_for_path (home);

  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (impl), home_file, NULL); /* NULL-GError */

  g_object_unref (home_file);
}

/* Sets the file chooser to multiple selection mode */
static void
set_select_multiple (GtkFileChooserWidget *impl,
                     gboolean               select_multiple)
{
  GtkTreeSelection *selection;
  GtkSelectionMode mode;

  if (select_multiple == impl->select_multiple)
    return;

  mode = select_multiple ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_set_mode (selection, mode);

  gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (impl->browse_files_tree_view), select_multiple);

  impl->select_multiple = select_multiple;
  g_object_notify (G_OBJECT (impl), "select-multiple");
}

/* Takes the folder stored in a row in the recent_model, and puts it in the pathbar */
static void
put_recent_folder_in_pathbar (GtkFileChooserWidget *impl, GtkTreeIter *iter)
{
  GFile *file;

  gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), iter,
                      MODEL_COL_FILE, &file,
                      -1);
  _gtk_path_bar_set_file (GTK_PATH_BAR (impl->browse_path_bar), file, FALSE);
  g_object_unref (file);
}

/* Sets the location bar in the appropriate mode according to the
 * current operation mode and action.  This is the central function
 * for dealing with the pathbar’s widgets; as long as impl->action and
 * impl->operation_mode are set correctly, then calling this function
 * will update all the pathbar’s widgets.
 */
static void
location_bar_update (GtkFileChooserWidget *impl)
{
  gboolean visible = TRUE;
  gboolean create_folder_visible = FALSE;

  switch (impl->operation_mode)
    {
    case OPERATION_MODE_ENTER_LOCATION:
      break;

    case OPERATION_MODE_OTHER_LOCATIONS:
      break;

    case OPERATION_MODE_BROWSE:
      break;

    case OPERATION_MODE_RECENT:
      if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
          GtkTreeSelection *selection;
          gboolean have_selected;
          GtkTreeIter iter;

          selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));

          /* Save mode means single-selection mode, so the following is valid */
          have_selected = gtk_tree_selection_get_selected (selection, NULL, &iter);

          if (have_selected)
            put_recent_folder_in_pathbar (impl, &iter);
        }
      visible = FALSE;
      break;

    case OPERATION_MODE_SEARCH:
      break;

    default:
      g_assert_not_reached ();
      return;
    }

  if (visible)
    {
      if (impl->create_folders
          && impl->action != GTK_FILE_CHOOSER_ACTION_OPEN
          && impl->operation_mode != OPERATION_MODE_RECENT)
        create_folder_visible = TRUE;
    }

  gtk_widget_set_visible (impl->browse_new_folder_button, create_folder_visible);
}

static void
operation_mode_stop (GtkFileChooserWidget *impl,
                     OperationMode         mode)
{
  if (mode == OPERATION_MODE_SEARCH)
    {
      g_clear_object (&impl->model_for_search);
      search_stop_searching (impl, TRUE);
      search_clear_model (impl, TRUE);
      gtk_widget_hide (impl->remote_warning_bar);
    }
}

static void
operation_mode_set_enter_location (GtkFileChooserWidget *impl)
{
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "list");
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "location");
  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), TRUE);
  location_bar_update (impl);
  gtk_widget_set_sensitive (impl->filter_combo, TRUE);
  location_mode_set (impl, LOCATION_MODE_FILENAME_ENTRY);
}

static void
operation_mode_set_browse (GtkFileChooserWidget *impl)
{
  GtkRevealerTransitionType old_revealer_transition_type;

  gtk_places_sidebar_set_location (GTK_PLACES_SIDEBAR (impl->places_sidebar), impl->current_folder);
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "list");
  location_mode_set (impl, LOCATION_MODE_PATH_BAR);
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "pathbar");

  old_revealer_transition_type = gtk_revealer_get_transition_type (GTK_REVEALER (impl->browse_header_revealer));
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    GTK_REVEALER_TRANSITION_TYPE_NONE);
  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), TRUE);
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    old_revealer_transition_type);

  gtk_widget_set_sensitive (impl->filter_combo, TRUE);
  g_object_notify (G_OBJECT (impl), "subtitle");
}

static void
operation_mode_set_search (GtkFileChooserWidget *impl)
{
  GtkWidget *visible_widget;

  g_assert (impl->search_model == NULL);

  visible_widget = gtk_stack_get_visible_child (GTK_STACK (impl->browse_files_stack));

  if (visible_widget != impl->places_view &&
      visible_widget != impl->browse_files_swin)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "list");
    }

  gtk_widget_grab_focus (impl->search_entry);
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "search");
  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), TRUE);
  location_bar_update (impl);
  search_setup_widgets (impl);
  gtk_widget_set_sensitive (impl->filter_combo, FALSE);
}

static void
operation_mode_set_recent (GtkFileChooserWidget *impl)
{
  GFile *file;
  GtkRevealerTransitionType old_revealer_transition_type;

  if (!impl->recent_manager)
    impl->recent_manager = gtk_recent_manager_get_default ();

  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "list");
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "pathbar");

  /* Hide browse_header without a transition */
  old_revealer_transition_type = gtk_revealer_get_transition_type (GTK_REVEALER (impl->browse_header_revealer));
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    GTK_REVEALER_TRANSITION_TYPE_NONE);
  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), FALSE);
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    old_revealer_transition_type);

  location_bar_update (impl);
  recent_start_loading (impl);
  file = g_file_new_for_uri ("recent:///");
  gtk_places_sidebar_set_location (GTK_PLACES_SIDEBAR (impl->places_sidebar), file);
  g_object_notify (G_OBJECT (impl), "subtitle");
  g_object_unref (file);
  gtk_widget_set_sensitive (impl->filter_combo, TRUE);
}

static void
operation_mode_set_other_locations (GtkFileChooserWidget *impl)
{
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "other_locations");
  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_header_stack), "pathbar");
  gtk_revealer_set_reveal_child (GTK_REVEALER (impl->browse_header_revealer), FALSE);
  location_bar_update (impl);
  stop_loading_and_clear_list_model (impl, TRUE);
  search_stop_searching (impl, TRUE);
  recent_clear_model (impl, TRUE);
  search_clear_model (impl, TRUE);
  gtk_widget_set_sensitive (impl->filter_combo, FALSE);
}

static void
operation_mode_set (GtkFileChooserWidget *impl, OperationMode mode)
{
  OperationMode old_mode;

  operation_mode_stop (impl, impl->operation_mode);

  old_mode = impl->operation_mode;
  impl->operation_mode = mode;

  switch (impl->operation_mode)
    {
    case OPERATION_MODE_ENTER_LOCATION:
      operation_mode_set_enter_location (impl);
      break;

    case OPERATION_MODE_OTHER_LOCATIONS:
      operation_mode_set_other_locations (impl);
      break;

    case OPERATION_MODE_BROWSE:
      operation_mode_set_browse (impl);
      break;

    case OPERATION_MODE_SEARCH:
      operation_mode_set_search (impl);
      break;

    case OPERATION_MODE_RECENT:
      operation_mode_set_recent (impl);
      break;

    default:
      g_assert_not_reached ();
      return;
    }

  if ((old_mode == OPERATION_MODE_SEARCH) != (mode == OPERATION_MODE_SEARCH))
    g_object_notify (G_OBJECT (impl), "search-mode");

  g_object_notify (G_OBJECT (impl), "subtitle");
}

/* This function is basically a do_all function.
 *
 * It sets the visibility on all the widgets based on the current state, and
 * moves the custom_widget if needed.
 */
static void
update_appearance (GtkFileChooserWidget *impl)
{
  if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
      save_widgets_create (impl);
      gtk_places_sidebar_set_show_recent (GTK_PLACES_SIDEBAR (impl->places_sidebar), FALSE);

      if (impl->select_multiple)
        {
          g_warning ("Save mode cannot be set in conjunction with multiple selection mode.  "
                     "Re-setting to single selection mode.");
          set_select_multiple (impl, FALSE);
        }

    }
  else if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
           impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      save_widgets_destroy (impl);
      gtk_places_sidebar_set_show_recent (GTK_PLACES_SIDEBAR (impl->places_sidebar), recent_files_setting_is_enabled (impl));
      location_mode_set (impl, impl->location_mode);
    }

  if (impl->location_entry)
    _gtk_file_chooser_entry_set_action (GTK_FILE_CHOOSER_ENTRY (impl->location_entry), impl->action);

  location_bar_update (impl);

  /* This *is* needed; we need to redraw the file list because the "sensitivity"
   * of files may change depending whether we are in a file or folder-only mode.
   */
  gtk_widget_queue_draw (impl->browse_files_tree_view);
}

static char *
gtk_file_chooser_widget_get_subtitle (GtkFileChooserWidget *impl)
{
  char *subtitle = NULL;

  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    {
      char *location;

      location = gtk_places_sidebar_get_location_title (GTK_PLACES_SIDEBAR (impl->places_sidebar));
      if (location)
        {
          subtitle = g_strdup_printf (_("Searching in %s"), location);
          g_free (location);
        }
      else if (impl->current_folder)
        {
          GFileInfo *info;

          info = g_file_query_info (impl->current_folder,
                                    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                    G_FILE_QUERY_INFO_NONE,
                                    NULL,
                                    NULL);
          if (info)
            {
              subtitle = g_strdup_printf (_("Searching in %s"), g_file_info_get_display_name (info));
              g_object_unref (info);
            }
        }

      if (subtitle == NULL)
        subtitle = g_strdup (_("Searching"));
    }
  else if (impl->operation_mode == OPERATION_MODE_ENTER_LOCATION ||
           (impl->operation_mode == OPERATION_MODE_BROWSE &&
            impl->location_mode == LOCATION_MODE_FILENAME_ENTRY))
    {
      subtitle = g_strdup (_("Enter location or URL"));
    }

  return subtitle;
}

static void
set_show_hidden (GtkFileChooserWidget *impl,
                 gboolean              show_hidden)
{
  if (impl->show_hidden != show_hidden)
    {
      impl->show_hidden = show_hidden;

      if (impl->browse_files_model)
        _gtk_file_system_model_set_show_hidden (impl->browse_files_model, show_hidden);
    }
}

static void
gtk_file_chooser_widget_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)

{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (object);

  switch (prop_id)
    {
    case PROP_SEARCH_MODE:
      if (g_value_get_boolean (value))
        operation_mode_set (impl, OPERATION_MODE_SEARCH);
      else
        {
          if (gtk_stack_get_visible_child (GTK_STACK (impl->browse_files_stack)) != impl->places_view)
            {
              operation_mode_set (impl, OPERATION_MODE_BROWSE);

              if (impl->current_folder)
                change_folder_and_display_error (impl, impl->current_folder, FALSE);
               else
                switch_to_home_dir (impl);
            }
          else
            {
              operation_mode_set (impl, OPERATION_MODE_OTHER_LOCATIONS);
            }
        }
      break;

    case GTK_FILE_CHOOSER_PROP_ACTION:
      {
        GtkFileChooserAction action = g_value_get_enum (value);

        if (action != impl->action)
          {
            gtk_file_chooser_widget_unselect_all (GTK_FILE_CHOOSER (impl));

            if (action == GTK_FILE_CHOOSER_ACTION_SAVE &&
                impl->select_multiple)
              {
                g_warning ("Tried to change the file chooser action to SAVE, "
                           "but this is not allowed in multiple selection "
                           "mode. Resetting the file chooser to single "
                           "selection mode.");
                set_select_multiple (impl, FALSE);
              }
            impl->action = action;
            update_cell_renderer_attributes (impl);
            update_appearance (impl);
            settings_load (impl);
          }
      }
      break;

    case GTK_FILE_CHOOSER_PROP_FILTER:
      set_current_filter (impl, g_value_get_object (value));
      break;

    case GTK_FILE_CHOOSER_PROP_SELECT_MULTIPLE:
      {
        gboolean select_multiple = g_value_get_boolean (value);
        if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE &&
            select_multiple)
          {
            g_warning ("Tried to set the file chooser to multiple selection "
                       "mode, but this is not allowed in SAVE mode.  Ignoring "
                       "the change and leaving the file chooser in single "
                       "selection mode.");
            return;
          }

        set_select_multiple (impl, select_multiple);
      }
      break;

    case GTK_FILE_CHOOSER_PROP_CREATE_FOLDERS:
      {
        gboolean create_folders = g_value_get_boolean (value);
        impl->create_folders = create_folders;
        update_appearance (impl);
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_file_chooser_widget_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (object);

  switch (prop_id)
    {
    case PROP_SEARCH_MODE:
      g_value_set_boolean (value, impl->operation_mode == OPERATION_MODE_SEARCH);
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, gtk_file_chooser_widget_get_subtitle (impl));
      break;

    case GTK_FILE_CHOOSER_PROP_ACTION:
      g_value_set_enum (value, impl->action);
      break;

    case GTK_FILE_CHOOSER_PROP_FILTER:
      g_value_set_object (value, impl->current_filter);
      break;

    case GTK_FILE_CHOOSER_PROP_SELECT_MULTIPLE:
      g_value_set_boolean (value, impl->select_multiple);
      break;

    case GTK_FILE_CHOOSER_PROP_CREATE_FOLDERS:
      g_value_set_boolean (value, impl->create_folders);
      break;

    case GTK_FILE_CHOOSER_PROP_FILTERS:
      g_value_set_object (value, impl->filters);
      break;

    case GTK_FILE_CHOOSER_PROP_SHORTCUT_FOLDERS:
      g_value_take_object (value, gtk_file_chooser_get_shortcut_folders (GTK_FILE_CHOOSER (impl)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* This cancels everything that may be going on in the background. */
static void
cancel_all_operations (GtkFileChooserWidget *impl)
{
  pending_select_files_free (impl);

  if (impl->file_list_drag_data_received_cancellable)
    g_cancellable_cancel (impl->file_list_drag_data_received_cancellable);
  g_clear_object (&impl->file_list_drag_data_received_cancellable);
  if (impl->update_current_folder_cancellable)
    g_cancellable_cancel (impl->update_current_folder_cancellable);
  g_clear_object (&impl->update_current_folder_cancellable);
  if (impl->should_respond_get_info_cancellable)
    g_cancellable_cancel (impl->should_respond_get_info_cancellable);
  g_clear_object (&impl->should_respond_get_info_cancellable);
  if (impl->file_exists_get_info_cancellable)
    g_cancellable_cancel (impl->file_exists_get_info_cancellable);
  g_clear_object (&impl->file_exists_get_info_cancellable);

  search_stop_searching (impl, TRUE);
}

/* Removes the settings signal handler.  It's safe to call multiple times */
static void
remove_settings_signal (GtkFileChooserWidget *impl)
{
  if (impl->settings_signal_id)
    {
      GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (impl));
      GtkSettings *settings = gtk_settings_get_for_display (display);

      g_signal_handler_disconnect (settings,
                                   impl->settings_signal_id);
      impl->settings_signal_id = 0;
    }
}

static void
gtk_file_chooser_widget_dispose (GObject *object)
{
  GtkFileChooserWidget *impl = (GtkFileChooserWidget *) object;

  cancel_all_operations (impl);

  /* browse_files_popover is not a template child */
  g_clear_pointer (&impl->browse_files_popover, gtk_widget_unparent);
  g_clear_pointer (&impl->bookmarks_manager, _gtk_bookmarks_manager_free);
  g_clear_object (&impl->extra_widget);

  if (impl->external_entry && impl->location_entry == impl->external_entry)
    {
      external_entry_disconnect (impl);
      location_entry_disconnect (impl);
      impl->external_entry = NULL;
    }

  remove_settings_signal (impl);

  gtk_widget_dispose_template (GTK_WIDGET (impl), GTK_TYPE_FILE_CHOOSER_WIDGET);

  G_OBJECT_CLASS (gtk_file_chooser_widget_parent_class)->dispose (object);
}

/* Handler for GtkWindow::set-focus; this is where we save the last-focused
 * widget on our toplevel.  See gtk_file_chooser_widget_hierarchy_changed()
 */
static void
toplevel_set_focus_cb (GtkWindow            *window,
                       GParamSpec           *pspec,
                       GtkFileChooserWidget *impl)
{
  impl->toplevel_last_focus_widget = impl->toplevel_current_focus_widget;
  impl->toplevel_current_focus_widget = gtk_root_get_focus (GTK_ROOT (window));
}

/* We monitor the focus widget on our toplevel to be able to know which widget
 * was last focused at the time our “should_respond” method gets called.
 */
static void
gtk_file_chooser_widget_root (GtkWidget *widget)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);
  GtkWidget *toplevel;

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->root (widget);

  toplevel = GTK_WIDGET (gtk_widget_get_root (widget));

  g_assert (impl->toplevel_set_focus_id == 0);
  impl->toplevel_set_focus_id = g_signal_connect (toplevel, "notify::focus-widget",
                                                  G_CALLBACK (toplevel_set_focus_cb), impl);
  impl->toplevel_last_focus_widget = NULL;
  impl->toplevel_current_focus_widget = gtk_root_get_focus (GTK_ROOT (toplevel));
}

static void
gtk_file_chooser_widget_unroot (GtkWidget *widget)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);
  GtkWidget *toplevel;

  toplevel = GTK_WIDGET (gtk_widget_get_root (widget));
  if (toplevel && impl->toplevel_set_focus_id != 0)
    {
      g_signal_handler_disconnect (toplevel, impl->toplevel_set_focus_id);
      impl->toplevel_set_focus_id = 0;
      impl->toplevel_last_focus_widget = NULL;
      impl->toplevel_current_focus_widget = NULL;
    }

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->unroot (widget);
}

/* Changes the icons wherever it is needed */
static void
change_icon_theme (GtkFileChooserWidget *impl)
{
  /* the first cell in the first column is the icon column, and we have a fixed size there */
  set_icon_cell_renderer_fixed_size (impl);

  clear_model_cache (impl, MODEL_COL_ICON);
  gtk_widget_queue_resize (impl->browse_files_tree_view);
}

/* Callback used when a GtkSettings value changes */
static void
settings_notify_cb (GObject               *object,
                    GParamSpec            *pspec,
                    GtkFileChooserWidget *impl)
{
  const char *name;

  name = g_param_spec_get_name (pspec);

  if (strcmp (name, "gtk-icon-theme-name") == 0)
    change_icon_theme (impl);
}

/* Installs a signal handler for GtkSettings so that we can monitor changes in
 * the icon theme.
 */
static void
check_icon_theme (GtkFileChooserWidget *impl)
{
  GtkSettings *settings;

  if (impl->settings_signal_id)
    return;

  settings = gtk_widget_get_settings (GTK_WIDGET (impl));
  impl->settings_signal_id = g_signal_connect (settings, "notify",
                                               G_CALLBACK (settings_notify_cb), impl);

  change_icon_theme (impl);
}

static void
gtk_file_chooser_widget_css_changed (GtkWidget         *widget,
                                     GtkCssStyleChange *change)
{
  GtkFileChooserWidget *impl;

  impl = GTK_FILE_CHOOSER_WIDGET (widget);

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->css_changed (widget, change);

  change_icon_theme (impl);
}

static void
set_sort_column (GtkFileChooserWidget *impl)
{
  GtkTreeSortable *sortable;

  sortable = GTK_TREE_SORTABLE (gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view)));

  /* can happen when we're still populating the model */
  if (sortable == NULL)
    return;

  gtk_tree_sortable_set_sort_column_id (sortable,
                                        impl->sort_column,
                                        impl->sort_order);
}

static void
settings_load (GtkFileChooserWidget *impl)
{
  gboolean show_hidden;
  gboolean show_size_column;
  gboolean show_type_column;
  gboolean sort_directories_first;
  DateFormat date_format;
  TypeFormat type_format;
  int sort_column;
  GtkSortType sort_order;
  StartupMode startup_mode;
  int sidebar_width;
  GSettings *settings;

  settings = _gtk_file_chooser_get_settings_for_widget (GTK_WIDGET (impl));

  show_hidden = g_settings_get_boolean (settings, SETTINGS_KEY_SHOW_HIDDEN);
  show_size_column = g_settings_get_boolean (settings, SETTINGS_KEY_SHOW_SIZE_COLUMN);
  show_type_column = g_settings_get_boolean (settings, SETTINGS_KEY_SHOW_TYPE_COLUMN);
  sort_column = g_settings_get_enum (settings, SETTINGS_KEY_SORT_COLUMN);
  sort_order = g_settings_get_enum (settings, SETTINGS_KEY_SORT_ORDER);
  sidebar_width = g_settings_get_int (settings, SETTINGS_KEY_SIDEBAR_WIDTH);
  startup_mode = g_settings_get_enum (settings, SETTINGS_KEY_STARTUP_MODE);
  sort_directories_first = g_settings_get_boolean (settings, SETTINGS_KEY_SORT_DIRECTORIES_FIRST);
  date_format = g_settings_get_enum (settings, SETTINGS_KEY_DATE_FORMAT);
  type_format = g_settings_get_enum (settings, SETTINGS_KEY_TYPE_FORMAT);

  set_show_hidden (impl, show_hidden);

  impl->show_size_column = show_size_column;
  gtk_tree_view_column_set_visible (impl->list_size_column, show_size_column);
  impl->show_type_column = show_type_column;
  gtk_tree_view_column_set_visible (impl->list_type_column, show_type_column);

  impl->sort_column = sort_column;
  impl->sort_order = sort_order;
  impl->startup_mode = startup_mode;
  impl->sort_directories_first = sort_directories_first;
  impl->show_time = date_format == DATE_FORMAT_WITH_TIME;
  impl->clock_format = g_settings_get_enum (settings, "clock-format");
  impl->type_format = type_format;

  /* We don't call set_sort_column() here as the models may not have been
   * created yet.  The individual functions that create and set the models will
   * call set_sort_column() themselves.
   */

  update_time_renderer_visible (impl);
  if (sidebar_width < 0)
    {
      GtkWidget *sidebar = gtk_paned_get_start_child (GTK_PANED (impl->browse_widgets_hpaned));

      gtk_widget_measure (sidebar, GTK_ORIENTATION_HORIZONTAL, -1,
                          NULL, &sidebar_width, NULL, NULL);
    }

  gtk_paned_set_position (GTK_PANED (impl->browse_widgets_hpaned), sidebar_width);
}

static void
settings_save (GtkFileChooserWidget *impl)
{
  GSettings *settings;

  settings = _gtk_file_chooser_get_settings_for_widget (GTK_WIDGET (impl));

  /* All the other state */

  g_settings_set_enum (settings, SETTINGS_KEY_LOCATION_MODE, impl->location_mode);
  g_settings_set_boolean (settings, SETTINGS_KEY_SHOW_HIDDEN, impl->show_hidden);
  g_settings_set_boolean (settings, SETTINGS_KEY_SHOW_SIZE_COLUMN, impl->show_size_column);
  g_settings_set_boolean (settings, SETTINGS_KEY_SHOW_TYPE_COLUMN, impl->show_type_column);
  g_settings_set_boolean (settings, SETTINGS_KEY_SORT_DIRECTORIES_FIRST, impl->sort_directories_first);
  g_settings_set_enum (settings, SETTINGS_KEY_SORT_COLUMN, impl->sort_column);
  g_settings_set_enum (settings, SETTINGS_KEY_SORT_ORDER, impl->sort_order);
  g_settings_set_int (settings, SETTINGS_KEY_SIDEBAR_WIDTH,
                      gtk_paned_get_position (GTK_PANED (impl->browse_widgets_hpaned)));
  g_settings_set_enum (settings, SETTINGS_KEY_DATE_FORMAT, impl->show_time ? DATE_FORMAT_WITH_TIME : DATE_FORMAT_REGULAR);
  g_settings_set_enum (settings, SETTINGS_KEY_TYPE_FORMAT, impl->type_format);

  /* Now apply the settings */
  g_settings_apply (settings);
}

/* Changes the current folder to $CWD */
static void
switch_to_cwd (GtkFileChooserWidget *impl)
{
  char *current_working_dir = g_get_current_dir ();
  GFile *cwd = g_file_new_for_path (current_working_dir);

  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (impl), cwd, NULL);

  g_object_unref (cwd);
  g_free (current_working_dir);
}

static gboolean
recent_files_setting_is_enabled (GtkFileChooserWidget *impl)
{
  GtkSettings *settings;
  gboolean enabled;

  settings = gtk_widget_get_settings (GTK_WIDGET (impl));
  g_object_get (settings, "gtk-recent-files-enabled", &enabled, NULL);
  return enabled;
}

/* Sets the file chooser to showing Recent Files or $CWD, depending on the
 * user’s settings.
 */
static void
set_startup_mode (GtkFileChooserWidget *impl)
{
  GtkRevealerTransitionType revealer_transition;
  GtkStackTransitionType stack_transition;

  /* turn off animations for this setup */
  revealer_transition
    = gtk_revealer_get_transition_type (GTK_REVEALER (impl->browse_header_revealer));
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    GTK_REVEALER_TRANSITION_TYPE_NONE);
  stack_transition
    = gtk_stack_get_transition_type (GTK_STACK (impl->browse_header_stack));
  gtk_stack_set_transition_type (GTK_STACK (impl->browse_header_stack),
                                 GTK_STACK_TRANSITION_TYPE_NONE);

  switch (impl->startup_mode)
    {
    case STARTUP_MODE_RECENT:
      if (gtk_places_sidebar_get_show_recent (GTK_PLACES_SIDEBAR (impl->places_sidebar)))
        {
          operation_mode_set (impl, OPERATION_MODE_RECENT);
          break;
        }

      G_GNUC_FALLTHROUGH;
    case STARTUP_MODE_CWD:
      switch_to_cwd (impl);
      break;

    default:
      g_assert_not_reached ();
    }

  gtk_stack_set_transition_type (GTK_STACK (impl->browse_header_stack),
                                 stack_transition);
  gtk_revealer_set_transition_type (GTK_REVEALER (impl->browse_header_revealer),
                                    revealer_transition);
}

static gboolean
shortcut_exists (GtkFileChooserWidget *impl, GFile *needle)
{
  GListModel *haystack;
  guint n, i;
  gboolean exists;

  exists = FALSE;

  haystack = gtk_places_sidebar_get_shortcuts (GTK_PLACES_SIDEBAR (impl->places_sidebar));
  n = g_list_model_get_n_items (haystack);
  for (i = 0; i < n; i++)
    {
      GFile *hay = g_list_model_get_item (haystack, i);

      if (g_file_equal (hay, needle))
        {
          g_object_unref (hay);
          exists = TRUE;
          break;
        }
      g_object_unref (hay);
    }
  g_object_unref (haystack);

  return exists;
}

static void
add_cwd_to_sidebar_if_needed (GtkFileChooserWidget *impl)
{
  char *cwd;
  GFile *cwd_file;
  GFile *home_file;

  cwd = g_get_current_dir ();
  cwd_file = g_file_new_for_path (cwd);
  g_free (cwd);

  if (shortcut_exists (impl, cwd_file))
    goto out;

  home_file = g_file_new_for_path (g_get_home_dir ());

  /* We only add an item for $CWD if it is different from $HOME.  This way,
   * applications which get launched from a shell in a terminal (by someone who
   * knows what they are doing) will get an item for $CWD in the places sidebar,
   * and "normal" applications launched from the desktop shell (whose $CWD is
   * $HOME) won't get any extra clutter in the sidebar.
   */
  if (!g_file_equal (home_file, cwd_file))
    gtk_places_sidebar_add_shortcut (GTK_PLACES_SIDEBAR (impl->places_sidebar), cwd_file);

  g_object_unref (home_file);

 out:
  g_object_unref (cwd_file);
}

/* GtkWidget::map method */
static void
gtk_file_chooser_widget_map (GtkWidget *widget)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);

  impl->browse_files_interaction_frozen = FALSE;

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->map (widget);

  settings_load (impl);

  add_cwd_to_sidebar_if_needed (impl);

  if (impl->operation_mode == OPERATION_MODE_BROWSE)
    {
      switch (impl->reload_state)
        {
        case RELOAD_EMPTY:
          set_startup_mode (impl);
          break;

        case RELOAD_HAS_FOLDER:
          /* Nothing; we are already loading or loaded, so we
           * don't need to reload
           */
          break;

        default:
          g_assert_not_reached ();
      }
    }
}

/* GtkWidget::unmap method */
static void
gtk_file_chooser_widget_unmap (GtkWidget *widget)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);

  settings_save (impl);

  cancel_all_operations (impl);
  impl->reload_state = RELOAD_EMPTY;

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->unmap (widget);
}

static int
compare_directory (GtkFileSystemModel   *model,
                   GtkTreeIter          *a,
                   GtkTreeIter          *b,
                   GtkFileChooserWidget *impl)
{
  gboolean dir_a, dir_b;

  dir_a = g_value_get_boolean (_gtk_file_system_model_get_value (model, a, MODEL_COL_IS_FOLDER));
  dir_b = g_value_get_boolean (_gtk_file_system_model_get_value (model, b, MODEL_COL_IS_FOLDER));

  if (impl->sort_directories_first && dir_a != dir_b)
    return impl->list_sort_ascending ? (dir_a ? -1 : 1) : (dir_a ? 1 : -1);

  return 0;
}

static int
compare_name (GtkFileSystemModel   *model,
              GtkTreeIter          *a,
              GtkTreeIter          *b,
              GtkFileChooserWidget *impl)
{
  const char *key_a, *key_b;
  int result;

  key_a = g_value_get_string (_gtk_file_system_model_get_value (model, a, MODEL_COL_NAME_COLLATED));
  key_b = g_value_get_string (_gtk_file_system_model_get_value (model, b, MODEL_COL_NAME_COLLATED));

  if (key_a && key_b)
    result = strcmp (key_a, key_b);
  else if (key_a)
    result = 1;
  else if (key_b)
    result = -1;
  else
    result = 0;

  return result;
}

static int
compare_size (GtkFileSystemModel   *model,
              GtkTreeIter          *a,
              GtkTreeIter          *b,
              GtkFileChooserWidget *impl)
{
  gint64 size_a, size_b;

  size_a = g_value_get_int64 (_gtk_file_system_model_get_value (model, a, MODEL_COL_SIZE));
  size_b = g_value_get_int64 (_gtk_file_system_model_get_value (model, b, MODEL_COL_SIZE));

  return size_a < size_b ? -1 : (size_a == size_b ? 0 : 1);
}

static int
compare_type (GtkFileSystemModel   *model,
              GtkTreeIter          *a,
              GtkTreeIter          *b,
              GtkFileChooserWidget *impl)
{
  const char *key_a, *key_b;

  key_a = g_value_get_string (_gtk_file_system_model_get_value (model, a, MODEL_COL_TYPE));
  key_b = g_value_get_string (_gtk_file_system_model_get_value (model, b, MODEL_COL_TYPE));

  return g_strcmp0 (key_a, key_b);
}

static int
compare_time (GtkFileSystemModel   *model,
              GtkTreeIter          *a,
              GtkTreeIter          *b,
              GtkFileChooserWidget *impl)
{
  glong ta, tb;

  ta = g_value_get_long (_gtk_file_system_model_get_value (model, a, MODEL_COL_TIME));
  tb = g_value_get_long (_gtk_file_system_model_get_value (model, b, MODEL_COL_TIME));

  return ta < tb ? -1 : (ta == tb ? 0 : 1);
}

static int
compare_location (GtkFileSystemModel   *model,
                  GtkTreeIter          *a,
                  GtkTreeIter          *b,
                  GtkFileChooserWidget *impl)
{
  const char *key_a, *key_b;

  key_a = g_value_get_string (_gtk_file_system_model_get_value (model, a, MODEL_COL_LOCATION_TEXT));
  key_b = g_value_get_string (_gtk_file_system_model_get_value (model, b, MODEL_COL_LOCATION_TEXT));

  return g_strcmp0 (key_a, key_b);
}

/* Sort callback for the filename column */
static int
name_sort_func (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_directory (fs_model, a, b, impl);

  if (result == 0)
    result = compare_name (fs_model, a, b, impl);

  return result;
}

/* Sort callback for the size column */
static int
size_sort_func (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_directory (fs_model, a, b, impl);

  if (result == 0)
    result = compare_size (fs_model, a, b, impl);

  return result;
}

/* Sort callback for the type column */
static int
type_sort_func (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_directory (fs_model, a, b, impl);

  if (result == 0)
    result = compare_type (fs_model, a, b, impl);

  return result;
}

/* Sort callback for the time column */
static int
time_sort_func (GtkTreeModel *model,
                GtkTreeIter  *a,
                GtkTreeIter  *b,
                gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_directory (fs_model, a, b, impl);

  if (result == 0)
    result = compare_time (fs_model, a, b, impl);

  return result;
}

static int
recent_sort_func (GtkTreeModel *model,
                  GtkTreeIter  *a,
                  GtkTreeIter  *b,
                  gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_time (fs_model, a, b, impl);

  if (result == 0)
    result = compare_name (fs_model, a, b, impl);

  if (result == 0)
    result = compare_location (fs_model, a, b, impl);

  return result;
}

static int
search_sort_func (GtkTreeModel *model,
                  GtkTreeIter  *a,
                  GtkTreeIter  *b,
                  gpointer      user_data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  GtkFileChooserWidget *impl = user_data;
  int result;

  result = compare_location (fs_model, a, b, impl);

  if (result == 0)
    result = compare_name (fs_model, a, b, impl);

  if (result == 0)
    result = compare_time (fs_model, a, b, impl);

  return result;
}

/* Callback used when the sort column changes.  We cache the sort order for use
 * in name_sort_func().
 */
static void
list_sort_column_changed_cb (GtkTreeSortable       *sortable,
                             GtkFileChooserWidget *impl)
{
  int sort_column_id;
  GtkSortType sort_type;

  if (gtk_tree_sortable_get_sort_column_id (sortable, &sort_column_id, &sort_type))
    {
      impl->list_sort_ascending = (sort_type == GTK_SORT_ASCENDING);
      impl->sort_column = sort_column_id;
      impl->sort_order = sort_type;
    }
}

static void
set_busy_cursor (GtkFileChooserWidget *impl,
                 gboolean               busy)
{
  GtkWidget *widget;
  GtkWindow *toplevel;

  toplevel = get_toplevel (GTK_WIDGET (impl));
  widget = GTK_WIDGET (toplevel);
  if (!toplevel || !gtk_widget_get_realized (widget))
    return;

  if (busy)
    gtk_widget_set_cursor_from_name (widget, "progress");
  else
    gtk_widget_set_cursor (widget, NULL);
}

static void
update_columns (GtkFileChooserWidget *impl,
                gboolean              location_visible,
                const char           *time_title)
{
  gboolean need_resize = FALSE;

  if (gtk_tree_view_column_get_visible (impl->list_location_column) != location_visible)
    {
      gtk_tree_view_column_set_visible (impl->list_location_column, location_visible);
      need_resize = TRUE;
    }

  if (g_strcmp0 (gtk_tree_view_column_get_title (impl->list_time_column), time_title) != 0)
    {
      gtk_tree_view_column_set_title (impl->list_time_column, time_title);
      need_resize = TRUE;
    }

  if (need_resize)
    {
      /* This undoes user resizing of columns when the columns change. */
      gtk_tree_view_column_set_expand (impl->list_name_column, TRUE);
      gtk_tree_view_column_set_expand (impl->list_location_column, TRUE);
      gtk_tree_view_columns_autosize (GTK_TREE_VIEW (impl->browse_files_tree_view));
    }
}

/* Creates a sort model to wrap the file system model and sets it on the tree view */
static void
load_set_model (GtkFileChooserWidget *impl)
{
  g_assert (impl->browse_files_model != NULL);

  gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view),
                           GTK_TREE_MODEL (impl->browse_files_model));
  update_columns (impl, FALSE, _("Modified"));
  file_list_set_sort_column_ids (impl);
  set_sort_column (impl);
  impl->list_sort_ascending = TRUE;

  g_set_object (&impl->model_for_search, impl->browse_files_model);
}

/* Timeout callback used when the loading timer expires */
static gboolean
load_timeout_cb (gpointer data)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (data);

  g_assert (impl->load_state == LOAD_PRELOAD);
  g_assert (impl->load_timeout_id != 0);
  g_assert (impl->browse_files_model != NULL);

  impl->load_timeout_id = 0;
  impl->load_state = LOAD_LOADING;

  load_set_model (impl);

  return FALSE;
}

/* Sets up a new load timer for the model and switches to the LOAD_PRELOAD state */
static void
load_setup_timer (GtkFileChooserWidget *impl)
{
  g_assert (impl->load_timeout_id == 0);
  g_assert (impl->load_state != LOAD_PRELOAD);

  impl->load_timeout_id = g_timeout_add (MAX_LOADING_TIME, load_timeout_cb, impl);
  gdk_source_set_static_name_by_id (impl->load_timeout_id, "[gtk] load_timeout_cb");
  impl->load_state = LOAD_PRELOAD;
}

/* Removes the load timeout; changes the impl->load_state to the specified value. */
static void
load_remove_timer (GtkFileChooserWidget *impl, LoadState new_load_state)
{
  if (impl->load_timeout_id != 0)
    {
      g_assert (impl->load_state == LOAD_PRELOAD);

      g_source_remove (impl->load_timeout_id);
      impl->load_timeout_id = 0;
    }
  else
    g_assert (impl->load_state == LOAD_EMPTY ||
              impl->load_state == LOAD_LOADING ||
              impl->load_state == LOAD_FINISHED);

  g_assert (new_load_state == LOAD_EMPTY ||
            new_load_state == LOAD_LOADING ||
            new_load_state == LOAD_FINISHED);
  impl->load_state = new_load_state;
}

/* Selects the first row in the file list */
static void
browse_files_select_first_row (GtkFileChooserWidget *impl)
{
  GtkTreePath *path;
  GtkTreeIter dummy_iter;
  GtkTreeModel *tree_model;

  tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view));

  if (!tree_model)
    return;

  path = gtk_tree_path_new_from_indices (0, -1);

  /* If the list is empty, do nothing. */
  if (gtk_tree_model_get_iter (tree_model, &dummy_iter, path))
    {
      /* Although the following call to gtk_tree_view_set_cursor() is intended to
       * only change the focus to the first row (not select it), GtkTreeView *will*
       * select the row anyway due to bug #492206.  So, we'll use a flag to
       * keep our own callbacks from changing the location_entry when the selection
       * is changed.  This entire function, browse_files_select_first_row(), may
       * go away when that bug is fixed in GtkTreeView.
       */
      impl->auto_selecting_first_row = TRUE;

      gtk_tree_view_set_cursor (GTK_TREE_VIEW (impl->browse_files_tree_view), path, NULL, FALSE);

      impl->auto_selecting_first_row = FALSE;
    }
  gtk_tree_path_free (path);
}

struct center_selected_row_closure {
  GtkFileChooserWidget *impl;
  gboolean already_centered;
};

/* Callback used from gtk_tree_selection_selected_foreach(); centers the
 * selected row in the tree view.
 */
static void
center_selected_row_foreach_cb (GtkTreeModel      *model,
                                GtkTreePath       *path,
                                GtkTreeIter       *iter,
                                gpointer           data)
{
  struct center_selected_row_closure *closure = data;

  if (closure->already_centered)
    return;

  gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (closure->impl->browse_files_tree_view), path, NULL, TRUE, 0.5, 0.0);
  closure->already_centered = TRUE;
}

/* Centers the selected row in the tree view */
static void
browse_files_center_selected_row (GtkFileChooserWidget *impl)
{
  struct center_selected_row_closure closure;
  GtkTreeSelection *selection;

  closure.impl = impl;
  closure.already_centered = FALSE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, center_selected_row_foreach_cb, &closure);
}

static gboolean
show_and_select_files (GtkFileChooserWidget *impl,
                       GSList               *files)
{
  GtkTreeSelection *selection;
  GtkFileSystemModel *fsmodel;
  gboolean enabled_hidden, removed_filters;
  gboolean selected_a_file;
  GSList *walk;

  g_assert (impl->load_state == LOAD_FINISHED);
  g_assert (impl->browse_files_model != NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  fsmodel = GTK_FILE_SYSTEM_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view)));

  g_assert (fsmodel == impl->browse_files_model);

  enabled_hidden = impl->show_hidden;
  removed_filters = (impl->current_filter == NULL);

  selected_a_file = FALSE;

  for (walk = files; walk; walk = walk->next)
    {
      GFile *file = walk->data;
      GtkTreeIter iter;

      /* Is it a hidden file? */

      if (!_gtk_file_system_model_get_iter_for_file (fsmodel, &iter, file))
        continue;

      if (!_gtk_file_system_model_iter_is_visible (fsmodel, &iter))
        {
          GFileInfo *info = _gtk_file_system_model_get_info (fsmodel, &iter);

          if (!enabled_hidden &&
              (g_file_info_get_is_hidden (info) ||
               g_file_info_get_is_backup (info)))
            {
              set_show_hidden (impl, TRUE);
              enabled_hidden = TRUE;
            }
        }

      /* Is it a filtered file? */

      if (!_gtk_file_system_model_get_iter_for_file (fsmodel, &iter, file))
        continue; /* re-get the iter as it may change when the model refilters */

      if (!_gtk_file_system_model_iter_is_visible (fsmodel, &iter))
        {
          /* Maybe we should have a way to ask the fsmodel if it had filtered a file */
          if (!removed_filters)
            {
              set_current_filter (impl, NULL);
              removed_filters = TRUE;
            }
        }

      /* Okay, can we select the file now? */
      if (!_gtk_file_system_model_get_iter_for_file (fsmodel, &iter, file))
        continue;

      if (_gtk_file_system_model_iter_is_visible (fsmodel, &iter))
        {
          GtkTreePath *path;

          gtk_tree_selection_select_iter (selection, &iter);

          path = gtk_tree_model_get_path (GTK_TREE_MODEL (fsmodel), &iter);
          gtk_tree_view_set_cursor (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                    path, NULL, FALSE);
          gtk_tree_path_free (path);

          selected_a_file = TRUE;
        }
    }

  browse_files_center_selected_row (impl);

  return selected_a_file;
}

/* Processes the pending operation when a folder is finished loading */
static void
pending_select_files_process (GtkFileChooserWidget *impl)
{
  g_assert (impl->load_state == LOAD_FINISHED);
  g_assert (impl->browse_files_model != NULL);

  if (impl->pending_select_files)
    {
      show_and_select_files (impl, impl->pending_select_files);
      pending_select_files_free (impl);
      browse_files_center_selected_row (impl);
    }
  else
    {
      /* We only select the first row if the chooser is actually mapped ---
       * selecting the first row is to help the user when he is interacting with
       * the chooser, but sometimes a chooser works not on behalf of the user,
       * but rather on behalf of something else like GtkFileChooserButton.  In
       * that case, the chooser's selection should be what the caller expects,
       * as the user can't see that something else got selected.  See bug #165264.
       */
      if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN &&
          gtk_widget_get_mapped (GTK_WIDGET (impl)))
        browse_files_select_first_row (impl);
    }

  g_assert (impl->pending_select_files == NULL);
}

static void
show_error_on_reading_current_folder (GtkFileChooserWidget *impl, GError *error)
{
  GFileInfo *info;
  char *msg;

  info = g_file_query_info (impl->current_folder,
                            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            NULL);
  if (info)
    {
      msg = g_strdup_printf (_("Could not read the contents of %s"), g_file_info_get_display_name (info));
      g_object_unref (info);
    }
  else
    msg = g_strdup (_("Could not read the contents of the folder"));

  error_message (impl, msg, error->message);
  g_free (msg);
}

/* Callback used when the file system model finishes loading */
static void
browse_files_model_finished_loading_cb (GtkFileSystemModel   *model,
                                        GError               *error,
                                        GtkFileChooserWidget *impl)
{
  if (error)
    {
      set_busy_cursor (impl, FALSE);
      show_error_on_reading_current_folder (impl, error);
    }

  if (impl->load_state == LOAD_PRELOAD)
    {
      load_remove_timer (impl, LOAD_FINISHED);
      load_set_model (impl);
    }
  else if (impl->load_state == LOAD_LOADING)
    {
      /* Nothing */
    }
  else
    {
      /* We can't g_assert_not_reached(), as something other than us may have
       *  initiated a folder reload.  See #165556.
       */
      return;
    }

  g_assert (impl->load_timeout_id == 0);

  impl->load_state = LOAD_FINISHED;

  pending_select_files_process (impl);
  set_busy_cursor (impl, FALSE);
}

/* Callback used when file system model adds or updates a file.
 * We detect here when a new renamed file appears and reveal it */
static void
browse_files_model_row_changed_cb (GtkTreeModel         *model,
                                   GtkTreePath          *path,
                                   GtkTreeIter          *iter,
                                   GtkFileChooserWidget *impl)
{
  GFile *file;
  GSList files;

  if (impl->renamed_file)
    {
      gtk_tree_model_get (model, iter, MODEL_COL_FILE, &file, -1);
      if (g_file_equal (impl->renamed_file, file))
        {
          g_clear_object (&impl->renamed_file);

          files.data = (gpointer) file;
          files.next = NULL;

          show_and_select_files (impl, &files);
        }

      g_object_unref (file);
    }
}

static void
stop_loading_and_clear_list_model (GtkFileChooserWidget *impl,
                                   gboolean              remove)
{
  load_remove_timer (impl, LOAD_EMPTY);

  g_set_object (&impl->browse_files_model, NULL);

  if (remove)
    gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view), NULL);
}

/* Replace 'target' with 'replacement' in the input string. */
static char *
string_replace (const char *input,
                const char *target,
                const char *replacement)
{
  char **pieces;
  char *output;

  pieces = g_strsplit (input, target, -1);
  output = g_strjoinv (replacement, pieces);
  g_strfreev (pieces);

  return output;
}

static void
replace_ratio (char **str)
{
  if (g_get_charset (NULL))
    {
      char *ret;
      ret = string_replace (*str, ":", "\xE2\x80\x8E∶");
      g_free (*str);
      *str = ret;
    }
}

static char *
my_g_format_date_for_display (GtkFileChooserWidget *impl,
                              glong                 secs)
{
  GDateTime *now, *time;
  GDateTime *now_date, *date;
  const char *format;
  char *date_str;
  int days_ago;

  time = g_date_time_new_from_unix_local (secs);
  date = g_date_time_new_local (g_date_time_get_year (time),
                                g_date_time_get_month (time),
                                g_date_time_get_day_of_month (time),
                                0, 0, 0);

  now = g_date_time_new_now_local ();
  now_date = g_date_time_new_local (g_date_time_get_year (now),
                                    g_date_time_get_month (now),
                                    g_date_time_get_day_of_month (now),
                                    0, 0, 0);
  days_ago = g_date_time_difference (now_date, date) / G_TIME_SPAN_DAY;

  if (days_ago < 1)
    {
      if (impl->show_time)
        format = "";
      else if (impl->clock_format == CLOCK_FORMAT_24)
        /* Translators: see g_date_time_format() for details on the format */
        format = _("%H:%M");
      else
        format = _("%l:%M %p");
    }
  else if (days_ago < 2)
    {
      format = _("Yesterday");
    }
  else if (days_ago < 7)
    {
      format = "%a"; /* Days from last week */
    }
  else if (g_date_time_get_year (now) == g_date_time_get_year (time))
    {
      format = _("%-e %b");
    }
  else
    {
      format = N_("%-e %b %Y");
    }

  date_str = g_date_time_format (time, format);
  replace_ratio (&date_str);

  g_date_time_unref (now);
  g_date_time_unref (now_date);
  g_date_time_unref (time);
  g_date_time_unref (date);

  return date_str;
}

static char *
my_g_format_time_for_display (GtkFileChooserWidget *impl,
                              glong                 secs)
{
  GDateTime *time;
  const char *format;
  char *date_str;

  time = g_date_time_new_from_unix_local (secs);

  if (impl->clock_format == CLOCK_FORMAT_24)
    format = _("%H:%M");
  else
    format = _("%l:%M %p");

  date_str = g_date_time_format (time, format);
  replace_ratio (&date_str);

  g_date_time_unref (time);

  return date_str;
}

static void
copy_attribute (GFileInfo   *to,
                GFileInfo   *from,
                const char *attribute)
{
  GFileAttributeType type;
  gpointer value;

  if (g_file_info_get_attribute_data (from, attribute, &type, &value, NULL))
    g_file_info_set_attribute (to, attribute, type, value);
}

static void
file_system_model_got_thumbnail (GObject      *object,
                                 GAsyncResult *res,
                                 gpointer      data)
{
  GtkFileSystemModel *model = data; /* might be unreffed if operation was cancelled */
  GFile *file = G_FILE (object);
  GFileInfo *queried, *info;
  GtkTreeIter iter;

  queried = g_file_query_info_finish (file, res, NULL);
  if (queried == NULL)
    return;

  /* now we know model is valid */

  /* file was deleted */
  if (!_gtk_file_system_model_get_iter_for_file (model, &iter, file))
    {
      g_object_unref (queried);
      return;
    }

  info = g_file_info_dup (_gtk_file_system_model_get_info (model, &iter));

  copy_attribute (info, queried, G_FILE_ATTRIBUTE_THUMBNAIL_PATH);
  copy_attribute (info, queried, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED);
  copy_attribute (info, queried, G_FILE_ATTRIBUTE_STANDARD_ICON);

  _gtk_file_system_model_update_file (model, file, info);

  g_object_unref (info);
  g_object_unref (queried);
}

/* Copied from src/nautilus_file.c:get_description() */
struct {
  const char *icon_name;
  const char *display_name;
} mime_type_map[] = {
  { "application-x-executable", N_("Program") },
  { "audio-x-generic", N_("Audio") },
  { "font-x-generic", N_("Font") },
  { "image-x-generic", N_("Image") },
  { "package-x-generic", N_("Archive") },
  { "text-html", N_("Markup") },
  { "text-x-generic", N_("Text") },
  { "text-x-generic-template", N_("Text") },
  { "text-x-script", N_("Program") },
  { "video-x-generic", N_("Video") },
  { "x-office-address-book", N_("Contacts") },
  { "x-office-calendar", N_("Calendar") },
  { "x-office-document", N_("Document") },
  { "x-office-presentation", N_("Presentation") },
  { "x-office-spreadsheet", N_("Spreadsheet") },
};

static char *
get_category_from_content_type (const char *content_type)
{
  char *icon_name;
  char *basic_type = NULL;

  icon_name = g_content_type_get_generic_icon_name (content_type);
  if (icon_name != NULL)
    {
      int i;

      for (i = 0; i < G_N_ELEMENTS (mime_type_map); i++)
        {
          if (strcmp (mime_type_map[i].icon_name, icon_name) == 0)
            {
              basic_type = g_strdup (_(mime_type_map[i].display_name));
              break;
            }
        }

      g_free (icon_name);
    }

  if (basic_type == NULL)
    {
      basic_type = g_content_type_get_description (content_type);
      if (basic_type == NULL)
        {
          basic_type = g_strdup (_("Unknown"));
        }
    }

  return basic_type;
}

static char *
get_type_information (GtkFileChooserWidget *impl,
                      GFileInfo            *info)
{
  const char *content_type;
  char *mime_type;
  char *description;

  content_type = g_file_info_get_content_type (info);
  if (!content_type)
    content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
  if (!content_type)
    goto end;

  switch (impl->type_format)
    {
    case TYPE_FORMAT_MIME:
      mime_type = g_content_type_get_mime_type (content_type);
      return mime_type ? mime_type : g_strdup (content_type);

    case TYPE_FORMAT_DESCRIPTION:
      description = g_content_type_get_description (content_type);
      return description ? description : g_strdup (content_type);

    case TYPE_FORMAT_CATEGORY:
      return get_category_from_content_type (content_type);

    default:
      g_assert_not_reached ();
    }

end:
  return g_strdup ("");
}

static gboolean
file_system_model_set (GtkFileSystemModel *model,
                       GFile              *file,
                       GFileInfo          *info,
                       int                 column,
                       GValue             *value,
                       gpointer            data)
{
  GtkFileChooserWidget *impl = data;

  switch (column)
    {
    case MODEL_COL_FILE:
      g_value_set_object (value, file);
      break;
    case MODEL_COL_NAME:
      if (info == NULL)
        g_value_set_string (value, DEFAULT_NEW_FOLDER_NAME);
      else
        g_value_set_string (value, g_file_info_get_display_name (info));
      break;
    case MODEL_COL_NAME_COLLATED:
      if (info == NULL)
        g_value_take_string (value, g_utf8_collate_key_for_filename (DEFAULT_NEW_FOLDER_NAME, -1));
      else
        g_value_take_string (value, g_utf8_collate_key_for_filename (g_file_info_get_display_name (info), -1));
      break;
    case MODEL_COL_IS_FOLDER:
      g_value_set_boolean (value, info == NULL || _gtk_file_info_consider_as_directory (info));
      break;
    case MODEL_COL_IS_SENSITIVE:
      if (info)
        {
          gboolean sensitive = TRUE;

          if (impl->action != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
            {
              sensitive = TRUE; /* for file modes... */
            }
          else if (!_gtk_file_info_consider_as_directory (info))
            {
              sensitive = FALSE; /* for folder modes, files are not sensitive... */
            }
          else
            {
              /* ... and for folder modes, folders are sensitive only if the filter says so */
              GtkTreeIter iter;
              if (!_gtk_file_system_model_get_iter_for_file (model, &iter, file))
                g_assert_not_reached ();
              sensitive = !_gtk_file_system_model_iter_is_filtered_out (model, &iter);
            }

          g_value_set_boolean (value, sensitive);
        }
      else
        g_value_set_boolean (value, TRUE);
      break;
    case MODEL_COL_ICON:
      if (info)
        {
          if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_ICON))
            {
              int scale;
              GtkIconTheme *icon_theme;

              scale = gtk_widget_get_scale_factor (GTK_WIDGET (impl));
              icon_theme = gtk_icon_theme_get_for_display (gtk_widget_get_display (GTK_WIDGET (impl)));

              g_value_take_object (value, _gtk_file_info_get_icon (info, ICON_SIZE, scale, icon_theme));
            }
          else
            {
              GtkTreeModel *tree_model;
              GtkTreePath *start, *end;
              GtkTreeIter iter;
              gboolean visible;

              if (impl->browse_files_tree_view == NULL ||
                  g_file_info_has_attribute (info, "filechooser::queried"))
                return FALSE;

              tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view));
              if (tree_model != GTK_TREE_MODEL (model))
                return FALSE;

              if (!_gtk_file_system_model_get_iter_for_file (model, &iter, file))
                g_assert_not_reached ();

              if (gtk_tree_view_get_visible_range (GTK_TREE_VIEW (impl->browse_files_tree_view), &start, &end))
                {
                  GtkTreePath *path;

                  gtk_tree_path_prev (start);
                  gtk_tree_path_next (end);
                  path = gtk_tree_model_get_path (tree_model, &iter);
                  visible = gtk_tree_path_compare (start, path) != 1 &&
                            gtk_tree_path_compare (path, end) != 1;
                  gtk_tree_path_free (path);
                  gtk_tree_path_free (start);
                  gtk_tree_path_free (end);
                }
              else
                visible = TRUE;
              if (visible)
                {
                  g_file_info_set_attribute_boolean (info, "filechooser::queried", TRUE);
                  g_file_query_info_async (file,
                                           G_FILE_ATTRIBUTE_THUMBNAIL_PATH ","
                                           G_FILE_ATTRIBUTE_THUMBNAILING_FAILED ","
                                           G_FILE_ATTRIBUTE_STANDARD_ICON,
                                           G_FILE_QUERY_INFO_NONE,
                                           G_PRIORITY_DEFAULT,
                                           _gtk_file_system_model_get_cancellable (model),
                                           file_system_model_got_thumbnail,
                                           model);
                }
              return FALSE;
            }
        }
      else
        g_value_set_boxed (value, NULL);
      break;
    case MODEL_COL_SIZE:
      g_value_set_int64 (value, info ? g_file_info_get_size (info) : 0);
      break;
    case MODEL_COL_SIZE_TEXT:
      if (info == NULL || _gtk_file_info_consider_as_directory (info))
        g_value_set_string (value, NULL);
      else
        g_value_take_string (value, g_format_size (g_file_info_get_size (info)));
      break;
    case MODEL_COL_TYPE:
      if (info == NULL || _gtk_file_info_consider_as_directory (info))
        g_value_set_string (value, NULL);
      else
        g_value_take_string (value, get_type_information (impl, info));
      break;
    case MODEL_COL_TIME:
    case MODEL_COL_DATE_TEXT:
    case MODEL_COL_TIME_TEXT:
      {
        glong time;
        if (info == NULL)
          break;
        if (impl->operation_mode == OPERATION_MODE_RECENT)
          time = (glong) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_ACCESS);
        else
          time = (glong) g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        if (column == MODEL_COL_TIME)
          g_value_set_long (value, time);
        else if (time == 0)
          g_value_set_static_string (value, _("Unknown"));
        else if (column == MODEL_COL_DATE_TEXT)
          g_value_take_string (value, my_g_format_date_for_display (impl, time));
        else
          g_value_take_string (value, my_g_format_time_for_display (impl, time));
        break;
      }
    case MODEL_COL_ELLIPSIZE:
      g_value_set_enum (value, info ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
      break;
    case MODEL_COL_LOCATION_TEXT:
      {
        GFile *home_location;
        GFile *dir_location;
        char *location;

        home_location = g_file_new_for_path (g_get_home_dir ());
        if (file)
          dir_location = g_file_get_parent (file);
        else
          dir_location = NULL;

        if (dir_location && file_is_recent_uri (dir_location))
          {
            const char *target_uri;
            GFile *target;

            target_uri = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI);
            target = g_file_new_for_uri (target_uri);
            g_object_unref (dir_location);
            dir_location = g_file_get_parent (target);
            g_object_unref (target);
          }

        if (!dir_location)
          location = g_strdup ("/");
        else if (impl->current_folder && g_file_equal (impl->current_folder, dir_location))
          location = g_strdup ("");
        else if (g_file_equal (home_location, dir_location))
          location = g_strdup (_("Home"));
        else if (g_file_has_prefix (dir_location, home_location))
          {
            char *relative_path;

            relative_path = g_file_get_relative_path (home_location, dir_location);
            location = g_filename_display_name (relative_path);

            g_free (relative_path);
          }
        else
          location = g_file_get_path (dir_location);

        g_value_take_string (value, location);

        if (dir_location)
          g_object_unref (dir_location);
        g_object_unref (home_location);
      }
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  return TRUE;
}

/* Gets rid of the old list model and creates a new one for the current folder */
static gboolean
set_list_model (GtkFileChooserWidget  *impl,
                GError               **error)
{
  g_assert (impl->current_folder != NULL);

  if (impl->browse_files_model &&
      _gtk_file_system_model_get_directory (impl->browse_files_model) == impl->current_folder)
    return TRUE;

  stop_loading_and_clear_list_model (impl, TRUE);

  set_busy_cursor (impl, TRUE);

  impl->browse_files_model =
    _gtk_file_system_model_new_for_directory (impl->current_folder,
                                              MODEL_ATTRIBUTES,
                                              file_system_model_set,
                                              impl,
                                              MODEL_COLUMN_TYPES);

  _gtk_file_system_model_set_show_hidden (impl->browse_files_model, impl->show_hidden);

  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (impl->browse_files_model), MODEL_COL_NAME, name_sort_func, impl, NULL);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (impl->browse_files_model), MODEL_COL_SIZE, size_sort_func, impl, NULL);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (impl->browse_files_model), MODEL_COL_TYPE, type_sort_func, impl, NULL);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (impl->browse_files_model), MODEL_COL_TIME, time_sort_func, impl, NULL);
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (impl->browse_files_model), NULL, NULL, NULL);
  set_sort_column (impl);
  impl->list_sort_ascending = TRUE;
  g_signal_connect (impl->browse_files_model, "sort-column-changed",
                    G_CALLBACK (list_sort_column_changed_cb), impl);

  load_setup_timer (impl); /* This changes the state to LOAD_PRELOAD */

  g_signal_connect (impl->browse_files_model, "finished-loading",
                    G_CALLBACK (browse_files_model_finished_loading_cb), impl);

  g_signal_connect (impl->browse_files_model, "row-changed",
                    G_CALLBACK (browse_files_model_row_changed_cb), impl);

  _gtk_file_system_model_set_filter (impl->browse_files_model, impl->current_filter);

  return TRUE;
}

struct update_chooser_entry_selected_foreach_closure {
  int num_selected;
  GtkTreeIter first_selected_iter;
};

static int
compare_utf8_filenames (const char *a,
                        const char *b)
{
  char *a_folded, *b_folded;
  int retval;

  a_folded = g_utf8_strdown (a, -1);
  b_folded = g_utf8_strdown (b, -1);

  retval = strcmp (a_folded, b_folded);

  g_free (a_folded);
  g_free (b_folded);

  return retval;
}

static void
update_chooser_entry_selected_foreach (GtkTreeModel *model,
                                       GtkTreePath  *path,
                                       GtkTreeIter  *iter,
                                       gpointer      data)
{
  struct update_chooser_entry_selected_foreach_closure *closure;

  closure = data;
  closure->num_selected++;

  if (closure->num_selected == 1)
    closure->first_selected_iter = *iter;
}

static void
update_chooser_entry (GtkFileChooserWidget *impl)
{
  GtkTreeSelection *selection;
  struct update_chooser_entry_selected_foreach_closure closure;

  /* no need to update the file chooser's entry if there's no entry */
  if (impl->operation_mode == OPERATION_MODE_SEARCH ||
      !impl->location_entry)
    return;

  if (!(impl->action == GTK_FILE_CHOOSER_ACTION_SAVE
        || ((impl->action == GTK_FILE_CHOOSER_ACTION_OPEN
             || impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
            && impl->location_mode == LOCATION_MODE_FILENAME_ENTRY)))
    return;

  g_assert (impl->location_entry != NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  closure.num_selected = 0;
  gtk_tree_selection_selected_foreach (selection, update_chooser_entry_selected_foreach, &closure);

  if (closure.num_selected == 0)
    {
      if (impl->operation_mode == OPERATION_MODE_RECENT)
        _gtk_file_chooser_entry_set_base_folder (GTK_FILE_CHOOSER_ENTRY (impl->location_entry), NULL);
      else
        goto maybe_clear_entry;
    }
  else if (closure.num_selected == 1)
    {
      if (impl->operation_mode == OPERATION_MODE_BROWSE)
        {
          GFileInfo *info;
          gboolean change_entry;

          info = _gtk_file_system_model_get_info (impl->browse_files_model, &closure.first_selected_iter);

          /* If the cursor moved to the row of the newly created folder,
           * retrieving info will return NULL.
           */
          if (!info)
            return;

          g_free (impl->browse_files_last_selected_name);
          impl->browse_files_last_selected_name =
            g_strdup (g_file_info_get_display_name (info));

          if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
              impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
            {
              /* Don't change the name when clicking on a folder... */
              change_entry = !_gtk_file_info_consider_as_directory (info);
            }
          else
            change_entry = TRUE; /* ... unless we are in SELECT_FOLDER mode */

          if (change_entry && !impl->auto_selecting_first_row)
            {
              GtkEntryCompletion *completion = gtk_entry_get_completion (GTK_ENTRY (impl->location_entry));

              if (completion)
                gtk_entry_completion_set_popup_completion (completion, FALSE);
              g_signal_handlers_block_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
              gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), impl->browse_files_last_selected_name);
              g_signal_handlers_unblock_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
              if (completion)
                gtk_entry_completion_set_popup_completion (completion, TRUE);

              if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
                _gtk_file_chooser_entry_select_filename (GTK_FILE_CHOOSER_ENTRY (impl->location_entry));
            }

          return;
        }
      else if (impl->operation_mode == OPERATION_MODE_RECENT
               && impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
          GFile *folder;

          /* Set the base folder on the name entry, so it will do completion relative to the correct recent-folder */

          gtk_tree_model_get (GTK_TREE_MODEL (impl->recent_model), &closure.first_selected_iter,
                              MODEL_COL_FILE, &folder,
                              -1);
          _gtk_file_chooser_entry_set_base_folder (GTK_FILE_CHOOSER_ENTRY (impl->location_entry), folder);
          g_object_unref (folder);
          return;
        }
    }
  else
    {
      g_assert (impl->action != GTK_FILE_CHOOSER_ACTION_SAVE);

      /* Multiple selection, so just clear the entry. */
      g_free (impl->browse_files_last_selected_name);
      impl->browse_files_last_selected_name = NULL;

      g_signal_handlers_block_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
      gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), "");
      g_signal_handlers_unblock_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
      return;
    }

 maybe_clear_entry:

  if ((impl->action == GTK_FILE_CHOOSER_ACTION_OPEN || impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
      && impl->browse_files_last_selected_name)
    {
      const char *entry_text;
      int len;
      gboolean clear_entry;

      entry_text = gtk_editable_get_text (GTK_EDITABLE (impl->location_entry));
      len = strlen (entry_text);
      if (len != 0)
        {
          /* The file chooser entry may have appended a "/" to its text.
           * So take it out, and compare the result to the old selection.
           */
          if (entry_text[len - 1] == G_DIR_SEPARATOR)
            {
              char *tmp;

              tmp = g_strndup (entry_text, len - 1);
              clear_entry = (compare_utf8_filenames (impl->browse_files_last_selected_name, tmp) == 0);
              g_free (tmp);
            }
          else
            clear_entry = (compare_utf8_filenames (impl->browse_files_last_selected_name, entry_text) == 0);
        }
      else
        clear_entry = FALSE;

      if (clear_entry)
        {
          g_signal_handlers_block_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
          gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), "");
          g_signal_handlers_unblock_by_func (impl->location_entry, G_CALLBACK (location_entry_changed_cb), impl);
        }
    }
}

static gboolean
gtk_file_chooser_widget_set_current_folder (GtkFileChooser  *chooser,
                                             GFile          *file,
                                             GError        **error)
{
  return gtk_file_chooser_widget_update_current_folder (chooser, file, FALSE, FALSE, error);
}


struct UpdateCurrentFolderData
{
  GtkFileChooserWidget *impl;
  GFile *file;
  gboolean keep_trail;
  gboolean clear_entry;
  GFile *original_file;
  GError *original_error;
};

static void
update_current_folder_mount_enclosing_volume_cb (GObject      *source,
                                                 GAsyncResult *result,
                                                 gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct UpdateCurrentFolderData *data = user_data;
  GtkFileChooserWidget *impl = data->impl;
  GError *error = NULL;

  g_clear_object (&impl->update_current_folder_cancellable);
  set_busy_cursor (impl, FALSE);

  g_file_mount_enclosing_volume_finish (file, result, &error);
  if (error)
    {
      error_changing_folder_dialog (data->impl, data->file, g_error_copy (error));
      impl->reload_state = RELOAD_EMPTY;
      goto out;
    }

  change_folder_and_display_error (impl, data->file, data->clear_entry);

out:
  g_object_unref (data->impl);
  g_object_unref (data->file);
  g_free (data);

  g_clear_error (&error);
}

static void
update_current_folder_get_info_cb (GObject      *source,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct UpdateCurrentFolderData *data = user_data;
  GFileInfo *info;
  GError *error = NULL;
  GtkFileChooserWidget *impl = data->impl;

  g_clear_object (&impl->update_current_folder_cancellable);
  impl->reload_state = RELOAD_EMPTY;

  set_busy_cursor (impl, FALSE);

  info = g_file_query_info_finish (file, result, &error);
  if (error)
    {
      GFile *parent_file;

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED))
        {
          GMountOperation *mount_operation;
          GtkWidget *toplevel;

          g_clear_error (&error);
          toplevel = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (impl)));

          mount_operation = gtk_mount_operation_new (GTK_WINDOW (toplevel));

          set_busy_cursor (impl, TRUE);

          impl->update_current_folder_cancellable = g_cancellable_new ();
          g_file_mount_enclosing_volume (data->file,
                                         G_MOUNT_MOUNT_NONE,
                                         mount_operation,
                                         impl->update_current_folder_cancellable,
                                         update_current_folder_mount_enclosing_volume_cb,
                                         data);

          return;
        }

      if (!data->original_file)
        {
          data->original_file = g_object_ref (data->file);
          data->original_error = g_error_copy (error);
        }

      parent_file = g_file_get_parent (data->file);

      /* get parent path and try to change the folder to that */
      if (parent_file)
        {
          g_object_unref (data->file);
          data->file = parent_file;

          g_clear_error (&error);

          /* restart the update current folder operation */
          impl->reload_state = RELOAD_HAS_FOLDER;

          impl->update_current_folder_cancellable = g_cancellable_new ();
          g_file_query_info_async (data->file,
                                   "standard::type",
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT,
                                   impl->update_current_folder_cancellable,
                                   update_current_folder_get_info_cb,
                                   data);

          set_busy_cursor (impl, TRUE);

          return;
        }
      else
        {
          /* Error and bail out, ignoring "not found" errors since they're useless:
           * they only happen when a program defaults to a folder that has been (re)moved.
           */
          if (!g_error_matches (data->original_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            error_changing_folder_dialog (impl, data->original_file, data->original_error);
          else
            g_error_free (data->original_error);

          g_clear_error (&error);
          g_object_unref (data->original_file);

          goto out;
        }
    }

  if (data->original_file)
    {
      /* Error and bail out, ignoring "not found" errors since they're useless:
       * they only happen when a program defaults to a folder that has been (re)moved.
       */
      if (!g_error_matches (data->original_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        error_changing_folder_dialog (impl, data->original_file, data->original_error);
      else
        g_error_free (data->original_error);

      g_object_unref (data->original_file);
    }

  if (! _gtk_file_info_consider_as_directory (info))
    goto out;

  _gtk_path_bar_set_file (GTK_PATH_BAR (impl->browse_path_bar), data->file, data->keep_trail);

  if (impl->current_folder != data->file)
    {
      if (impl->current_folder)
        g_object_unref (impl->current_folder);

      impl->current_folder = g_object_ref (data->file);
    }

  impl->reload_state = RELOAD_HAS_FOLDER;

  /* Set the folder on the save entry */

  if (impl->location_entry)
    {
      _gtk_file_chooser_entry_set_base_folder (GTK_FILE_CHOOSER_ENTRY (impl->location_entry),
                                               impl->current_folder);

      if (data->clear_entry)
        gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), "");
    }

  /* Create a new list model.  This is slightly evil; we store the result value
   * but perform more actions rather than returning immediately even if it
   * generates an error.
   */
  set_list_model (impl, NULL);

  /* Refresh controls */

  gtk_places_sidebar_set_location (GTK_PLACES_SIDEBAR (impl->places_sidebar), impl->current_folder);

  g_object_notify (G_OBJECT (impl), "subtitle");

  update_default (impl);

out:
  g_object_unref (data->impl);
  g_object_unref (data->file);
  g_free (data);

  g_clear_object (&info);
}

static gboolean
gtk_file_chooser_widget_update_current_folder (GtkFileChooser  *chooser,
                                               GFile          *file,
                                               gboolean        keep_trail,
                                               gboolean        clear_entry,
                                               GError        **error)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  struct UpdateCurrentFolderData *data;

  g_object_ref (file);

  operation_mode_set (impl, OPERATION_MODE_BROWSE);

  if (impl->update_current_folder_cancellable)
    g_cancellable_cancel (impl->update_current_folder_cancellable);
  g_clear_object (&impl->update_current_folder_cancellable);

  /* Test validity of path here.  */
  data = g_new0 (struct UpdateCurrentFolderData, 1);
  data->impl = g_object_ref (impl);
  data->file = g_object_ref (file);
  data->keep_trail = keep_trail;
  data->clear_entry = clear_entry;

  impl->reload_state = RELOAD_HAS_FOLDER;

  impl->update_current_folder_cancellable = g_cancellable_new ();
  g_file_query_info_async (file,
                           "standard::type",
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           impl->update_current_folder_cancellable,
                           update_current_folder_get_info_cb,
                           data);

  set_busy_cursor (impl, TRUE);
  g_object_unref (file);

  return TRUE;
}

static GFile *
gtk_file_chooser_widget_get_current_folder (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  if (impl->operation_mode == OPERATION_MODE_RECENT)
    return NULL;

  if (impl->current_folder)
    return g_object_ref (impl->current_folder);

  return NULL;
}

static void
gtk_file_chooser_widget_set_current_name (GtkFileChooser *chooser,
                                          const char     *name)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkEntryCompletion *completion;

  g_return_if_fail (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE);

  pending_select_files_free (impl);

  completion = gtk_entry_get_completion (GTK_ENTRY (impl->location_entry));
  gtk_entry_completion_set_popup_completion (completion, FALSE);

  gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), name);

  gtk_entry_completion_set_popup_completion (completion, TRUE);
}

static char *
gtk_file_chooser_widget_get_current_name (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  g_return_val_if_fail (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE, NULL);

  return g_strdup (gtk_editable_get_text (GTK_EDITABLE (impl->location_entry)));
}

static gboolean
gtk_file_chooser_widget_select_file (GtkFileChooser  *chooser,
                                     GFile           *file,
                                     GError         **error)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GFile *parent_file;
  gboolean same_path;
  GtkFileSystemModel *fsmodel;

  parent_file = g_file_get_parent (file);

  if (!parent_file)
    return gtk_file_chooser_set_current_folder (chooser, file, error);

  fsmodel = GTK_FILE_SYSTEM_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view)));

  if (impl->operation_mode == OPERATION_MODE_SEARCH ||
      impl->operation_mode == OPERATION_MODE_RECENT ||
      impl->load_state == LOAD_EMPTY ||
      impl->browse_files_model != fsmodel)
    {
      same_path = FALSE;
    }
  else
    {
      g_assert (impl->current_folder != NULL);

      same_path = g_file_equal (parent_file, impl->current_folder);
    }

  if (same_path && impl->load_state == LOAD_FINISHED)
    {
      gboolean result;
      GSList files;

      files.data = (gpointer) file;
      files.next = NULL;

      /* Prevent the file chooser from loading a different folder when it is mapped */
      impl->reload_state = RELOAD_HAS_FOLDER;

      result = show_and_select_files (impl, &files);
      g_object_unref (parent_file);
      return result;
    }

  pending_select_files_add (impl, file);

  if (!same_path)
    {
      gboolean result;

      result = gtk_file_chooser_set_current_folder (chooser, parent_file, error);
      g_object_unref (parent_file);
      return result;
    }

  g_object_unref (parent_file);
  return TRUE;
}

static void
gtk_file_chooser_widget_unselect_file (GtkFileChooser *chooser,
                                       GFile          *file)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkTreeView *tree_view;
  GtkTreeModel *model;
  GtkTreeIter iter;

  tree_view = GTK_TREE_VIEW (impl->browse_files_tree_view);
  model = gtk_tree_view_get_model (tree_view);
  if (!model)
    return;

  if (!_gtk_file_system_model_get_iter_for_file (GTK_FILE_SYSTEM_MODEL (model), &iter, file))
    return;

  gtk_tree_selection_unselect_iter (gtk_tree_view_get_selection (tree_view), &iter);
}

static gboolean
maybe_select (GtkTreeModel *model,
              GtkTreePath  *path,
              GtkTreeIter  *iter,
              gpointer      data)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (data);
  GtkTreeSelection *selection;
  gboolean is_sensitive;
  gboolean is_folder;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));

  gtk_tree_model_get (model, iter,
                      MODEL_COL_IS_FOLDER, &is_folder,
                      MODEL_COL_IS_SENSITIVE, &is_sensitive,
                      -1);

  if (is_sensitive &&
      ((is_folder && impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) ||
       (!is_folder && impl->action == GTK_FILE_CHOOSER_ACTION_OPEN)))
    gtk_tree_selection_select_iter (selection, iter);
  else
    gtk_tree_selection_unselect_iter (selection, iter);

  return FALSE;
}

static void
gtk_file_chooser_widget_select_all (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  if (impl->operation_mode == OPERATION_MODE_SEARCH ||
      impl->operation_mode == OPERATION_MODE_RECENT)
    {
      GtkTreeSelection *selection;

      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
      gtk_tree_selection_select_all (selection);
      return;
    }

  if (impl->select_multiple)
    gtk_tree_model_foreach (GTK_TREE_MODEL (impl->browse_files_model),
                            maybe_select, impl);
}

static void
gtk_file_chooser_widget_unselect_all (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));

  gtk_tree_selection_unselect_all (selection);
  pending_select_files_free (impl);
}

/* Checks whether the filename entry for Save modes contains a well-formed filename.
 *
 * is_well_formed_ret - whether what the user typed passes gkt_file_system_make_path()
 *
 * is_empty_ret - whether the file entry is totally empty
 *
 * is_file_part_empty_ret - whether the file part is empty (will be if user types
 *                          "foobar/", and the path will be “$cwd/foobar”)
 */
static void
check_save_entry (GtkFileChooserWidget  *impl,
                  GFile                **file_ret,
                  gboolean              *is_well_formed_ret,
                  gboolean              *is_empty_ret,
                  gboolean              *is_file_part_empty_ret,
                  gboolean              *is_folder)
{
  GtkFileChooserEntry *chooser_entry;
  GFile *current_folder;
  const char *file_part;
  char *file_part_stripped;
  GFile *file;
  GError *error;

  g_assert (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE ||
            ((impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
              impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) &&
             impl->location_mode == LOCATION_MODE_FILENAME_ENTRY));

  chooser_entry = GTK_FILE_CHOOSER_ENTRY (impl->location_entry);

  if (strlen (gtk_editable_get_text (GTK_EDITABLE (chooser_entry))) == 0)
    {
      *file_ret = NULL;
      *is_well_formed_ret = TRUE;
      *is_empty_ret = TRUE;
      *is_file_part_empty_ret = TRUE;
      *is_folder = FALSE;

      return;
    }

  *is_empty_ret = FALSE;

  current_folder = _gtk_file_chooser_entry_get_current_folder (chooser_entry);
  if (!current_folder)
    {
      *file_ret = NULL;
      *is_well_formed_ret = FALSE;
      *is_file_part_empty_ret = FALSE;
      *is_folder = FALSE;

      return;
    }

  file_part = _gtk_file_chooser_entry_get_file_part (chooser_entry);

  /* Copy and strip leading and trailing whitespace */
  file_part_stripped = g_strstrip (g_strdup (file_part));

  if (!file_part_stripped || file_part_stripped[0] == '\0')
    {
      *file_ret = current_folder;
      *is_well_formed_ret = TRUE;
      *is_file_part_empty_ret = TRUE;
      *is_folder = TRUE;

      g_free (file_part_stripped);
      return;
    }

  *is_file_part_empty_ret = FALSE;

  error = NULL;
  file = g_file_get_child_for_display_name (current_folder, file_part_stripped, &error);
  g_object_unref (current_folder);
  g_free (file_part_stripped);

  if (!file)
    {
      error_building_filename_dialog (impl, error);
      *file_ret = NULL;
      *is_well_formed_ret = FALSE;
      *is_folder = FALSE;

      return;
    }

  *file_ret = file;
  *is_well_formed_ret = TRUE;
  *is_folder = _gtk_file_chooser_entry_get_is_folder (chooser_entry, file);
}

struct get_files_closure {
  GtkFileChooserWidget *impl;
  GListStore *result;
  GFile *file_from_entry;
};

static void
get_files_foreach (GtkTreeModel *model,
                   GtkTreePath  *path,
                   GtkTreeIter  *iter,
                   gpointer      data)
{
  GtkFileSystemModel *fs_model = GTK_FILE_SYSTEM_MODEL (model);
  struct get_files_closure *info = data;
  GFile *file;

  file = _gtk_file_system_model_get_file (fs_model, iter);

  if (!info->file_from_entry || !g_file_equal (info->file_from_entry, file))
    g_list_store_append (info->result, file);
}

static GListModel *
get_selected_files_as_model (GtkFileChooserWidget *impl)
{
  GListStore *store;
  GSList *files, *l;

  store = g_list_store_new (G_TYPE_FILE);
  files = get_selected_files (impl);
  for (l = files; l; l = l->next)
    g_list_store_append (store, l->data);
  g_slist_free_full (files, g_object_unref);

  return G_LIST_MODEL (store);
}

static GListModel *
gtk_file_chooser_widget_get_files (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  struct get_files_closure info;
  GtkWindow *toplevel;
  GtkWidget *current_focus;
  gboolean file_list_seen;

  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    return get_selected_files_as_model (impl);

  info.impl = impl;
  info.result = g_list_store_new (G_TYPE_FILE);
  info.file_from_entry = NULL;

  if (impl->operation_mode == OPERATION_MODE_RECENT)
    {
      if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
          file_list_seen = TRUE;
          goto file_entry;
        }
      else
        {
          g_object_unref (info.result);
          return get_selected_files_as_model (impl);
        }
    }

  toplevel = get_toplevel (GTK_WIDGET (impl));
  if (toplevel)
    current_focus = gtk_root_get_focus (GTK_ROOT (toplevel));
  else
    current_focus = NULL;

  file_list_seen = FALSE;
  if (current_focus == impl->browse_files_tree_view)
    {
      GtkTreeSelection *selection;

    file_list:

      file_list_seen = TRUE;
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
      gtk_tree_selection_selected_foreach (selection, get_files_foreach, &info);

      /* If there is no selection in the file list, we probably have this situation:
       *
       * 1. The user typed a filename in the SAVE filename entry ("foo.txt").
       * 2. He then double-clicked on a folder ("bar") in the file list
       *
       * So we want the selection to be "bar/foo.txt".  Jump to the case for the
       * filename entry to see if that is the case.
       */
      if (g_list_model_get_n_items (G_LIST_MODEL (info.result)) == 0 && impl->location_entry)
        goto file_entry;
    }
  else if (impl->location_entry &&
           current_focus &&
           (current_focus == impl->location_entry ||
            gtk_widget_is_ancestor (current_focus, impl->location_entry)))
    {
      gboolean is_well_formed, is_empty, is_file_part_empty, is_folder;

    file_entry:

      check_save_entry (impl, &info.file_from_entry, &is_well_formed, &is_empty, &is_file_part_empty, &is_folder);

      if (is_empty)
        goto out;

      if (!is_well_formed)
        goto empty;

      if (info.file_from_entry)
        {
          g_list_store_append (info.result, info.file_from_entry);
          g_object_unref (info.file_from_entry);
        }
      else if (!file_list_seen)
        goto file_list;
      else
        goto empty;
    }
  else if (impl->toplevel_last_focus_widget == impl->browse_files_tree_view)
    goto file_list;
  else if (impl->location_entry && impl->toplevel_last_focus_widget == impl->location_entry)
    goto file_entry;
  else
    {
      /* The focus is on a dialog's action area button or something else */
      if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        goto file_entry;
      else
        goto file_list;
    }

 out:

  /* If there's no folder selected, and we're in SELECT_FOLDER mode,
   * then we fall back to the current directory
   */
  if (impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER &&
      g_list_model_get_n_items (G_LIST_MODEL (info.result)) == 0)
    {
      GFile *current_folder;

      current_folder = gtk_file_chooser_get_current_folder (chooser);

      if (current_folder)
        g_list_store_append (info.result, current_folder);
    }

  return G_LIST_MODEL (info.result);

empty:

  g_list_store_remove_all (info.result);
  return G_LIST_MODEL (info.result);
}

/* Shows or hides the filter widgets */
static void
show_filters (GtkFileChooserWidget *impl,
              gboolean              show)
{
  gtk_widget_set_visible (impl->filter_combo_hbox, show);
  update_extra_and_filters (impl);
}

static void
gtk_file_chooser_widget_add_filter (GtkFileChooser *chooser,
                                    GtkFileFilter  *filter)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  if (g_list_store_find (impl->filters, filter, NULL))
    {
      g_warning ("gtk_file_chooser_add_filter() called on filter already in list");
      return;
    }

  g_object_ref_sink (filter);

  g_list_store_append (impl->filters, filter);
  g_object_unref (filter);

  if (!impl->current_filter)
    set_current_filter (impl, filter);

  show_filters (impl, TRUE);

  g_object_notify (G_OBJECT (chooser), "filters");
}

static void
gtk_file_chooser_widget_remove_filter (GtkFileChooser *chooser,
                                       GtkFileFilter  *filter)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  guint filter_index;

  if (!g_list_store_find (impl->filters, filter, &filter_index))
    {
      g_warning ("gtk_file_chooser_remove_filter() called on filter not in list");
      return;
    }

  g_list_store_remove (impl->filters, filter_index);

  if (filter == impl->current_filter)
    {
      if (impl->filters)
        {
          GtkFileFilter *f = g_list_model_get_item (G_LIST_MODEL (impl->filters), 0);
          set_current_filter (impl, f);
          g_object_unref (f);
        }
      else
        set_current_filter (impl, NULL);
    }

  g_object_unref (filter);

  if (!impl->filters)
    show_filters (impl, FALSE);

  g_object_notify (G_OBJECT (chooser), "filters");
}

static GListModel *
gtk_file_chooser_widget_get_filters (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  return G_LIST_MODEL (g_object_ref (impl->filters));
}

static gboolean
gtk_file_chooser_widget_add_shortcut_folder (GtkFileChooser  *chooser,
                                             GFile           *file,
                                             GError         **error)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  gtk_places_sidebar_add_shortcut (GTK_PLACES_SIDEBAR (impl->places_sidebar), file);

  g_object_notify (G_OBJECT (chooser), "shortcut-folders");

  return TRUE;
}

static gboolean
gtk_file_chooser_widget_remove_shortcut_folder (GtkFileChooser  *chooser,
                                                GFile           *file,
                                                GError         **error)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  gtk_places_sidebar_remove_shortcut (GTK_PLACES_SIDEBAR (impl->places_sidebar), file);

  g_object_notify (G_OBJECT (chooser), "shortcut-folders");

  return TRUE;
}

static GListModel *
gtk_file_chooser_widget_get_shortcut_folders (GtkFileChooser *chooser)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);

  return gtk_places_sidebar_get_shortcuts (GTK_PLACES_SIDEBAR (impl->places_sidebar));
}

struct switch_folder_closure {
  GtkFileChooserWidget *impl;
  GFile *file;
  int num_selected;
};

/* Used from gtk_tree_selection_selected_foreach() in switch_to_selected_folder() */
static void
switch_folder_foreach_cb (GtkTreeModel *model,
                          GtkTreePath  *path,
                          GtkTreeIter  *iter,
                          gpointer      data)
{
  struct switch_folder_closure *closure;

  closure = data;

  closure->file = _gtk_file_system_model_get_file (GTK_FILE_SYSTEM_MODEL (model), iter);
  closure->num_selected++;
}

/* Changes to the selected folder in the list view */
static void
switch_to_selected_folder (GtkFileChooserWidget *impl)
{
  GtkTreeSelection *selection;
  struct switch_folder_closure closure;

  /* We do this with foreach() rather than get_selected() as we may be in
   * multiple selection mode
   */

  closure.impl = impl;
  closure.file = NULL;
  closure.num_selected = 0;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, switch_folder_foreach_cb, &closure);

  g_assert (closure.file && closure.num_selected == 1);

  change_folder_and_display_error (impl, closure.file, FALSE);
}

/* Gets the GFileInfo for the selected row in the file list; assumes single
 * selection mode.
 */
static GFileInfo *
get_selected_file_info_from_file_list (GtkFileChooserWidget *impl,
                                       gboolean              *had_selection)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GFileInfo *info;
  GtkTreeModel *model;

  g_assert (!impl->select_multiple);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      *had_selection = FALSE;
      return NULL;
    }

  *had_selection = TRUE;

  info = _gtk_file_system_model_get_info (GTK_FILE_SYSTEM_MODEL (model), &iter);
  return info;
}

/* Gets the display name of the selected file in the file list; assumes single
 * selection mode and that something is selected.
 */
static const char *
get_display_name_from_file_list (GtkFileChooserWidget *impl)
{
  GFileInfo *info;
  gboolean had_selection;

  info = get_selected_file_info_from_file_list (impl, &had_selection);
  g_assert (had_selection);
  g_assert (info != NULL);

  return g_file_info_get_display_name (info);
}

static void
add_custom_button_to_dialog (GtkDialog   *dialog,
                             const char *mnemonic_label,
                             int          response_id)
{
  GtkWidget *button;

  button = gtk_button_new_with_mnemonic (mnemonic_label);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, response_id);
}

/* Every time we request a response explicitly, we need to save the selection to
 * the recently-used list, as requesting a response means, “the dialog is confirmed”.
 */
static void
request_response_and_add_to_recent_list (GtkFileChooserWidget *impl)
{
  gtk_widget_activate_action (GTK_WIDGET (impl), "response.activate", NULL);
  add_selection_to_recent_list (impl);
}

static void
on_confirm_overwrite_response (GtkWidget *dialog,
                               int        response,
                               gpointer   user_data)
{
  GtkFileChooserWidget *impl = user_data;

  if (response == GTK_RESPONSE_ACCEPT)
    {
      /* Dialog is now going to be closed, so prevent any button/key presses to
       * file list (will be restablished on next map()). Fixes data loss bug #2288 */
      impl->browse_files_interaction_frozen = TRUE;

      request_response_and_add_to_recent_list (impl);
    }

  gtk_window_destroy (GTK_WINDOW (dialog));
}

/* Presents an overwrite confirmation dialog */
static void
confirm_dialog_should_accept_filename (GtkFileChooserWidget *impl,
                                       const char            *file_part,
                                       const char            *folder_display_name)
{
  GtkWindow *toplevel;
  GtkWidget *dialog;

  toplevel = get_toplevel (GTK_WIDGET (impl));

  dialog = gtk_message_dialog_new (toplevel,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("A file named “%s” already exists.  Do you want to replace it?"),
                                   file_part);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("The file already exists in “%s”.  Replacing it will "
                                              "overwrite its contents."),
                                            folder_display_name);

  gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
  add_custom_button_to_dialog (GTK_DIALOG (dialog), _("_Replace"), GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  if (gtk_window_has_group (toplevel))
    gtk_window_group_add_window (gtk_window_get_group (toplevel), GTK_WINDOW (dialog));

  gtk_window_present (GTK_WINDOW (dialog));

  g_signal_connect (dialog, "response",
                    G_CALLBACK (on_confirm_overwrite_response),
                    impl);
}

struct GetDisplayNameData
{
  GtkFileChooserWidget *impl;
  char *file_part;
};

static void
confirmation_confirm_get_info_cb (GObject      *source,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct GetDisplayNameData *data = user_data;
  GFileInfo *info;

  g_clear_object (&data->impl->should_respond_get_info_cancellable);

  info = g_file_query_info_finish (file, result, NULL);
  if (!info)
    goto out;

  confirm_dialog_should_accept_filename (data->impl, data->file_part,
                                         g_file_info_get_display_name (info));

  set_busy_cursor (data->impl, FALSE);

out:
  g_object_unref (data->impl);
  g_free (data->file_part);
  g_free (data);

  g_clear_object (&info);
}

/* Does overwrite confirmation if appropriate, and returns whether the dialog
 * should respond.  Can get the file part from the file list or the save entry.
 */
static gboolean
should_respond_after_confirm_overwrite (GtkFileChooserWidget *impl,
                                        const char           *file_part,
                                        GFile                *parent_file)
{
  struct GetDisplayNameData *data;

  g_assert (file_part != NULL);

  data = g_new0 (struct GetDisplayNameData, 1);
  data->impl = g_object_ref (impl);
  data->file_part = g_strdup (file_part);

  if (impl->should_respond_get_info_cancellable)
    g_cancellable_cancel (impl->should_respond_get_info_cancellable);
  g_clear_object (&impl->should_respond_get_info_cancellable);

  impl->should_respond_get_info_cancellable = g_cancellable_new ();
  g_file_query_info_async (parent_file,
                           "standard::display-name",
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           impl->should_respond_get_info_cancellable,
                           confirmation_confirm_get_info_cb,
                           data);
  set_busy_cursor (data->impl, TRUE);
  return FALSE;
}

static void
name_entry_get_parent_info_cb (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct FileExistsData *data = user_data;
  GFileInfo *info;
  gboolean parent_is_folder = FALSE;
  gboolean parent_is_accessible = FALSE;
  GtkFileChooserWidget *impl = data->impl;
  GError *error = NULL;

  g_clear_object (&impl->should_respond_get_info_cancellable);

  set_busy_cursor (impl, FALSE);

  info = g_file_query_info_finish (file, result, &error);
  if (info)
    {
      parent_is_folder = _gtk_file_info_consider_as_directory (info);

      /* Some gvfs backends do not set executable attribute, let's assume that
       * the folder is accessible even if the attribute is not set.
       */
      parent_is_accessible = !g_file_info_has_attribute (info, "access::can-execute") ||
                             g_file_info_get_attribute_boolean (info, "access::can-execute");
    }

  if (parent_is_folder && parent_is_accessible)
    {
      if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN)
        {
          request_response_and_add_to_recent_list (impl); /* even if the file doesn't exist, apps can make good use of that (e.g. Emacs) */
        }
      else if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
        {
          if (data->file_exists_and_is_not_folder)
            {
              gboolean retval;
              char *file_part;

              /* Dup the string because the string may be modified
               * depending on what clients do in the confirm-overwrite
               * signal and this corrupts the pointer
               */
              file_part = g_strdup (_gtk_file_chooser_entry_get_file_part (GTK_FILE_CHOOSER_ENTRY (impl->location_entry)));
              retval = should_respond_after_confirm_overwrite (impl, file_part, data->parent_file);
              g_free (file_part);

              if (retval)
                request_response_and_add_to_recent_list (impl);
            }
          else
            request_response_and_add_to_recent_list (impl);
        }
      else if (impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
        {
          GError *mkdir_error = NULL;

          /* In both cases (SELECT_FOLDER and CREATE_FOLDER), if you type
           * "/blah/nonexistent" you *will* want a folder created.
           */

          set_busy_cursor (impl, TRUE);
          g_file_make_directory (data->file, NULL, &mkdir_error);
          set_busy_cursor (impl, FALSE);

          if (!mkdir_error)
            request_response_and_add_to_recent_list (impl);
          else
            error_creating_folder_dialog (impl, data->file, mkdir_error);
        }
      else
        g_assert_not_reached ();
    }
  else if (parent_is_folder)
    {
      GError *internal_error;

      internal_error = NULL;
      g_set_error_literal (&internal_error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                           _("You do not have access to the specified folder."));

      error_changing_folder_dialog (impl, data->parent_file, internal_error);
    }
  else if (info)
    {
      /* The parent exists, but it's not a folder!
       * Someone probably typed existing_file.txt/subfile.txt
       */
      error_with_file_under_nonfolder (impl, data->parent_file);
    }
  else
    {
      GError *error_copy;

      /* The parent folder is not readable for some reason */

      error_copy = g_error_copy (error);
      error_changing_folder_dialog (impl, data->parent_file, error_copy);
    }

  g_object_unref (data->impl);
  g_object_unref (data->file);
  g_object_unref (data->parent_file);
  g_free (data);

  g_clear_error (&error);
  g_clear_object (&info);
}

static void
file_exists_get_info_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GFile *file = G_FILE (source);
  struct FileExistsData *data = user_data;
  GFileInfo *info;
  gboolean data_ownership_taken = FALSE;
  gboolean file_exists;
  gboolean is_folder;
  gboolean needs_parent_check = FALSE;
  GtkFileChooserWidget *impl = data->impl;
  GError *error = NULL;

  g_clear_object (&impl->file_exists_get_info_cancellable);

  set_busy_cursor (impl, FALSE);

  info = g_file_query_info_finish (file, result, &error);
  file_exists = (info != NULL);
  is_folder = (file_exists && _gtk_file_info_consider_as_directory (info));

  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN)
    {
      if (is_folder)
        change_folder_and_display_error (impl, data->file, TRUE);
      else
        {
          if (file_exists)
            request_response_and_add_to_recent_list (impl); /* user typed an existing filename; we are done */
          else
            needs_parent_check = TRUE; /* file doesn't exist; see if its parent exists */
        }
    }
  else if (impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      if (!file_exists)
        {
          needs_parent_check = TRUE;
        }
      else
        {
          if (is_folder)
            {
              /* User typed a folder; we are done */
              request_response_and_add_to_recent_list (impl);
            }
          else
            error_selecting_folder_over_existing_file_dialog (impl);
        }
    }
  else if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
      if (is_folder)
        change_folder_and_display_error (impl, data->file, TRUE);
      else
        if (!file_exists && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FILENAME_TOO_LONG))
          error_filename_to_long_dialog (data->impl);
        else
          needs_parent_check = TRUE;
    }
  else
    {
      g_assert_not_reached();
    }

  if (needs_parent_check)
    {
      /* check that everything up to the last path component exists (i.e. the parent) */

      data->file_exists_and_is_not_folder = file_exists && !is_folder;
      data_ownership_taken = TRUE;

      if (impl->should_respond_get_info_cancellable)
        g_cancellable_cancel (impl->should_respond_get_info_cancellable);
      g_clear_object (&impl->should_respond_get_info_cancellable);

      impl->should_respond_get_info_cancellable = g_cancellable_new ();
      g_file_query_info_async (data->parent_file,
                               "standard::type,access::can-execute",
                               G_FILE_QUERY_INFO_NONE,
                               G_PRIORITY_DEFAULT,
                               impl->should_respond_get_info_cancellable,
                               name_entry_get_parent_info_cb,
                               data);
      set_busy_cursor (impl, TRUE);
    }

  if (!data_ownership_taken)
    {
      g_object_unref (impl);
      g_object_unref (data->file);
      g_object_unref (data->parent_file);
      g_free (data);
    }

  g_clear_error (&error);
  g_clear_object (&info);
}

static void
paste_text_received (GObject      *source,
                     GAsyncResult *result,
                     gpointer      data)
{
  GtkFileChooserWidget *impl = data;
  GFile *file;
  char *text;

  text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source), result, NULL);
  if (!text)
    return;

  file = g_file_new_for_uri (text);

  if (!gtk_file_chooser_widget_select_file (GTK_FILE_CHOOSER (impl), file, NULL))
    location_popup_handler (impl, text);

  g_object_unref (file);
  g_free (text);
}

/* Handler for the "location-popup-on-paste" keybinding signal */
static void
location_popup_on_paste_handler (GtkFileChooserWidget *impl)
{
  GdkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET (impl));

  gdk_clipboard_read_text_async (clipboard,
                                 NULL,
                                 paste_text_received,
                                 impl);
}

/* Implementation for GtkFileChooserEmbed::should_respond() */
static void
add_selection_to_recent_list (GtkFileChooserWidget *impl)
{
  GListModel *files;
  guint i, n;

  files = gtk_file_chooser_widget_get_files (GTK_FILE_CHOOSER (impl));

  if (!impl->recent_manager)
    impl->recent_manager = gtk_recent_manager_get_default ();

  n = g_list_model_get_n_items (files);
  for (i = 0; i < n; i++)
    {
      GFile *file = g_list_model_get_item (files, i);
      char *uri;

      uri = g_file_get_uri (file);
      if (uri)
        {
          gtk_recent_manager_add_item (impl->recent_manager, uri);
          g_free (uri);
        }

      g_object_unref (file);
    }

  g_object_unref (files);
}

gboolean
gtk_file_chooser_widget_should_respond (GtkFileChooserWidget *impl)
{
  GtkWidget *toplevel;
  GtkWidget *current_focus;
  gboolean retval;

  toplevel = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (impl)));
  g_assert (GTK_IS_WINDOW (toplevel));

  retval = FALSE;

  current_focus = gtk_root_get_focus (GTK_ROOT (toplevel));

  if (current_focus == impl->browse_files_tree_view)
    {
      /* The following array encodes what we do based on the impl->action and the
       * number of files selected.
       */
      typedef enum {
        NOOP,                   /* Do nothing (don't respond) */
        RESPOND,                /* Respond immediately */
        RESPOND_OR_SWITCH,      /* Respond immediately if the selected item is a file; switch to it if it is a folder */
        ALL_FILES,              /* Respond only if everything selected is a file */
        ALL_FOLDERS,            /* Respond only if everything selected is a folder */
        SAVE_ENTRY,             /* Go to the code for handling the save entry */
        NOT_REACHED             /* Sanity check */
      } ActionToTake;
      static const ActionToTake what_to_do[3][3] = {
        /*                           0 selected  1 selected         many selected */
        /* ACTION_OPEN */          { NOOP,       RESPOND_OR_SWITCH, ALL_FILES   },
        /* ACTION_SAVE */          { SAVE_ENTRY, RESPOND_OR_SWITCH, NOT_REACHED },
        /* ACTION_SELECT_FOLDER */ { RESPOND,    ALL_FOLDERS,       ALL_FOLDERS },
      };

      int num_selected;
      gboolean all_files, all_folders;
      int k;
      ActionToTake action;

    file_list:

      g_assert (impl->action >= GTK_FILE_CHOOSER_ACTION_OPEN && impl->action <= GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

      if (impl->operation_mode == OPERATION_MODE_RECENT)
        {
          if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
            goto save_entry;
          else
            {
              retval = recent_should_respond (impl);
              goto out;
            }
        }

      selection_check (impl, &num_selected, &all_files, &all_folders);

      if (num_selected > 2)
        k = 2;
      else
        k = num_selected;

      action = what_to_do [impl->action] [k];

      switch (action)
        {
        case NOOP:
          return FALSE;

        case RESPOND:
          retval = TRUE;
          goto out;

        case RESPOND_OR_SWITCH:
          g_assert (num_selected == 1);

          if (all_folders)
            {
              switch_to_selected_folder (impl);
              return FALSE;
            }
          else if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
            {
              retval = should_respond_after_confirm_overwrite (impl,
                                                               get_display_name_from_file_list (impl),
                                                               impl->current_folder);
              goto out;
            }
          else
            {
              retval = TRUE;
              goto out;
            }

        case ALL_FILES:
          retval = all_files;
          goto out;

        case ALL_FOLDERS:
          retval = all_folders;
          goto out;

        case SAVE_ENTRY:
          goto save_entry;

        case NOT_REACHED:
        default:
          g_assert_not_reached ();
        }
    }
  else if ((impl->location_entry != NULL) &&
           (current_focus == impl->location_entry ||
            gtk_widget_is_ancestor (current_focus, impl->location_entry)))
    {
      GFile *file;
      gboolean is_well_formed, is_empty, is_file_part_empty;
      gboolean is_folder;
      GtkFileChooserEntry *entry;

    save_entry:

      g_assert (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE ||
                ((impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
                  impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) &&
                 impl->location_mode == LOCATION_MODE_FILENAME_ENTRY));

      entry = GTK_FILE_CHOOSER_ENTRY (impl->location_entry);
      check_save_entry (impl, &file, &is_well_formed, &is_empty, &is_file_part_empty, &is_folder);

      if (!is_well_formed)
        {
          if (!is_empty &&
              impl->action == GTK_FILE_CHOOSER_ACTION_SAVE &&
              impl->operation_mode == OPERATION_MODE_RECENT)
            {
              /* FIXME: ERROR_NO_FOLDER */
#if 0
              /* We'll #ifdef this out, as the fucking treeview selects its first row,
               * thus changing our assumption that no selection is present - setting
               * a selection causes the error message from path_bar_set_mode() to go away,
               * but we want the user to see that message!
               */
              gtk_widget_grab_focus (impl->browse_files_tree_view);
#endif
            }
          /* FIXME: else show an "invalid filename" error as the pathbar mode? */

          return FALSE;
        }

      if (is_empty)
        {
          if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
            {
              /* FIXME: ERROR_NO_FILENAME */
              gtk_widget_grab_focus (impl->location_entry);
              return FALSE;
            }

          goto file_list;
        }

      g_assert (file != NULL);

      if (is_folder)
        {
          if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
              impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
            {
              change_folder_and_display_error (impl, file, TRUE);
            }
          else if (impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
            {
              /* The folder already exists, so we do not need to create it.
               * Just respond to terminate the dialog.
               */
              retval = TRUE;
            }
          else
            {
              g_assert_not_reached ();
            }
        }
      else
        {
          struct FileExistsData *data;

          /* We need to check whether file exists and whether it is a folder -
           * the GtkFileChooserEntry *does* report is_folder==FALSE as a false
           * negative (it doesn't know yet if your last path component is a
           * folder).
           */

          data = g_new0 (struct FileExistsData, 1);
          data->impl = g_object_ref (impl);
          data->file = g_object_ref (file);
          data->parent_file = _gtk_file_chooser_entry_get_current_folder (entry);

          if (impl->file_exists_get_info_cancellable)
            g_cancellable_cancel (impl->file_exists_get_info_cancellable);
          g_clear_object (&impl->file_exists_get_info_cancellable);

          impl->file_exists_get_info_cancellable = g_cancellable_new ();
          g_file_query_info_async (file,
                                   "standard::type",
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT,
                                   impl->file_exists_get_info_cancellable,
                                   file_exists_get_info_cb,
                                   data);

          set_busy_cursor (impl, TRUE);
        }

      g_object_unref (file);
    }
  else if (impl->toplevel_last_focus_widget == impl->browse_files_tree_view)
    {
      /* The focus is on a dialog's action area button, *and* the widget that
       * was focused immediately before it is the file list.
       */
      goto file_list;
    }
  else if (impl->operation_mode == OPERATION_MODE_SEARCH && impl->toplevel_last_focus_widget == impl->search_entry)
    {
      search_entry_activate_cb (impl);
      return FALSE;
    }
  else if (impl->location_entry && impl->toplevel_last_focus_widget == impl->location_entry)
    {
      /* The focus is on a dialog's action area button, *and* the widget that
       * was focused immediately before it is the location entry.
       */
      goto save_entry;
    }
  else
    /* The focus is on a dialog's action area button or something else */
    if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
      goto save_entry;
    else
      goto file_list;

 out:

  if (retval)
    add_selection_to_recent_list (impl);

  return retval;
}

void
gtk_file_chooser_widget_initial_focus (GtkFileChooserWidget *impl)
{
  GtkWidget *widget;

  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
      impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      if (impl->location_mode == LOCATION_MODE_PATH_BAR
          || impl->operation_mode == OPERATION_MODE_RECENT)
        widget = impl->browse_files_tree_view;
      else
        widget = impl->location_entry;
    }
  else if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    widget = impl->location_entry;
  else
    {
      g_assert_not_reached ();
      widget = NULL;
    }

  g_assert (widget != NULL);
  gtk_widget_grab_focus (widget);
}

static void
selected_foreach_get_file_cb (GtkTreeModel *model,
                              GtkTreePath  *path,
                              GtkTreeIter  *iter,
                              gpointer      data)
{
  GSList **list;
  GFile *file;

  list = data;

  gtk_tree_model_get (model, iter, MODEL_COL_FILE, &file, -1);
  /* The file already has a new ref courtesy of gtk_tree_model_get();
   * this will be unreffed by the caller
   */
  *list = g_slist_prepend (*list, file);
}

static GSList *
get_selected_files (GtkFileChooserWidget *impl)
{
  GSList *result;
  GtkTreeSelection *selection;

  result = NULL;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, selected_foreach_get_file_cb, &result);
  result = g_slist_reverse (result);

  return result;
}

static void
selected_foreach_get_info_cb (GtkTreeModel *model,
                              GtkTreePath  *path,
                              GtkTreeIter  *iter,
                              gpointer      data)
{
  GSList **list;
  GFileInfo *info;

  list = data;

  info = _gtk_file_system_model_get_info (GTK_FILE_SYSTEM_MODEL (model), iter);
  *list = g_slist_prepend (*list, g_object_ref (info));
}

static GSList *
get_selected_infos (GtkFileChooserWidget *impl)
{
  GSList *result;
  GtkTreeSelection *selection;

  result = NULL;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_selected_foreach (selection, selected_foreach_get_info_cb, &result);
  result = g_slist_reverse (result);

  return result;
}

/* Callback used from GtkSearchEngine when we get new hits */
static void
search_engine_hits_added_cb (GtkSearchEngine      *engine,
                             GList                *hits,
                             GtkFileChooserWidget *impl)
{
  GList *l, *files, *files_with_info, *infos;
  GFile *file;

  files = NULL;
  files_with_info = NULL;
  infos = NULL;
  for (l = hits; l; l = l->next)
    {
      GtkSearchHit *hit = (GtkSearchHit *)l->data;
      file = g_object_ref (hit->file);
      if (hit->info)
        {
          files_with_info = g_list_prepend (files_with_info, file);
          infos = g_list_prepend (infos, g_object_ref (hit->info));
        }
      else
        files = g_list_prepend (files, file);
    }

  _gtk_file_system_model_update_files (impl->search_model,
                                       files_with_info, infos);
  _gtk_file_system_model_add_and_query_files (impl->search_model,
                                              files, MODEL_ATTRIBUTES);

  g_list_free_full (files, g_object_unref);
  g_list_free_full (files_with_info, g_object_unref);
  g_list_free_full (infos, g_object_unref);

  gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "list");
}

/* Callback used from GtkSearchEngine when the query is done running */
static void
search_engine_finished_cb (GtkSearchEngine *engine,
                           gboolean         got_results,
                           gpointer         data)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (data);

  set_busy_cursor (impl, FALSE);
  gtk_widget_hide (impl->search_spinner);

  if (impl->show_progress_timeout)
    {
      g_source_remove (impl->show_progress_timeout);
      impl->show_progress_timeout = 0;
    }

  if (!got_results)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (impl->browse_files_stack), "empty");
      gtk_widget_grab_focus (impl->search_entry);
    }
}

static void
search_engine_error_cb (GtkSearchEngine *engine,
                        const char      *message,
                        gpointer         data)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (data);

  search_stop_searching (impl, TRUE);
  error_message (impl, _("Could not send the search request"), message);
}

/* Frees the data in the search_model */
static void
search_clear_model (GtkFileChooserWidget *impl,
                    gboolean              remove)
{
  if (!impl->search_model)
    return;

  if (remove &&
      gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view)) == GTK_TREE_MODEL (impl->search_model))
    gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view), NULL);

  g_clear_object (&impl->search_model);
}

/* Stops any ongoing searches; does not touch the search_model */
static void
search_stop_searching (GtkFileChooserWidget *impl,
                       gboolean              remove_query)
{
  if (remove_query && impl->search_entry)
    {
      gtk_editable_set_text (GTK_EDITABLE (impl->search_entry), "");
    }

  if (impl->search_engine)
    {
      _gtk_search_engine_stop (impl->search_engine);
      g_signal_handlers_disconnect_by_data (impl->search_engine, impl);
      g_clear_object (&impl->search_engine);

      set_busy_cursor (impl, FALSE);
      gtk_widget_hide (impl->search_spinner);
    }

  if (impl->show_progress_timeout)
    {
      g_source_remove (impl->show_progress_timeout);
      impl->show_progress_timeout = 0;
    }
}

/* Creates the search_model and puts it in the tree view */
static void
search_setup_model (GtkFileChooserWidget *impl)
{
  g_assert (impl->search_model == NULL);

  impl->search_model = _gtk_file_system_model_new (file_system_model_set,
                                                   impl,
                                                   MODEL_COLUMN_TYPES);

  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (impl->search_model),
                                           search_sort_func,
                                           impl, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (impl->search_model),
                                        GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                        GTK_SORT_ASCENDING);

  gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view),
                           GTK_TREE_MODEL (impl->search_model));

  gtk_tree_view_column_set_sort_column_id (impl->list_name_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_time_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_size_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_type_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_location_column, -1);

  update_columns (impl, TRUE, _("Modified"));
}

static gboolean
show_spinner (gpointer data)
{
  GtkFileChooserWidget *impl = data;

  gtk_widget_show (impl->search_spinner);
  gtk_spinner_start (GTK_SPINNER (impl->search_spinner));
  impl->show_progress_timeout = 0;

  return G_SOURCE_REMOVE;
}

/* Creates a new query with the specified text and launches it */
static void
search_start_query (GtkFileChooserWidget *impl,
                    const char           *query_text)
{
  GFile *file;

  if (gtk_stack_get_visible_child (GTK_STACK (impl->browse_files_stack)) == impl->places_view)
    return;

  stop_loading_and_clear_list_model (impl, TRUE);
  recent_clear_model (impl, TRUE);

  search_stop_searching (impl, FALSE);
  search_clear_model (impl, TRUE);
  search_setup_model (impl);

  set_busy_cursor (impl, TRUE);
  impl->show_progress_timeout = g_timeout_add (1500, show_spinner, impl);

  if (impl->search_engine == NULL)
    impl->search_engine = _gtk_search_engine_new ();

  if (!impl->search_query)
    {
      impl->search_query = gtk_query_new ();
      gtk_query_set_text (impl->search_query, query_text);
    }

  file = gtk_places_sidebar_get_location (GTK_PLACES_SIDEBAR (impl->places_sidebar));
  if (file)
    {
      gtk_query_set_location (impl->search_query, file);
      g_object_unref (file);
    }
  else
    gtk_query_set_location (impl->search_query, impl->current_folder);

  _gtk_search_engine_set_model (impl->search_engine, impl->model_for_search);
  _gtk_search_engine_set_query (impl->search_engine, impl->search_query);

  g_signal_connect (impl->search_engine, "hits-added",
                    G_CALLBACK (search_engine_hits_added_cb), impl);
  g_signal_connect (impl->search_engine, "finished",
                    G_CALLBACK (search_engine_finished_cb), impl);
  g_signal_connect (impl->search_engine, "error",
                    G_CALLBACK (search_engine_error_cb), impl);

  _gtk_search_engine_start (impl->search_engine);

  if (gtk_query_get_location (impl->search_query) &&
      _gtk_file_consider_as_remote (gtk_query_get_location (impl->search_query)))
    gtk_widget_show (impl->remote_warning_bar);

  /* We're not showing the file list here already and instead rely on the
   * GtkSearchEntry timeout and the ::hits-added signal from above to
   * switch. */
}

/* Callback used when the user presses Enter while typing on the search
 * entry; starts the query
 */
static void
search_entry_activate_cb (GtkFileChooserWidget *impl)
{
  const char *text;

  if (impl->operation_mode != OPERATION_MODE_SEARCH)
    return;

  text = gtk_editable_get_text (GTK_EDITABLE (impl->search_entry));

  /* reset any existing query object */
  g_set_object (&impl->search_query, NULL);

  gtk_places_view_set_search_query (GTK_PLACES_VIEW (impl->places_view), text);

  if (text[0] != '\0')
    search_start_query (impl, text);
  else
    search_stop_searching (impl, FALSE);
}

static void
search_entry_stop_cb (GtkFileChooserWidget *impl)
{
  if (impl->search_engine)
    search_stop_searching (impl, FALSE);
  else
    g_object_set (impl, "search-mode", FALSE, NULL);

  impl->starting_search = FALSE;
}

/* Hides the path bar and creates the search entry */
static void
search_setup_widgets (GtkFileChooserWidget *impl)
{
  /* if there already is a query, restart it */
  if (impl->search_query)
    {
      const char *query;

      query = gtk_query_get_text (impl->search_query);
      if (query)
        {
          gtk_editable_set_text (GTK_EDITABLE (impl->search_entry), query);
          search_start_query (impl, query);
        }
      else
        {
          g_object_unref (impl->search_query);
          impl->search_query = NULL;
        }
    }
}

/*
 * Recent files support
 */

/* Frees the data in the recent_model */
static void
recent_clear_model (GtkFileChooserWidget *impl,
                    gboolean              remove)
{
  if (!impl->recent_model)
    return;

  if (remove)
    gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view), NULL);

  g_set_object (&impl->recent_model, NULL);
}

static void
recent_start_loading (GtkFileChooserWidget *impl)
{
  GList *items;

  recent_clear_model (impl, TRUE);

  /* Setup recent model */
  g_assert (impl->recent_model == NULL);

  impl->recent_model = _gtk_file_system_model_new (file_system_model_set,
                                                   impl,
                                                   MODEL_COLUMN_TYPES);

  _gtk_file_system_model_set_filter (impl->recent_model, impl->current_filter);
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (impl->recent_model),
                                           recent_sort_func,
                                           impl, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (impl->recent_model),
                                        GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                        GTK_SORT_DESCENDING);


  if (!impl->recent_manager)
    return;

  items = gtk_recent_manager_get_items (impl->recent_manager);
  if (!items)
    return;

  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN)
    {
      const int limit = DEFAULT_RECENT_FILES_LIMIT;
      const char *app_name = g_get_application_name ();
      GList *l;
      int n;

      n = 0;

      for (l = items; l; l = l->next)
        {
          GtkRecentInfo *info = l->data;
          GFile *file;

          if (gtk_recent_info_get_private_hint (info) &&
              !gtk_recent_info_has_application (info, app_name))
            continue;


          file = g_file_new_for_uri (gtk_recent_info_get_uri (info));
          _gtk_file_system_model_add_and_query_file (impl->recent_model,
                                                     file,
                                                     MODEL_ATTRIBUTES);
          g_object_unref (file);

          n++;
          if (limit != -1 && n >= limit)
            break;
        }

      g_set_object (&impl->model_for_search, impl->recent_model);
    }
  else
    {
      GList *folders;
      GList *l;

      folders = _gtk_file_chooser_extract_recent_folders (items);

      for (l = folders; l; l = l->next)
        _gtk_file_system_model_add_and_query_file (impl->recent_model,
                                                   G_FILE (l->data),
                                                   MODEL_ATTRIBUTES);

      g_list_free_full (folders, g_object_unref);
    }

  g_list_free_full (items, (GDestroyNotify) gtk_recent_info_unref);

  gtk_tree_view_set_model (GTK_TREE_VIEW (impl->browse_files_tree_view),
                           GTK_TREE_MODEL (impl->recent_model));
  gtk_tree_view_set_search_column (GTK_TREE_VIEW (impl->browse_files_tree_view), -1);

  gtk_tree_view_column_set_sort_column_id (impl->list_name_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_time_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_size_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_type_column, -1);
  gtk_tree_view_column_set_sort_column_id (impl->list_location_column, -1);

  update_columns (impl, TRUE, _("Accessed"));
}

/* Called from ::should_respond(). We return whether there are selected
 * files in the recent files list.
 */
static gboolean
recent_should_respond (GtkFileChooserWidget *impl)
{
  GtkTreeSelection *selection;

  g_assert (impl->operation_mode == OPERATION_MODE_RECENT);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  return (gtk_tree_selection_count_selected_rows (selection) != 0);
}

static void
set_current_filter (GtkFileChooserWidget *impl,
                    GtkFileFilter        *filter)
{
  if (impl->current_filter != filter)
    {
      guint filter_index;

      /* NULL filters are allowed to reset to non-filtered status */
      if (filter)
        {
          if (!g_list_store_find (impl->filters, filter, &filter_index))
            filter_index = GTK_INVALID_LIST_POSITION;
        }
      else
        {
          filter_index = -1;
        }

      if (impl->current_filter)
        g_object_unref (impl->current_filter);
      impl->current_filter = filter;
      if (impl->current_filter)
        g_object_ref_sink (impl->current_filter);

      gtk_drop_down_set_selected (GTK_DROP_DOWN (impl->filter_combo), filter_index);

      clear_model_cache (impl, MODEL_COL_IS_SENSITIVE);
      set_model_filter (impl, impl->current_filter);
      g_object_notify (G_OBJECT (impl), "filter");
    }
}

static void
filter_combo_changed (GtkDropDown          *dropdown,
                      GParamSpec           *pspec,
                      GtkFileChooserWidget *impl)
{
  GtkFileFilter *new_filter;

  new_filter = gtk_drop_down_get_selected_item (dropdown);

  set_current_filter (impl, new_filter);

  if (impl->location_entry != NULL)
    _gtk_file_chooser_entry_set_file_filter (GTK_FILE_CHOOSER_ENTRY (impl->location_entry),
                                             new_filter);
}

static gboolean
list_select_func (GtkTreeSelection *selection,
                  GtkTreeModel     *model,
                  GtkTreePath      *path,
                  gboolean          path_currently_selected,
                  gpointer          data)
{
  GtkFileChooserWidget *impl = data;

  if (impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      GtkTreeIter iter;
      gboolean is_sensitive;
      gboolean is_folder;

      if (!gtk_tree_model_get_iter (model, &iter, path))
        return FALSE;
      gtk_tree_model_get (model, &iter,
                          MODEL_COL_IS_SENSITIVE, &is_sensitive,
                          MODEL_COL_IS_FOLDER, &is_folder,
                          -1);
      if (!is_sensitive || !is_folder)
        return FALSE;
    }

  return TRUE;
}

static void
list_selection_changed (GtkTreeSelection     *selection,
                        GtkFileChooserWidget *impl)
{
  if (gtk_tree_view_get_model (GTK_TREE_VIEW (impl->browse_files_tree_view)) == NULL)
    return;

  if (impl->location_entry &&
      impl->browse_files_model)
    update_chooser_entry (impl);

  location_bar_update (impl);
  update_default (impl);
}

static gboolean
browse_files_tree_view_keynav_failed_cb (GtkWidget        *widget,
                                         GtkDirectionType  direction,
                                         gpointer          user_data)
{
 GtkFileChooserWidget *impl = user_data;

 if (direction == GTK_DIR_UP && impl->operation_mode == OPERATION_MODE_SEARCH)
   {
     gtk_widget_grab_focus (impl->search_entry);

     return TRUE;
   }

  return FALSE;
}

/* Callback used when a row in the file list is activated */
static void
list_row_activated (GtkTreeView          *tree_view,
                    GtkTreePath          *path,
                    GtkTreeViewColumn    *column,
                    GtkFileChooserWidget *impl)
{
  GFile *file;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gboolean is_folder;
  gboolean is_sensitive;

  model = gtk_tree_view_get_model (tree_view);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;

  gtk_tree_model_get (model, &iter,
                      MODEL_COL_FILE, &file,
                      MODEL_COL_IS_FOLDER, &is_folder,
                      MODEL_COL_IS_SENSITIVE, &is_sensitive,
                      -1);

  if (is_sensitive && is_folder && file)
    {
      change_folder_and_display_error (impl, file, FALSE);
      goto out;
    }

  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
      impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    gtk_widget_activate_default (GTK_WIDGET (impl));

 out:

  if (file)
    g_object_unref (file);
}

static void
path_bar_clicked (GtkPathBar           *path_bar,
                  GFile                *file,
                  GFile                *child_file,
                  gboolean              child_is_hidden,
                  GtkFileChooserWidget *impl)
{
  if (child_file)
    pending_select_files_add (impl, child_file);

  if (!change_folder_and_display_error (impl, file, FALSE))
    return;

  /* Say we have "/foo/bar/[.baz]" and the user clicks on "bar".  We should then
   * show hidden files so that ".baz" appears in the file list, as it will still
   * be shown in the path bar: "/foo/[bar]/.baz"
   */
  if (child_is_hidden)
    set_show_hidden (impl, TRUE);
}

static void
update_cell_renderer_attributes (GtkFileChooserWidget *impl)
{
  gtk_tree_view_column_set_attributes (impl->list_name_column,
                                       impl->list_pixbuf_renderer,
                                       "gicon", MODEL_COL_ICON,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);
  gtk_tree_view_column_set_attributes (impl->list_name_column,
                                       impl->list_name_renderer,
                                       "text", MODEL_COL_NAME,
                                       "ellipsize", MODEL_COL_ELLIPSIZE,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  gtk_tree_view_column_set_attributes (impl->list_size_column,
                                       impl->list_size_renderer,
                                       "text", MODEL_COL_SIZE_TEXT,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  gtk_tree_view_column_set_attributes (impl->list_type_column,
                                       impl->list_type_renderer,
                                       "text", MODEL_COL_TYPE,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  gtk_tree_view_column_set_attributes (impl->list_time_column,
                                       impl->list_date_renderer,
                                       "text", MODEL_COL_DATE_TEXT,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  gtk_tree_view_column_set_attributes (impl->list_time_column,
                                       impl->list_time_renderer,
                                       "text", MODEL_COL_TIME_TEXT,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  gtk_tree_view_column_set_attributes (impl->list_location_column,
                                       impl->list_location_renderer,
                                       "text", MODEL_COL_LOCATION_TEXT,
                                       "sensitive", MODEL_COL_IS_SENSITIVE,
                                       NULL);

  update_time_renderer_visible (impl);
}

static void
location_set_user_text (GtkFileChooserWidget *impl,
                        const char           *path)
{
  gtk_editable_set_text (GTK_EDITABLE (impl->location_entry), path);
  gtk_editable_set_position (GTK_EDITABLE (impl->location_entry), -1);
}

static void
location_popup_handler (GtkFileChooserWidget *impl,
                        const char           *path)
{
  if (impl->operation_mode != OPERATION_MODE_BROWSE)
    {
      operation_mode_set (impl, OPERATION_MODE_BROWSE);
      if (impl->current_folder)
        change_folder_and_display_error (impl, impl->current_folder, FALSE);
      else
        switch_to_home_dir (impl);
    }

  if (impl->action == GTK_FILE_CHOOSER_ACTION_OPEN ||
      impl->action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {
      if (!path)
        return;

      location_mode_set (impl, LOCATION_MODE_FILENAME_ENTRY);
      location_set_user_text (impl, path);
    }
  else if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
      gtk_widget_grab_focus (impl->location_entry);
      if (path != NULL)
        location_set_user_text (impl, path);
    }
  else
    g_assert_not_reached ();
}

/* Handler for the "up-folder" keybinding signal */
static void
up_folder_handler (GtkFileChooserWidget *impl)
{
  _gtk_path_bar_up (GTK_PATH_BAR (impl->browse_path_bar));
}

/* Handler for the "down-folder" keybinding signal */
static void
down_folder_handler (GtkFileChooserWidget *impl)
{
  _gtk_path_bar_down (GTK_PATH_BAR (impl->browse_path_bar));
}

/* Handler for the "home-folder" keybinding signal */
static void
home_folder_handler (GtkFileChooserWidget *impl)
{
  switch_to_home_dir (impl);
}

/* Handler for the "desktop-folder" keybinding signal */
static void
desktop_folder_handler (GtkFileChooserWidget *impl)
{
  const char *name;
  GFile *file;

  /* "To disable a directory, point it to the homedir."
   * See http://freedesktop.org/wiki/Software/xdg-user-dirs
   */
  name = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
  if (!g_strcmp0 (name, g_get_home_dir ()))
    return;

  file = g_file_new_for_path (name);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (impl), file, NULL);
  g_object_unref (file);
}

/* Handler for the "search-shortcut" keybinding signal */
static void
search_shortcut_handler (GtkFileChooserWidget *impl)
{
  if (impl->operation_mode == OPERATION_MODE_SEARCH)
    {
      operation_mode_set (impl, OPERATION_MODE_BROWSE);
      if (impl->current_folder)
        change_folder_and_display_error (impl, impl->current_folder, FALSE);
      else
        switch_to_home_dir (impl);
    }
  else
    operation_mode_set (impl, OPERATION_MODE_SEARCH);
}

/* Handler for the "recent-shortcut" keybinding signal */
static void
recent_shortcut_handler (GtkFileChooserWidget *impl)
{
  operation_mode_set (impl, OPERATION_MODE_RECENT);
}

/* Handler for the "places-shortcut" keybinding signal */
static void
places_shortcut_handler (GtkFileChooserWidget *impl)
{
  gtk_widget_child_focus (impl->places_sidebar, GTK_DIR_LEFT);
}

static void
quick_bookmark_handler (GtkFileChooserWidget *impl,
                        int                   bookmark_index)
{
  GFile *file;

  file = gtk_places_sidebar_get_nth_bookmark (GTK_PLACES_SIDEBAR (impl->places_sidebar), bookmark_index);

  if (file)
    {
      change_folder_and_display_error (impl, file, FALSE);
      g_object_unref (file);
    }
}

static void
show_hidden_handler (GtkFileChooserWidget *impl)
{
  set_show_hidden (impl, !impl->show_hidden);
}

static void
add_normal_and_shifted_binding (GtkWidgetClass  *widget_class,
                                guint            keyval,
                                GdkModifierType  modifiers,
                                const char      *signal_name)
{
  gtk_widget_class_add_binding_signal (widget_class,
                                       keyval, modifiers,
                                       signal_name,
                                       NULL);

  gtk_widget_class_add_binding_signal (widget_class,
                                       keyval, modifiers | GDK_SHIFT_MASK,
                                       signal_name,
                                       NULL);
}

static void
gtk_file_chooser_widget_size_allocate (GtkWidget *widget,
                                       int        width,
                                       int        height,
                                       int        baseline)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (widget);

  GTK_WIDGET_CLASS (gtk_file_chooser_widget_parent_class)->size_allocate (widget, width, height, baseline);

  if (impl->browse_files_popover)
    gtk_popover_present (GTK_POPOVER (impl->browse_files_popover));
  if (impl->rename_file_popover)
    gtk_popover_present (GTK_POPOVER (impl->rename_file_popover));
}

static void
gtk_file_chooser_widget_class_init (GtkFileChooserWidgetClass *class)
{
  static const guint quick_bookmark_keyvals[10] = {
    GDK_KEY_1, GDK_KEY_2, GDK_KEY_3, GDK_KEY_4, GDK_KEY_5, GDK_KEY_6, GDK_KEY_7, GDK_KEY_8, GDK_KEY_9, GDK_KEY_0
  };
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  int i;

  gobject_class->finalize = gtk_file_chooser_widget_finalize;
  gobject_class->constructed = gtk_file_chooser_widget_constructed;
  gobject_class->set_property = gtk_file_chooser_widget_set_property;
  gobject_class->get_property = gtk_file_chooser_widget_get_property;
  gobject_class->dispose = gtk_file_chooser_widget_dispose;

  widget_class->map = gtk_file_chooser_widget_map;
  widget_class->unmap = gtk_file_chooser_widget_unmap;
  widget_class->root = gtk_file_chooser_widget_root;
  widget_class->unroot = gtk_file_chooser_widget_unroot;
  widget_class->css_changed = gtk_file_chooser_widget_css_changed;
  widget_class->size_allocate = gtk_file_chooser_widget_size_allocate;
  widget_class->grab_focus = gtk_widget_grab_focus_child;
  widget_class->focus = gtk_widget_focus_child;

  /*
   * Signals
   */

  /**
   * GtkFileChooserWidget::location-popup:
   * @widget: the object which received the signal
   * @path: a string that gets put in the text entry for the file name
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show a "Location" prompt which
   * the user can use to manually type the name of the file he wishes to select.
   *
   * The default bindings for this signal are <kbd>Control</kbd>-<kbd>L</kbd>
   * with a @path string of "" (the empty string). It is also bound to
   * <kbd>/</kbd> with a @path string of "`/`" (a slash):  this lets you
   * type `/` and immediately type a path name. On Unix systems, this is
   * bound to <kbd>~</kbd> (tilde) with a @path string of "~" itself for
   * access to home directories.
   */
  signals[LOCATION_POPUP] =
    g_signal_new_class_handler (I_("location-popup"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (location_popup_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * GtkFileChooserWidget::location-popup-on-paste:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show a "Location" prompt
   * when the user pastes into a `GtkFileChooserWidget`.
   *
   * The default binding for this signal is <kbd>Control</kbd>-<kbd>V</kbd>.
   */
  signals[LOCATION_POPUP_ON_PASTE] =
    g_signal_new_class_handler (I_("location-popup-on-paste"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (location_popup_on_paste_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::location-toggle-popup:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to toggle the visibility of a "Location" prompt
   * which the user can use to manually type the name of the file
   * he wishes to select.
   *
   * The default binding for this signal is <kbd>Control</kbd>-<kbd>L</kbd>.
   */
  signals[LOCATION_TOGGLE_POPUP] =
    g_signal_new_class_handler (I_("location-toggle-popup"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (location_toggle_popup_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::up-folder:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser go to the parent
   * of the current folder in the file hierarchy.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>Up</kbd>.
   */
  signals[UP_FOLDER] =
    g_signal_new_class_handler (I_("up-folder"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (up_folder_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::down-folder:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser go to a child of the
   * current folder in the file hierarchy. The subfolder that will
   * be used is displayed in the path bar widget of the file chooser.
   * For example, if the path bar is showing "/foo/bar/baz", with bar
   * currently displayed, then this will cause the file chooser to
   * switch to the "baz" subfolder.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>Down</kbd>.
   */
  signals[DOWN_FOLDER] =
    g_signal_new_class_handler (I_("down-folder"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (down_folder_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::home-folder:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show the user's home
   * folder in the file list.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>Home</kbd>.
   */
  signals[HOME_FOLDER] =
    g_signal_new_class_handler (I_("home-folder"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (home_folder_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::desktop-folder:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show the user's Desktop
   * folder in the file list.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>D</kbd>.
   */
  signals[DESKTOP_FOLDER] =
    g_signal_new_class_handler (I_("desktop-folder"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (desktop_folder_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::quick-bookmark:
   * @widget: the object which received the signal
   * @bookmark_index: the number of the bookmark to switch to
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser switch to the bookmark
   * specified in the @bookmark_index parameter. For example, if
   * you have three bookmarks, you can pass 0, 1, 2 to this signal
   * to switch to each of them, respectively.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>1</kbd>,
   * <kbd>Alt</kbd>-<kbd>2</kbd>, etc. until <kbd>Alt</kbd>-<kbd>0</kbd>.
   * Note that in the default binding, that <kbd>Alt</kbd>-<kbd>1</kbd> is
   * actually defined to switch to the bookmark at index 0, and so on
   * successively.
   */
  signals[QUICK_BOOKMARK] =
    g_signal_new_class_handler (I_("quick-bookmark"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (quick_bookmark_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GtkFileChooserWidget::show-hidden:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser display hidden files.
   *
   * The default binding for this signal is <kbd>Control</kbd>-<kbd>H</kbd>.
   */
  signals[SHOW_HIDDEN] =
    g_signal_new_class_handler (I_("show-hidden"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (show_hidden_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::search-shortcut:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show the search entry.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>S</kbd>.
   */
  signals[SEARCH_SHORTCUT] =
    g_signal_new_class_handler (I_("search-shortcut"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (search_shortcut_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::recent-shortcut:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to make the file chooser show the Recent location.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>R</kbd>.
   */
  signals[RECENT_SHORTCUT] =
    g_signal_new_class_handler (I_("recent-shortcut"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (recent_shortcut_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  /**
   * GtkFileChooserWidget::places-shortcut:
   * @widget: the object which received the signal
   *
   * Emitted when the user asks for it.
   *
   * This is a [keybinding signal](class.SignalAction.html).
   *
   * This is used to move the focus to the places sidebar.
   *
   * The default binding for this signal is <kbd>Alt</kbd>-<kbd>P</kbd>.
   */
  signals[PLACES_SHORTCUT] =
    g_signal_new_class_handler (I_("places-shortcut"),
                                G_OBJECT_CLASS_TYPE (class),
                                G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                G_CALLBACK (places_shortcut_handler),
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 0);

  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_l, GDK_CONTROL_MASK,
                                       "location-toggle-popup",
                                       NULL);

  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_v, GDK_CONTROL_MASK,
                                       "location-popup-on-paste",
                                       NULL);

  add_normal_and_shifted_binding (widget_class,
                                  GDK_KEY_Up, GDK_ALT_MASK,
                                  "up-folder");
  add_normal_and_shifted_binding (widget_class,
                                  GDK_KEY_KP_Up, GDK_ALT_MASK,
                                  "up-folder");

  add_normal_and_shifted_binding (widget_class,
                                  GDK_KEY_Down, GDK_ALT_MASK,
                                  "down-folder");
  add_normal_and_shifted_binding (widget_class,
                                  GDK_KEY_KP_Down, GDK_ALT_MASK,
                                  "down-folder");

  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_Home, GDK_ALT_MASK,
                                       "home-folder",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_KP_Home, GDK_ALT_MASK,
                                       "home-folder",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_d, GDK_ALT_MASK,
                                       "desktop-folder",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_h, GDK_CONTROL_MASK,
                                       "show-hidden",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_s, GDK_ALT_MASK,
                                       "search-shortcut",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_f, GDK_CONTROL_MASK,
                                       "search-shortcut",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_r, GDK_ALT_MASK,
                                       "recent-shortcut",
                                       NULL);
  gtk_widget_class_add_binding_signal (widget_class,
                                       GDK_KEY_p, GDK_ALT_MASK,
                                       "places-shortcut",
                                       NULL);
  gtk_widget_class_add_binding (widget_class,
                                GDK_KEY_slash, 0,
                                trigger_location_entry,
                                "s", "/");
  gtk_widget_class_add_binding (widget_class,
                                GDK_KEY_KP_Divide, 0,
                                trigger_location_entry,
                                "s", "/");
  gtk_widget_class_add_binding (widget_class,
                                GDK_KEY_period, 0,
                                trigger_location_entry,
                                "s", ".");
  gtk_widget_class_add_binding (widget_class,
                                GDK_KEY_asciitilde, 0,
                                trigger_location_entry,
                                "s", "~");

  for (i = 0; i < G_N_ELEMENTS (quick_bookmark_keyvals); i++)
    gtk_widget_class_add_binding_signal (widget_class,
                                         quick_bookmark_keyvals[i], GDK_ALT_MASK,
                                         "quick-bookmark",
                                         "(i)", i);

  g_object_class_install_property (gobject_class, PROP_SEARCH_MODE,
                                   g_param_spec_boolean ("search-mode", NULL, NULL,
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SUBTITLE,
                                   g_param_spec_string ("subtitle", NULL, NULL,
                                                        "",
                                                        GTK_PARAM_READABLE));

  _gtk_file_chooser_install_properties (gobject_class);

  /* Bind class to template */
  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gtk/libgtk/ui/gtkfilechooserwidget.ui");

  /* A *lot* of widgets that we need to handle .... */
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_widgets_hpaned);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_files_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, places_sidebar);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, places_view);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_files_tree_view);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_files_swin);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_header_revealer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_header_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_new_folder_button);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_path_bar_size_group);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, browse_path_bar);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, filter_combo_hbox);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, filter_combo);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, extra_align);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, extra_and_filters);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, location_entry_box);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, search_entry);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, search_spinner);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_name_column);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_pixbuf_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_name_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_time_column);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_date_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_time_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_size_column);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_size_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_type_column);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_type_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_location_column);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, list_location_renderer);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, new_folder_name_entry);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, new_folder_create_button);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, new_folder_error_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, new_folder_popover);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, rename_file_name_entry);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, rename_file_rename_button);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, rename_file_error_stack);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, rename_file_popover);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, remote_warning_bar);
  gtk_widget_class_bind_template_child (widget_class, GtkFileChooserWidget, box);

  /* And a *lot* of callbacks to bind ... */
  gtk_widget_class_bind_template_callback (widget_class, file_list_query_tooltip_cb);
  gtk_widget_class_bind_template_callback (widget_class, list_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, list_selection_changed);
  gtk_widget_class_bind_template_callback (widget_class, browse_files_tree_view_keynav_failed_cb);
  gtk_widget_class_bind_template_callback (widget_class, filter_combo_changed);
  gtk_widget_class_bind_template_callback (widget_class, path_bar_clicked);
  gtk_widget_class_bind_template_callback (widget_class, places_sidebar_open_location_cb);
  gtk_widget_class_bind_template_callback (widget_class, places_sidebar_show_error_message_cb);
  gtk_widget_class_bind_template_callback (widget_class, places_sidebar_show_other_locations_with_flags_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, search_entry_stop_cb);
  gtk_widget_class_bind_template_callback (widget_class, new_folder_popover_active);
  gtk_widget_class_bind_template_callback (widget_class, new_folder_name_changed);
  gtk_widget_class_bind_template_callback (widget_class, new_folder_create_clicked);
  gtk_widget_class_bind_template_callback (widget_class, rename_file_name_changed);
  gtk_widget_class_bind_template_callback (widget_class, rename_file_rename_clicked);
  gtk_widget_class_bind_template_callback (widget_class, rename_file_end);
  gtk_widget_class_bind_template_callback (widget_class, click_cb);
  gtk_widget_class_bind_template_callback (widget_class, long_press_cb);

  gtk_widget_class_set_css_name (widget_class, I_("filechooser"));

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static gboolean
captured_key (GtkEventControllerKey *controller,
              guint                  keyval,
              guint                  keycode,
              GdkModifierType        state,
              gpointer               data)
{
  GtkFileChooserWidget *impl = data;
  gboolean handled;

  if (impl->operation_mode == OPERATION_MODE_SEARCH ||
      impl->operation_mode == OPERATION_MODE_ENTER_LOCATION ||
      (impl->operation_mode == OPERATION_MODE_BROWSE &&
       impl->location_mode == LOCATION_MODE_FILENAME_ENTRY))
    return GDK_EVENT_PROPAGATE;

  if (keyval == GDK_KEY_slash)
    return GDK_EVENT_PROPAGATE;

  if (impl->location_entry)
    {
      GtkWidget *focus = gtk_root_get_focus (gtk_widget_get_root (GTK_WIDGET (impl)));

      if (focus && gtk_widget_is_ancestor (focus, impl->location_entry))
        return GDK_EVENT_PROPAGATE;
    }

  handled = gtk_event_controller_key_forward (controller, GTK_WIDGET (impl->search_entry));
  if (handled == GDK_EVENT_STOP)
    operation_mode_set (impl, OPERATION_MODE_SEARCH);

  return handled;
}

static void
post_process_ui (GtkFileChooserWidget *impl)
{
  GdkContentFormats *drag_formats;
  GtkTreeSelection *selection;
  GFile            *file;
  GtkDropTarget *target;
  GtkGesture *gesture;
  GtkEventController *controller;
  GtkShortcutTrigger *trigger;
  GtkShortcutAction *action;
  GtkShortcut *shortcut;

  /* Setup file list treeview */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (impl->browse_files_tree_view));
  gtk_tree_selection_set_select_function (selection,
                                          list_select_func,
                                          impl, NULL);

  drag_formats = gdk_content_formats_new_for_gtype (GDK_TYPE_FILE_LIST);
  gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (impl->browse_files_tree_view),
                                          GDK_BUTTON1_MASK,
                                          drag_formats,
                                          GDK_ACTION_COPY | GDK_ACTION_MOVE);
  gdk_content_formats_unref (drag_formats);

  target = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (target, "drop", G_CALLBACK (file_list_drag_drop_cb), impl);
  gtk_widget_add_controller (impl->browse_files_tree_view, GTK_EVENT_CONTROLLER (target));

  /* File browser treemodel columns are shared between GtkFileChooser implementations,
   * so we don't set cell renderer attributes in GtkBuilder, but rather keep that
   * in code.
   */
  file_list_set_sort_column_ids (impl);
  update_cell_renderer_attributes (impl);

  file = g_file_new_for_path ("/");
  _gtk_path_bar_set_file (GTK_PATH_BAR (impl->browse_path_bar), file, FALSE);
  g_object_unref (file);

  /* Set the fixed size icon renderer, this requires
   * that impl->icon_size be already setup.
   */
  set_icon_cell_renderer_fixed_size (impl);

  gtk_popover_set_default_widget (GTK_POPOVER (impl->new_folder_popover), impl->new_folder_create_button);
  gtk_popover_set_default_widget (GTK_POPOVER (impl->rename_file_popover), impl->rename_file_rename_button);

  impl->item_actions = G_ACTION_GROUP (g_simple_action_group_new ());
  g_action_map_add_action_entries (G_ACTION_MAP (impl->item_actions),
                                   entries, G_N_ELEMENTS (entries),
                                   impl);
  gtk_widget_insert_action_group (GTK_WIDGET (impl),
                                  "item",
                                  impl->item_actions);

  gtk_search_entry_set_key_capture_widget (GTK_SEARCH_ENTRY (impl->search_entry), impl->search_entry);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller, "key-pressed", G_CALLBACK (captured_key), impl);
  g_signal_connect (controller, "key-released", G_CALLBACK (captured_key), impl);
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  gtk_widget_add_controller (GTK_WIDGET (impl), controller);

  gtk_widget_set_parent (impl->rename_file_popover, GTK_WIDGET (impl));

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect (gesture, "pressed", G_CALLBACK (files_list_clicked), impl);
  gtk_widget_add_controller (GTK_WIDGET (impl->browse_files_tree_view), GTK_EVENT_CONTROLLER (gesture));

  controller = gtk_shortcut_controller_new ();
  trigger = gtk_alternative_trigger_new (gtk_keyval_trigger_new (GDK_KEY_F10, GDK_SHIFT_MASK),
                                         gtk_keyval_trigger_new (GDK_KEY_Menu, 0));
  action = gtk_callback_action_new (list_popup_menu_cb, impl, NULL);
  shortcut = gtk_shortcut_new (trigger, action);
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);
  gtk_widget_add_controller (GTK_WIDGET (impl->browse_files_tree_view), controller);

  /* Add ability to restrict interaction on file list (click and key_press events),
   * needed to prevent data loss bug #2288 */
  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  gtk_gesture_single_set_exclusive (GTK_GESTURE_SINGLE (gesture), TRUE);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
  g_signal_connect (gesture, "pressed", G_CALLBACK (files_list_restrict_clicking), impl);
  gtk_widget_add_controller (impl->browse_files_tree_view, GTK_EVENT_CONTROLLER (gesture));

  controller = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  g_signal_connect (controller, "key-pressed", G_CALLBACK (files_list_restrict_key_presses), impl);
  gtk_widget_add_controller (impl->browse_files_tree_view, controller);
}

void
gtk_file_chooser_widget_set_save_entry (GtkFileChooserWidget *impl,
                                        GtkWidget            *entry)
{
  g_return_if_fail (GTK_IS_FILE_CHOOSER_WIDGET (impl));
  g_return_if_fail (entry == NULL || GTK_IS_FILE_CHOOSER_ENTRY (entry));

  if (impl->action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
      save_widgets_destroy (impl);

      impl->external_entry = entry;
      save_widgets_create (impl);
    }
}

static void
display_changed_cb (GtkWidget            *wiget,
                    GParamSpec           *pspec,
                    GtkFileChooserWidget *impl)
{
  remove_settings_signal (impl);
  check_icon_theme (impl);
}

static char *
filter_name (GtkFileFilter *filter)
{
  return g_strdup (gtk_file_filter_get_name (filter));
}

static void
gtk_file_chooser_widget_init (GtkFileChooserWidget *impl)
{
  GtkExpression *expression;

  impl->select_multiple = FALSE;
  impl->show_hidden = FALSE;
  impl->show_size_column = TRUE;
  impl->show_type_column = TRUE;
  impl->type_format = TYPE_FORMAT_MIME;
  impl->load_state = LOAD_EMPTY;
  impl->reload_state = RELOAD_EMPTY;
  impl->pending_select_files = NULL;
  impl->location_mode = LOCATION_MODE_PATH_BAR;
  impl->operation_mode = OPERATION_MODE_BROWSE;
  impl->sort_column = MODEL_COL_NAME;
  impl->sort_order = GTK_SORT_ASCENDING;
  impl->create_folders = TRUE;
  impl->auto_selecting_first_row = FALSE;
  impl->renamed_file = NULL;

  /* Ensure private types used by the template
   * definition before calling gtk_widget_init_template()
   */
  g_type_ensure (GTK_TYPE_PATH_BAR);
  g_type_ensure (GTK_TYPE_PLACES_VIEW);
  g_type_ensure (GTK_TYPE_PLACES_SIDEBAR);
  g_type_ensure (GTK_TYPE_FILE_CHOOSER_ERROR_STACK);

  gtk_widget_init_template (GTK_WIDGET (impl));
  gtk_widget_set_size_request (impl->browse_files_tree_view, 280, -1);

  g_signal_connect (impl, "notify::display,", G_CALLBACK (display_changed_cb), impl);
  check_icon_theme (impl);

  impl->bookmarks_manager = _gtk_bookmarks_manager_new (NULL, NULL);

  impl->filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  gtk_drop_down_set_model (GTK_DROP_DOWN (impl->filter_combo), G_LIST_MODEL (impl->filters));

  expression = gtk_cclosure_expression_new (G_TYPE_STRING, NULL,
                                            0, NULL,
                                            G_CALLBACK (filter_name),
                                            NULL, NULL);
  gtk_drop_down_set_expression (GTK_DROP_DOWN (impl->filter_combo), expression);
  gtk_expression_unref (expression);

  /* Setup various attributes and callbacks in the UI
   * which cannot be done with GtkBuilder
   */
  post_process_ui (impl);
}

/**
 * gtk_file_chooser_widget_new:
 * @action: Open or save mode for the widget
 *
 * Creates a new `GtkFileChooserWidget`.
 *
 * This is a file chooser widget that can be embedded in custom
 * windows, and it is the same widget that is used by
 * `GtkFileChooserDialog`.
 *
 * Returns: a new `GtkFileChooserWidget`
 */
GtkWidget *
gtk_file_chooser_widget_new (GtkFileChooserAction action)
{
  return g_object_new (GTK_TYPE_FILE_CHOOSER_WIDGET,
                       "action", action,
                       NULL);
}

static void
gtk_file_chooser_widget_add_choice (GtkFileChooser  *chooser,
                                    const char      *id,
                                    const char      *label,
                                    const char     **options,
                                    const char     **option_labels)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkWidget *widget;

  if (impl->choices == NULL)
    {
      impl->choices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      impl->choice_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      set_extra_widget (impl, impl->choice_box);
    }
  else if (g_hash_table_lookup (impl->choices, id))
    {
      g_warning ("Duplicate choice %s", id);
      return;
    }

  if (options)
    {
      GtkWidget *box;
      GtkWidget *combo;

      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_box_append (GTK_BOX (box), gtk_label_new (label));

      combo = gtk_drop_down_new_from_strings ((const char * const *)option_labels);
      g_object_set_data_full (G_OBJECT (combo), "options",
                              g_strdupv ((char **)options), (GDestroyNotify)g_strfreev);
      g_hash_table_insert (impl->choices, g_strdup (id), combo);
      gtk_box_append (GTK_BOX (box), combo);

      widget = box;
    }
  else
    {
      GtkWidget *check;

      check = gtk_check_button_new_with_label (label);
      g_hash_table_insert (impl->choices, g_strdup (id), check);

      widget = check;
    }

  gtk_box_append (GTK_BOX (impl->choice_box), widget);
}

static void
gtk_file_chooser_widget_remove_choice (GtkFileChooser  *chooser,
                                       const char      *id)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkWidget *widget;

  if (impl->choices == NULL)
    return;

  widget = (GtkWidget *)g_hash_table_lookup (impl->choices, id);
  g_hash_table_remove (impl->choices, id);
  gtk_box_remove (GTK_BOX (impl->choice_box), widget);

  if (g_hash_table_size (impl->choices) == 0)
    {
      set_extra_widget (impl, NULL);
      g_hash_table_unref (impl->choices);
      impl->choices = NULL;
      impl->choice_box = NULL;
    }
}

static void
gtk_file_chooser_widget_set_choice (GtkFileChooser  *chooser,
                                    const char      *id,
                                    const char      *option)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkWidget *widget;

  if (impl->choices == NULL)
    return;

  widget = (GtkWidget *)g_hash_table_lookup (impl->choices, id);

  if (GTK_IS_BOX (widget))
    {
      guint i;
      const char **options;
      GtkWidget *dropdown;

      dropdown = gtk_widget_get_last_child (widget);

      options = (const char **) g_object_get_data (G_OBJECT (dropdown), "options");
      for (i = 0; options[i]; i++)
        {
          if (strcmp (option, options[i]) == 0)
            {
              gtk_drop_down_set_selected (GTK_DROP_DOWN (dropdown), i);
              break;
            }
        }
    }
  else if (GTK_IS_CHECK_BUTTON (widget))
    gtk_check_button_set_active (GTK_CHECK_BUTTON (widget), g_str_equal (option, "true"));
}

static const char *
gtk_file_chooser_widget_get_choice (GtkFileChooser  *chooser,
                                    const char      *id)
{
  GtkFileChooserWidget *impl = GTK_FILE_CHOOSER_WIDGET (chooser);
  GtkWidget *widget;

  if (impl->choices == NULL)
    return NULL;

  widget = (GtkWidget *)g_hash_table_lookup (impl->choices, id);
  if (GTK_IS_DROP_DOWN (widget))
    {
      const char **options;
      guint selected;

      options = (const char **) g_object_get_data (G_OBJECT (widget), "options");
      selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (widget));

      return options[selected];
    }
  else if (GTK_IS_CHECK_BUTTON (widget))
    {
      return gtk_check_button_get_active (GTK_CHECK_BUTTON (widget)) ? "true" : "false";
    }

  return NULL;
}

