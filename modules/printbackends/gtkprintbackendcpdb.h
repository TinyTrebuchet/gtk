#ifndef __GTK_PRINT_BACKEND_CPDB_H__
#define __GTK_PRINT_BACKEND_CPDB_H__

#include <glib-object.h>
#include <gtk/gtk.h>
#include "gtkprintbackendprivate.h"
#include <cpdb-libs-frontend.h>
#include <gtkprintercpdb.h>

G_BEGIN_DECLS

#define GTK_TYPE_PRINT_BACKEND_CPDB             (gtk_print_backend_cpdb_get_type ())
#define GTK_PRINT_BACKEND_CPDB(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdb))
#define GTK_IS_PRINT_BACKEND_CPDB(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_PRINT_BACKEND_CPDB))

typedef struct _GtkPrintBackendCpdbClass GtkPrintBackendCpdbClass;
typedef struct _GtkPrintBackendCpdb      GtkPrintBackendCpdb;

GtkPrintBackend *gtk_print_backend_cpdb_new      (void);
GType          gtk_print_backend_cpdb_get_type (void) G_GNUC_CONST;

static void gtk_print_backend_cpdb_finalize                   (GObject *object);

static void cpdb_request_printer_list                         (GtkPrintBackend *backend);
static void cpdb_add_gtk_printer                              (PrinterObj *p, 
                                                               GtkPrintBackend *backend);


static GtkPrintCapabilities cpdb_printer_get_capabilities     (GtkPrinter *printer);

static GtkPrinterOptionSet *cpdb_printer_get_options          (GtkPrinter *printer, 
                                                               GtkPrintSettings *settings, 
                                                               GtkPageSetup *page_setup, 
                                                               GtkPrintCapabilities capabilities);
                                                               
static GList *cpdb_printer_list_papers                        (GtkPrinter *printer);
static GtkPageSetup *cpdb_printer_get_default_page_size       (GtkPrinter *printer);

static void cpdb_fill_gtk_option                              (GtkPrinterOption *gtk_option,
                                                               Option *cpdb_option,
                                                               PrinterObj *p);

static void cpdb_printer_get_settings_from_options            (GtkPrinter *printer,
                                                               GtkPrinterOptionSet *options,
                                                               GtkPrintSettings *settings);
                            
static void cpdb_printer_prepare_for_print                    (GtkPrinter *printer,
                                                               GtkPrintJob *print_job,
                                                               GtkPrintSettings *settings,
                                                               GtkPageSetup *page_setup);
                      
static void cpdb_print_cb                                     (GtkPrintBackendCpdb *cpdb_backend, 
                                                               GError *error, 
                                                               gpointer user_data);

static gboolean cpdb_write                                    (GIOChannel *source,
                                                               GIOCondition con,
                                                               gpointer user_data);

static void cpdb_print_stream                                 (GtkPrintBackend *backend,
                                                               GtkPrintJob *job,
                                                               GIOChannel *data_io,
                                                               GtkPrintJobCompleteFunc callback,
                                                               gpointer user_data,
                                                               GDestroyNotify dnotify);

void func                                                     (GtkPrinterOption *option, gpointer user_data);

static char *localtime_to_utctime                             (const char *local_time);
static gboolean supports_am_pm                                (void);

static void gtk_printer_cpdb_configure_page_setup 		      (GtkPrinter *printer, GtkPageSetup *page_setup);
static void gtk_printer_cpdb_configure_settings				  (const char *key, const char *value, gpointer user_data);

static cairo_surface_t *cpdb_printer_create_cairo_surface     (GtkPrinter *printer,
                                                               GtkPrintSettings *settings,
                                                               double width,
                                                               double height,
                                                               GIOChannel *cache_io);

static void add_printer_callback                              (FrontendObj *f, PrinterObj *p);
static void remove_printer_callback                           (FrontendObj *f, PrinterObj *p);


G_END_DECLS

#endif /* __GTK_PRINT_BACKEND_CPDB_H__ */

