#include <config.h>

#include "gtkprintbackendcpdb.h"

#include <cairo.h>
#include <cairo-pdf.h>

#include <glib/gi18n-lib.h>
#include <sys/random.h>

#define GTK_PRINT_BACKEND_CPDB_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))
#define GTK_IS_PRINT_BACKEND_CPDB_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PRINT_BACKEND_CPDB))
#define GTK_PRINT_BACKEND_CPDB_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))

#define _CPDB_MAX_CHUNK_SIZE 8192



static void gtk_print_backend_cpdb_finalize                   (GObject *object);

static void cpdb_request_printer_list                         (GtkPrintBackend *backend);

static void cpdb_printer_request_details                      (GtkPrinter *printer);
static gpointer acquire_details                               (gpointer data);

static GtkPrintCapabilities cpdb_printer_get_capabilities     (GtkPrinter *printer);

static GtkPrinterOptionSet *cpdb_printer_get_options          (GtkPrinter *printer, 
                                                               GtkPrintSettings *settings, 
                                                               GtkPageSetup *page_setup, 
                                                               GtkPrintCapabilities capabilities);
                                                               
static GList *cpdb_printer_list_papers                        (GtkPrinter *printer);
static GtkPageSetup *cpdb_printer_get_default_page_size       (GtkPrinter *printer);

static gboolean cpdb_printer_get_hard_margins                 (GtkPrinter *printer,
                                                               double *top,
                                                               double *bottom,
                                                               double *left,
                                                               double *right);

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

static void gtk_printer_cpdb_configure_page_setup             (GtkPrinter *printer,
                                                               GtkPageSetup *page_setup, 
                                                               GtkPrintSettings *settings);
static void gtk_printer_cpdb_configure_settings	              (const char *key,
                                                               const char *value, 
                                                               gpointer user_data);

static cairo_surface_t *cpdb_printer_create_cairo_surface     (GtkPrinter *printer,
                                                               GtkPrintSettings *settings,
                                                               double width,
                                                               double height,
                                                               GIOChannel *cache_io);

static void cpdb_fill_gtk_option                              (GtkPrinterOption *gtk_option,
                                                               Option *cpdb_option,
                                                               PrinterObj *p);

static void add_printer_callback                              (PrinterObj *p);
static void remove_printer_callback                           (PrinterObj *p);

static void cpdb_printer_add_list                             (gpointer data, gpointer user_data);
static void cpdb_printer_remove_list                          (gpointer data, gpointer user_data);

static void cpdb_printer_add_hash_table                       (gpointer key, 
                                                               gpointer value, 
                                                               gpointer user_data);

static void cpdb_add_gtk_printer                              (GtkPrintBackend *backend, PrinterObj *p);
static void cpdb_remove_gtk_printer                           (GtkPrintBackend *backend, PrinterObj *p);

static void set_state_message                                 (GtkPrinter *printer, PrinterObj *p);

static void  emit_printer_status_changed                      (gpointer data, gpointer user_data);

static char *random_string                                    (int size);
static char *localtime_to_utctime                             (const char *local_time);
static gboolean supports_am_pm                                (void);



struct _GtkPrintBackendCpdbClass
{
  GtkPrintBackendClass parent_class;
};

struct _GtkPrintBackendCpdb
{
  GtkPrintBackend parent_instance;
};

typedef struct {
  GtkPrintBackend *backend;
  GtkPrintJobCompleteFunc callback;
  GtkPrintJob *job;
  GFileOutputStream *target_io_stream;
  gpointer user_data;
  GDestroyNotify dnotify;
} _PrintStreamData;


/*
 * Declares the class initialization function, 
 * an instance intialization function, and
 * a static variable named gtk_print_backend_cpdb_parent_class pointing to the parent class
 * Also does: typedef _GtkPrintBackendCpdb GtkPrintBackendCpdb
 */
G_DEFINE_DYNAMIC_TYPE (GtkPrintBackendCpdb, gtk_print_backend_cpdb, GTK_TYPE_PRINT_BACKEND)

/*
 * Global FrontendObj shared by all GtkPrintBackend objects
 */
FrontendObj *f;

/*
 * List of all GtkPrintBackend objects, when multiple print dialogs are opened simultaneously
 */
static GList *gtk_print_backends = NULL;


void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  gtk_print_backend_cpdb_register_type (G_TYPE_MODULE (module));

  g_io_extension_point_implement (GTK_PRINT_BACKEND_EXTENSION_POINT_NAME,
                                  GTK_TYPE_PRINT_BACKEND_CPDB,
                                  "cpdb",
                                  10);
}

void
g_io_module_unload (GIOModule *module)
{
}

char **
g_io_module_query (void)
{
  char *eps[] = {
    (char *)GTK_PRINT_BACKEND_EXTENSION_POINT_NAME,
    NULL
  };

  return g_strdupv (eps);
}

/*
 * GtkPrintBackendCpdb
 */

/*
 * gtk_print_backend_cpdb_new:
 *
 * Creates a new #GtkPrintBackendCpdb object. #GtkPrintBackendCpdb
 * implements the #GtkPrintBackend interface with direct access to
 * the filesystem using Unix/Linux API calls
 *
 * Returns: the new #GtkPrintBackendCpdb object
 */
GtkPrintBackend *
gtk_print_backend_cpdb_new (void)
{
  GTK_NOTE (PRINTING,
            g_print ("CPDB Backend: Creating a new CPDB print backend object\n"));

  return g_object_new (GTK_TYPE_PRINT_BACKEND_CPDB, NULL);
}

/*
 * Initialize CPDB PrintBackend class
 */
static void
gtk_print_backend_cpdb_class_init (GtkPrintBackendCpdbClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkPrintBackendClass *backend_class = GTK_PRINT_BACKEND_CLASS (klass);

  gobject_class->finalize = gtk_print_backend_cpdb_finalize;

  backend_class->request_printer_list = cpdb_request_printer_list;
  backend_class->printer_request_details = cpdb_printer_request_details;
  backend_class->printer_get_capabilities = cpdb_printer_get_capabilities;
  backend_class->printer_get_options = cpdb_printer_get_options;
  backend_class->printer_list_papers = cpdb_printer_list_papers;
  backend_class->printer_get_default_page_size = cpdb_printer_get_default_page_size;
  backend_class->printer_get_hard_margins = cpdb_printer_get_hard_margins;
  backend_class->printer_get_settings_from_options = cpdb_printer_get_settings_from_options;
  backend_class->printer_prepare_for_print = cpdb_printer_prepare_for_print;
  backend_class->printer_create_cairo_surface = cpdb_printer_create_cairo_surface;
  backend_class->print_stream = cpdb_print_stream;

  /* 
   * Initialize the global FrontendObj with a random instance name to 
   * prevent conflicts with print dialogs opened from other programs
   */
  char *instance_name, *tmp;
  tmp = random_string(4);
  instance_name = g_strdup_printf ("Gtk_%s", tmp);

  GTK_NOTE (PRINTING,
            g_print ("Creating frontendObj for CPDB backend: %s\n", instance_name));

  f = get_new_FrontendObj (instance_name,
                           (event_callback) add_printer_callback,
                           (event_callback) remove_printer_callback);

  ignore_last_saved_settings(f);
  connect_to_dbus (f);

  g_free(tmp); g_free(instance_name);
}


/*
 * Finalize CPDB PrintBackend class
 */
static void
gtk_print_backend_cpdb_class_finalize (GtkPrintBackendCpdbClass *class)
{
}


/*
 * Intialize CPDB PrintBackend instance
 * Runs everytime for each instance created
 */
static void
gtk_print_backend_cpdb_init (GtkPrintBackendCpdb *cpdb_backend)
{
  GTK_NOTE (PRINTING,
            g_print ("Initializing CPDB backend object\n"));

  gtk_print_backends = g_list_prepend (gtk_print_backends, cpdb_backend);
}

/*
 * Finalize CPDB PrintBackend instance
 * Runs everytime for each instance closed
 */
static void
gtk_print_backend_cpdb_finalize (GObject *object)
{
  GTK_NOTE (PRINTING,
            g_print ("Finalizing CPDB backend object\n"));

  GtkPrintBackendCpdb *backend_cpdb = GTK_PRINT_BACKEND_CPDB (object);
  GObjectClass *backend_parent_class = G_OBJECT_CLASS (gtk_print_backend_cpdb_parent_class);

  gtk_print_backends = g_list_remove(gtk_print_backends, backend_cpdb);

  backend_parent_class->finalize (object);
}

/*
 * This function is responsible for displaying the printer 
 * list obtained from CPDB backend on the print dialog.
 */
static void
cpdb_request_printer_list (GtkPrintBackend *backend)
{
  g_hash_table_foreach (f->printer, cpdb_printer_add_hash_table, backend);

  gtk_print_backend_set_list_done (backend);
}

/*
 * This function is responsible for making a printer acquire all
 * the details and supported options list, which need not be queried 
 * until the user clicks on the printer in the printer list in the dialog.
 * The print dialog runs this function asynchronously and displays 
 * "Getting printer attributes..." as the printer status meanwhile.
 * This function is needed as some printers (like temporary CUPS queue)
 * can take a couple of seconds to materialize and acquire all the details,
 * and it's important the dialog doesn't block during that time.
 */
static void
cpdb_printer_request_details (GtkPrinter *printer)
{
  g_thread_new (NULL, acquire_details, printer);
}

/*
 * Asynchronously acquire details
 */
static gpointer
acquire_details (gpointer data)
{
  GtkPrinter *printer = GTK_PRINTER (data);
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  gtk_printer_set_job_count (printer, get_active_jobs_count (p));

  Options *opts = get_all_options (p);
  if (opts == NULL) 
    {
      GTK_NOTE (PRINTING, g_print ("Error retrieving printer options\n"));
      g_signal_emit_by_name (printer, "details-acquired", FALSE);
      return NULL;
    }
  
  gboolean accepting_jobs = is_accepting_jobs (p);
  gboolean paused = g_strcmp0 (get_state (p), "stopped") == 0;
  gboolean status_changed = paused ^ gtk_printer_is_paused (printer);

  gtk_printer_set_is_accepting_jobs (printer, accepting_jobs);
  gtk_printer_set_is_paused (printer, paused);
  set_state_message (printer, p);

  gtk_printer_set_has_details (printer, TRUE);
  g_signal_emit_by_name (printer, "details-acquired", TRUE);

  if (status_changed)
    g_list_foreach (gtk_print_backends, emit_printer_status_changed, printer);

  return NULL;
}

static void
emit_printer_status_changed (gpointer data,
                             gpointer user_data)
{
  GtkPrintBackend *backend = GTK_PRINT_BACKEND (data);
  GtkPrinter *printer = GTK_PRINTER (user_data);

  g_signal_emit_by_name (backend, "printer-status-changed", printer);
}


/*
 * This function is responsible for specifying which features the
 * print dialog should offer for the given printer.
 */
static GtkPrintCapabilities
cpdb_printer_get_capabilities (GtkPrinter *printer)
{
  GtkPrintCapabilities capabilities = 0;
  Option *cpdb_option;
  GtkPrinterCpdb *cpdb_printer = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (cpdb_printer);

  cpdb_option = get_Option (p, (gchar *) "page-set");
  if (cpdb_option != NULL && cpdb_option->num_supported >= 3)
    capabilities |= GTK_PRINT_CAPABILITY_PAGE_SET;

  cpdb_option = get_Option (p, (gchar *) "copies");
  if (cpdb_option != NULL && g_strcmp0 (cpdb_option->supported_values[0], "1-1") != 0)
    capabilities |= GTK_PRINT_CAPABILITY_COPIES;

  cpdb_option = get_Option (p, (gchar *) "multiple-document-handling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
    capabilities |= GTK_PRINT_CAPABILITY_COLLATE;

  cpdb_option = get_Option (p, (gchar *) "page-delivery");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
    capabilities |= GTK_PRINT_CAPABILITY_REVERSE;

  cpdb_option = get_Option (p, (gchar *) "print-scaling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
    capabilities |= GTK_PRINT_CAPABILITY_SCALE;

  cpdb_option = get_Option (p, (gchar *) "number-up");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP;

  cpdb_option = get_Option (p, (gchar *) "number-up-layout");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT;

  return capabilities;
}


/*
 * This function is responsible for getting all the options
 * that the printer supports and display them into the 
 * GUI template of GTK+ print dialog box.
 * The backend should obtain whatever options it supports,
 * from either its print server or PPDs.
 */
static GtkPrinterOptionSet *
cpdb_printer_get_options (GtkPrinter *printer,
                          GtkPrintSettings *settings,
                          GtkPageSetup *page_setup,
                          GtkPrintCapabilities capabilities)
{
  GtkPrinterCpdb *printer_cpdb;
  PrinterObj *p;
  Option *cpdb_option;
  GtkPrinterOption *gtk_option;
  GtkPrinterOptionSet *gtk_option_set = gtk_printer_option_set_new();

  printer_cpdb = GTK_PRINTER_CPDB (printer);
  p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  /** Page-Setup **/
  cpdb_option = get_Option (p, (gchar *) "number-up");
  if ((capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP) && cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-n-up",
                                         "Pages per Sheet",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "number-up-layout");
  if ((capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT) && cpdb_option != NULL)
  {
    cpdb_option = get_Option (p, (gchar *) "number-up-layout");
    gtk_option = gtk_printer_option_new ("gtk-n-up-layout",
                                         "Page Ordering",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "sides");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-duplex",
                                         "Duplex Printing",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "media-source");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-paper-source",
                                           "Paper source",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  cpdb_option = get_Option (p, (gchar *) "media-type");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-paper-type",
                                           "Paper Type",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  cpdb_option = get_Option (p, (gchar *) "output-bin");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-output-tray",
                                           "Output Tray",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }


  /** Jobs **/
  cpdb_option = get_Option (p, (gchar *) "job-priority");
  if (cpdb_option != NULL)
  {
    // job-priority is represented as a number from 1-100
    const gchar *prio[] = {"100", "80", "50", "30"};
    const gchar *prio_display[] = {"Urgent", "High", "Medium", "Low"};
    gtk_option = gtk_printer_option_new ("gtk-job-prio",
                                         "Job Priority",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    gtk_printer_option_choices_from_array (gtk_option,
                                           G_N_ELEMENTS (prio),
                                           prio, prio_display);

    gtk_printer_option_set (gtk_option, "50");
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "job-sheets");
  if (cpdb_option != NULL) {
    gtk_option = gtk_printer_option_new ("gtk-cover-before",
                                         "Before",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);

    gtk_option = gtk_printer_option_new ("gtk-cover-after",
                                         "After",
                                         GTK_PRINTER_OPTION_TYPE_PICKONE);

    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  gtk_option = gtk_printer_option_new ("gtk-billing-info",
                                       "Billing Info",
                                       GTK_PRINTER_OPTION_TYPE_STRING);

  gtk_printer_option_set (gtk_option, "");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);

  const char *print_at[] = {"now", "at", "on-hold"};
  gtk_option = gtk_printer_option_new ("gtk-print-time",
                                       "Print at",
                                       GTK_PRINTER_OPTION_TYPE_PICKONE);

  gtk_printer_option_choices_from_array (gtk_option,
                                         G_N_ELEMENTS (print_at),
                                         print_at, print_at);

  gtk_printer_option_set (gtk_option, "now");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);

  gtk_option = gtk_printer_option_new ("gtk-print-time-text",
                                       "Print at time",
                                       GTK_PRINTER_OPTION_TYPE_STRING);

  gtk_printer_option_set (gtk_option, "");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);


  /** Image Quality **/
  cpdb_option = get_Option (p, (gchar *) "printer-resolution");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("printer-resolution",
                                           "Resolution",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ImageQualityPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }
  
  cpdb_option = get_Option (p, (gchar *) "print-quality");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("print-quality",
                                           "Print quality",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ImageQualityPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  /** Color **/
  cpdb_option = get_Option (p, (gchar *) "print-color-mode");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("print-color-mode",
                                           "Print Color Mode",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ColorPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }
  
  /** Advanced **/
  cpdb_option = get_Option (p, (gchar *) "print-scaling");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("print-scaling",
                                           "Print Scaling",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("Advanced");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  /* Check if borderles printing is supprted */
  gboolean borderless = TRUE;
  const char *attrs[] = {"media-top-margin", "media-bottom-margin", "media-left-margin", "media-right-margin"};
  for (int i=0; i<4; i++) 
    {
      cpdb_option = get_Option (p, (gchar *) attrs[i]);
      gboolean found = FALSE;

      if (cpdb_option != NULL)
        {
          for (int j=0; j<cpdb_option->num_supported; j++)
            {
              if (g_strcmp0(cpdb_option->supported_values[j], "0") == 0)
                {
                  found = TRUE;
                  break;
                }
            }
        }

      if (!found)
        {
          borderless = FALSE;
          break;
        }
    }
  
  if (borderless)
    {
      gtk_option = gtk_printer_option_new ("borderless",
                                           "Borderless",
                                           GTK_PRINTER_OPTION_TYPE_PICKONE);

      gtk_printer_option_allocate_choices (gtk_option, 2);
      gtk_option->choices[0] = g_strdup ("true");
      gtk_option->choices_display[0] = g_strdup ("True");
      gtk_option->choices[1] = g_strdup ("false");
      gtk_option->choices_display[1] = g_strdup ("False");
      gtk_option->group = g_strdup ("Advanced");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  return gtk_option_set;
}

/* 
 * This function is responsible for listing all the page sizes supported by a printer
 */
static GList *
cpdb_printer_list_papers (GtkPrinter *printer)
{
  int width, height;
  char *display_name;
  double left, right, top, bottom;
  GList *result = NULL;
  Option *media;
  GtkPageSetup *page_setup;
  GtkPaperSize *paper_size;
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  left = g_ascii_strtod (get_default (p, (char *) "media-left-margin"), NULL);
  left = left / 100.0;

  right = g_ascii_strtod (get_default (p, (char *) "media-right-margin"), NULL);
  right = right / 100.0;

  top = g_ascii_strtod (get_default (p, (char *) "media-top-margin"), NULL);
  top = top / 100.0;

  bottom = g_ascii_strtod (get_default (p, (char *) "media-bottom-margin"), NULL);
  bottom = bottom / 100.0;

  media = get_Option (p, (gchar *) "media");
  if (media != NULL)
    {
      for (int i=0; i<media->num_supported; i++)
        {
          if (!g_str_has_prefix (media->supported_values[i], "custom_min") &&
              !g_str_has_prefix (media->supported_values[i], "custom_max"))
            {
              display_name = get_human_readable_choice_name (p,
                                                             (char *) "media",
                                                             media->supported_values[i]);

              get_media_size(p,
                             (const char *) media->supported_values[i],
                             &width,
                             &height);

              page_setup = gtk_page_setup_new ();

              paper_size = gtk_paper_size_new_custom (media->supported_values[i],
                                                      display_name,
                                                      width/100.0,
                                                      height/100.0,
                                                      GTK_UNIT_MM);

              gtk_page_setup_set_paper_size (page_setup, paper_size);
              gtk_page_setup_set_left_margin (page_setup, left, GTK_UNIT_MM);
              gtk_page_setup_set_right_margin (page_setup, right, GTK_UNIT_MM);
              gtk_page_setup_set_top_margin (page_setup, top, GTK_UNIT_MM);
              gtk_page_setup_set_bottom_margin (page_setup, bottom, GTK_UNIT_MM);

              gtk_paper_size_free (paper_size);

              result = g_list_prepend (result, page_setup);
            }
        }
      result = g_list_reverse (result);
    }

  return result;
}

/*
 * This function is responsible for getting the default page size for a printer
 */
static GtkPageSetup *
cpdb_printer_get_default_page_size (GtkPrinter *printer)
{
  int width, height;
  double left, right, top, bottom;
  char *display_name, *default_media;
  GtkPaperSize *paper_size;
  GtkPageSetup *page_setup = NULL;
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  left = g_ascii_strtod (get_default (p, (char *) "media-left-margin"), NULL);
  left = left / 100.0;

  right = g_ascii_strtod (get_default (p, (char *) "media-right-margin"), NULL);
  right = right / 100.0;

  top = g_ascii_strtod (get_default (p, (char *) "media-top-margin"), NULL);
  top = top / 100.0;

  bottom = g_ascii_strtod (get_default (p, (char *) "media-bottom-margin"), NULL);
  bottom = bottom / 100.0;

  default_media = get_default (p, (gchar *) "media");
  if (default_media != NULL)
    {
      display_name = get_human_readable_choice_name (p,
                                                     (char *) "media",
                                                     default_media);

      get_media_size(p, (const char *) default_media, &width, &height);

      page_setup = gtk_page_setup_new ();
      paper_size = gtk_paper_size_new_custom (default_media,
                                              display_name,
                                              width/100.0,
                                              height/100.0,
                                              GTK_UNIT_MM);

      gtk_page_setup_set_paper_size (page_setup, paper_size);
      gtk_page_setup_set_left_margin (page_setup, left, GTK_UNIT_MM);
      gtk_page_setup_set_right_margin (page_setup, right, GTK_UNIT_MM);
      gtk_page_setup_set_top_margin (page_setup, top, GTK_UNIT_MM);
      gtk_page_setup_set_bottom_margin (page_setup, bottom, GTK_UNIT_MM);

      gtk_paper_size_free (paper_size);
    }

  return page_setup;
}

/* 
 * This function is responsible for getting the
 * default page size margins supported by a printer
 */
static gboolean
cpdb_printer_get_hard_margins (GtkPrinter *printer,
                               double *top,
                               double *bottom,
                               double *left,
                               double *right)
{
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  *left = g_ascii_strtod (get_default (p, (char *) "media-left-margin"),
                          NULL);
  *left /= 100.0;

  *right = g_ascii_strtod (get_default (p, (char *) "media-right-margin"),
                           NULL);
  *right /= 100.0;

  *top = g_ascii_strtod (get_default (p, (char *) "media-top-margin"),
                         NULL);
  *top /= 100.0;

  *bottom = g_ascii_strtod (get_default (p, (char *) "media-bottom-margin"),
                            NULL);
  *bottom /= 100.0;

  return TRUE;
}


/*
 * This method is invoked when the print button on the print dialog is pressed.
 * It's responsible for gathering all the settings from the print dialog that the
 * user may have selected before pressing the print button.
 */
static void
cpdb_printer_get_settings_from_options (GtkPrinter *printer,
					GtkPrinterOptionSet *options,
					GtkPrintSettings *settings)
{
  GtkPrinterOption *option;

  option = gtk_printer_option_set_lookup (options, "gtk-n-up");
  if (option)
    gtk_print_settings_set (settings, "number-up", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-n-up-layout");
  if (option)
    gtk_print_settings_set (settings, "number-up-layout", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-duplex");
  if (option)
    gtk_print_settings_set (settings, "sides", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-paper-source");
  if (option)
    gtk_print_settings_set (settings, "media-source", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-paper-type");
  if (option)
    gtk_print_settings_set (settings, "media-type", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-output-tray");
  if (option)
    gtk_print_settings_set (settings, "output-bin", option->value);

  option = gtk_printer_option_set_lookup (options, "gtk-job-prio");
  if (option)
    gtk_print_settings_set (settings, "job-priority", option->value);

  GtkPrinterOption *cover_before, *cover_after;
  cover_before = gtk_printer_option_set_lookup (options, "gtk-cover-before");
  cover_after = gtk_printer_option_set_lookup (options, "gtk-cover-after");
  if (cover_before && cover_after)
    {
      char *value = g_strdup_printf ("%s,%s",
                                     cover_before->value,
                                     cover_after->value);

      gtk_print_settings_set (settings, "job-sheets", value);
      g_free (value);
    }

  option = gtk_printer_option_set_lookup (options, "gtk-billing-info");
  if (option)
    gtk_print_settings_set (settings, "billing-info", option->value);
	
  char *print_at = NULL;
  option = gtk_printer_option_set_lookup (options, "gtk-print-time");
  if (option)
    print_at = g_strdup (option->value);

  char *print_at_time = NULL;
  option = gtk_printer_option_set_lookup (options, "gtk-print-time-text");
  if (option)
    print_at_time = g_strdup (option->value);

  if (print_at && print_at_time)
    {
      if (g_strcmp0 (print_at, "at") == 0)
        {
          char *utc_time = NULL;

          utc_time = localtime_to_utctime (print_at_time);
          if (utc_time)
            {
              gtk_print_settings_set (settings, "job-hold-until", utc_time);
              g_free (utc_time);
            }
          else
            gtk_print_settings_set (settings, "job-hold-until", print_at_time);
        }
      else if (g_strcmp0 (print_at, "on-hold") == 0)
        gtk_print_settings_set (settings, "job-hold-until", "indefinite");
    }

  if (print_at) g_free (print_at);
  if (print_at_time) g_free (print_at_time);

  option = gtk_printer_option_set_lookup (options, "printer-resolution");
  if (option)
    gtk_print_settings_set (settings, "printer-resolution", option->value);

  option = gtk_printer_option_set_lookup (options, "print-quality");
  if (option)
    gtk_print_settings_set (settings, "print-quality", option->value);
  
  option = gtk_printer_option_set_lookup (options, "print-color-mode");
  if (option)
    gtk_print_settings_set (settings, "print-color-mode", option->value);
  
  option = gtk_printer_option_set_lookup (options, "print-scaling");
  if (option)
    gtk_print_settings_set (settings, "print-scaling", option->value);

  option = gtk_printer_option_set_lookup (options, "borderless");
  if (option)
    gtk_print_settings_set (settings, "borderless", option->value);

}

static cairo_status_t
_cairo_write (void *closure,
              const unsigned char *data,
              unsigned int length)
{
  GIOChannel *io = (GIOChannel *)closure;
  gsize written;
  GError *error;

  error = NULL;

  GTK_NOTE (PRINTING,
            g_print ("CPDB Backend: Writing %i byte chunk to temp file\n", length));

  while (length > 0)
    {
      g_io_channel_write_chars (io, (const char *)data, length, &written, &error);

      if (error != NULL)
        {
          GTK_NOTE (PRINTING,
                    g_print ("CPDB Backend: Error writing to temp file, %s\n", error->message));

          g_error_free (error);
          return CAIRO_STATUS_WRITE_ERROR;
        }

      GTK_NOTE (PRINTING,
                g_print ("CPDB Backend: Wrote %" G_GSIZE_FORMAT " bytes to temp file\n", written));

      data += written;
      length -= written;
    }

  return CAIRO_STATUS_SUCCESS;
}


/*
 * called after prepare_for_print()
 */
static cairo_surface_t *
cpdb_printer_create_cairo_surface  (GtkPrinter *printer,
                                    GtkPrintSettings *settings,
                                    double width,
                                    double height,
                                    GIOChannel *cache_io)
{
  cairo_surface_t *surface;

  surface = cairo_pdf_surface_create_for_stream (_cairo_write, cache_io, width, height);

  cairo_surface_set_fallback_resolution  (surface,
                                          2.0 * gtk_print_settings_get_printer_lpi (settings),
                                          2.0 * gtk_print_settings_get_printer_lpi (settings));

  return surface;
}

static void
gtk_printer_cpdb_configure_settings (const char *key,
                                     const char *value,
                                     gpointer user_data)
{
  GtkPrinterCpdb *printer_cpdb;
	PrinterObj *p;

	printer_cpdb = GTK_PRINTER_CPDB (user_data);
  p = gtk_printer_cpdb_get_pObj (printer_cpdb);

	add_setting_to_printer (p, (char *) key, (char *) value);
}

static void
gtk_printer_cpdb_configure_page_setup (GtkPrinter *printer,
                                       GtkPageSetup *page_setup,
                                       GtkPrintSettings *settings)
{
  char *value;
  const char *borderless;
  double width, height, left, top, right, bottom;
  int orientation, default_orientation;
  GtkPageOrientation page_orientation;
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);

  width = gtk_page_setup_get_paper_width (page_setup, GTK_UNIT_MM) * 100.0;
  height = gtk_page_setup_get_paper_height (page_setup, GTK_UNIT_MM) * 100.0;

  left = gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM) * 100.0;
  right = gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM) * 100.0;
  top = gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM) * 100.0;
  bottom = gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM) * 100.0;

  borderless = gtk_print_settings_get (settings, "borderless");
  if (g_ascii_strcasecmp (borderless, "true") == 0) 
    left = right = top = bottom = 0;

  value = g_strdup_printf ("{media-size={x-dimension=%.0f y-dimension=%.0f} "
                            "media-bottom-margin=%.0f "
                            "media-left-margin=%.0f "
                            "media-right-margin=%.0f "
                            "media-top-margin=%.0f}", 
                            width, height, bottom, left, right, top);
  gtk_print_settings_set (settings, "media-col", value);
  g_free (value);

  page_orientation = gtk_page_setup_get_orientation (page_setup);
  default_orientation = g_ascii_strtoll (get_default (p, (char *) "orientation-requested"),
                                         NULL,
                                         0);
  switch (page_orientation)
    {
    case GTK_PAGE_ORIENTATION_PORTRAIT:
      orientation = 3;
      break;

    case GTK_PAGE_ORIENTATION_LANDSCAPE:
      orientation = 4;
      break;

    case GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE:
      orientation = 5;
      break;

    case GTK_PAGE_ORIENTATION_REVERSE_PORTRAIT:
      orientation = 6;
      break;

    default:
      orientation = default_orientation;
    }

  value = g_strdup_printf ("%d", orientation);
  gtk_print_settings_set (settings, "orientation-requested", value);
  g_free(value);
}

static void
cpdb_printer_prepare_for_print (GtkPrinter *printer,
                                GtkPrintJob *print_job,
                                GtkPrintSettings *settings,
                                GtkPageSetup *page_setup)
{
  int n_ranges;
  double scale;
  GtkPrintPages pages;
  GtkPageRange *ranges;
  GtkPageSet page_set;
  GtkPrintCapabilities capabilities;

  capabilities = cpdb_printer_get_capabilities (printer);

  pages = gtk_print_settings_get_print_pages (settings);
  gtk_print_job_set_pages (print_job, pages);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_PRINT_PAGES);

  if (pages == GTK_PRINT_PAGES_RANGES)
    ranges = gtk_print_settings_get_page_ranges (settings, &n_ranges);
  else
    {
      ranges = NULL;
      n_ranges = 0;
    }
  gtk_print_job_set_page_ranges (print_job, ranges, n_ranges);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_PAGE_RANGES);

  scale = gtk_print_settings_get_scale (settings);
  if (scale != 100.0)
    gtk_print_job_set_scale (print_job, scale / 100.0);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_SCALE);

  if (capabilities & GTK_PRINT_CAPABILITY_COLLATE)
    {
      if (gtk_print_settings_get_collate (settings))
        gtk_print_settings_set (settings,
                                "multiple-document-handling",
                                "separate-documents-collated-copies");
    }
  gtk_print_job_set_collate (print_job, FALSE);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_COLLATE);

  if (capabilities & GTK_PRINT_CAPABILITY_REVERSE)
    {
      if (gtk_print_settings_get_reverse (settings))
        gtk_print_settings_set (settings, "page-delivery", "reverse-order");
    }
  gtk_print_job_set_reverse (print_job, FALSE);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_REVERSE);

  if (capabilities & GTK_PRINT_CAPABILITY_COPIES) 
    {
      int copies = gtk_print_settings_get_n_copies (settings);
      if (copies > 1)
        {
          char *value = g_strdup_printf ("%d", copies);
          gtk_print_settings_set (settings, "copies", value);
          g_free (value);
        }
    }
  gtk_print_job_set_num_copies (print_job, 1);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_N_COPIES);

  page_set = gtk_print_settings_get_page_set (settings);
  switch (page_set)
    {
    case GTK_PAGE_SET_EVEN :
      gtk_print_settings_set (settings, "page-set", "even");
      break;

    case GTK_PAGE_SET_ODD :
      gtk_print_settings_set (settings, "page-set", "odd");
      break;

    case GTK_PAGE_SET_ALL :
    default :
      gtk_print_settings_set (settings, "page-set", "all");
    }
  gtk_print_job_set_page_set (print_job, GTK_PAGE_SET_ALL);
  gtk_print_settings_unset (settings, GTK_PRINT_SETTINGS_PAGE_SET);

  gtk_printer_cpdb_configure_page_setup (printer, page_setup, settings);

  g_print ("Configuring print settings\n");
  gtk_print_settings_foreach (settings,
                              gtk_printer_cpdb_configure_settings,
                              printer);
}


static void 
cpdb_print_cb  (GtkPrintBackendCpdb *backend_cpdb, 
                GError *error, 
                gpointer user_data)
{
  char *uri, *path;

  _PrintStreamData *ps = (_PrintStreamData *) user_data;
  GtkRecentManager *recent_manager;
  GtkPrinterCpdb *printer_cpdb;
  PrinterObj *p;

  if (ps->target_io_stream != NULL)
    (void)g_output_stream_close (G_OUTPUT_STREAM (ps->target_io_stream), NULL, NULL);

  if (ps->callback)
    ps->callback (ps->job, ps->user_data, error);

  if (ps->dnotify)
    ps->dnotify (ps->user_data);

  gtk_print_job_set_status (ps->job,
                            error ? GTK_PRINT_STATUS_FINISHED_ABORTED
                                  : GTK_PRINT_STATUS_FINISHED);

  recent_manager = gtk_recent_manager_get_default ();
  uri = g_strdup ("file:///tmp/output.pdf");
  gtk_recent_manager_add_item (recent_manager, uri);
  g_free (uri);

  if (!error)
    {
      path = g_strdup("/tmp/output.pdf");
      printer_cpdb = GTK_PRINTER_CPDB (gtk_print_job_get_printer (ps->job));
      p = gtk_printer_cpdb_get_pObj (printer_cpdb);
      g_print ("Sending file to CPDB for printing\n");
      print_file (p, path);
      g_free (path);
    }

  if (ps->job)
    g_object_unref (ps->job);

  g_free (ps);
}

static gboolean
cpdb_write (GIOChannel *source,
            GIOCondition con,
            gpointer user_data)
{
  char buf[_CPDB_MAX_CHUNK_SIZE];
  gsize bytes_read;
  GError *error;
  GIOStatus status;
  _PrintStreamData *ps = (_PrintStreamData *) user_data;

  error = NULL;

  status = g_io_channel_read_chars (source,
                                    buf,
                                    _CPDB_MAX_CHUNK_SIZE,
                                    &bytes_read,
                                    &error);

  if (status != G_IO_STATUS_ERROR)
    {
      gsize bytes_written;

      g_output_stream_write_all (G_OUTPUT_STREAM (ps->target_io_stream),
                                buf,
                                bytes_read,
                                &bytes_written,
                                NULL,
                                &error);
    }

  if (error != NULL || status == G_IO_STATUS_EOF)
    {
      cpdb_print_cb (GTK_PRINT_BACKEND_CPDB (ps->backend),
                     error,
                     user_data);

      if (error != NULL)
        {
          GTK_NOTE (PRINTING,
                    g_print ("CPDB Backend: %s\n", error->message));

          g_error_free (error);
        }

      return FALSE;
    }

  GTK_NOTE (PRINTING,
            g_print ("CPDB Backend: Writing %" G_GSIZE_FORMAT " byte chunk to cpdb pipe\n", bytes_read));

  return TRUE;
}



static void 
cpdb_print_stream  (GtkPrintBackend *backend,
                    GtkPrintJob *job,
                    GIOChannel *data_io,
                    GtkPrintJobCompleteFunc callback,
                    gpointer user_data,
                    GDestroyNotify dnotify)
{
  GError *error;
  _PrintStreamData *ps;
  char *uri;
  GFile *file;

  ps = g_new0 (_PrintStreamData, 1);
  ps->callback = callback;
  ps->user_data = user_data;
  ps->dnotify = dnotify;
  ps->job = g_object_ref(job);
  ps->backend = backend;
  
  // TODO: generate proper uri
  error = NULL;
  uri = g_strdup ("file:///tmp/output.pdf");
  
  if (uri == NULL)
	  goto error;

  file = g_file_new_for_uri (uri);
  ps->target_io_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error);

  g_object_unref (file);
  g_free (uri);

error:
  if (error != NULL)
    {
      GTK_NOTE (PRINTING,
                g_print ("Error: %s\n", error->message));

      cpdb_print_cb (GTK_PRINT_BACKEND_CPDB (backend), error, ps);

      g_error_free (error);
      return;
    }

  g_io_add_watch (data_io,
                  G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                  (GIOFunc) cpdb_write,
                  ps);

}

/* 
 * Whenever a new printer is found for FrontendObj,
 * it must be added to the printer list of all GtkPrintBackends
 */
static void
add_printer_callback (PrinterObj *p)
{
  GTK_NOTE (PRINTING,
            g_message ("Found printer %s : %s\n", p->name, p->backend_name));

  g_list_foreach (gtk_print_backends, cpdb_printer_add_list, p);
}

/* 
 * Whenever an old printer is lost for FrontendObj,
 * it must be removed from the printer list of all GtkPrintBackends
 */
static void
remove_printer_callback (PrinterObj *p)
{
  GTK_NOTE (PRINTING,
            g_message ("Lost printer %s : %s\n", p->name, p->backend_name));

  g_list_foreach (gtk_print_backends, cpdb_printer_remove_list, p);

  free (p);
}

static void
cpdb_printer_add_list (gpointer data,             // data = GtkPrintBackend
                       gpointer user_data)        // user_data = PrinterObj
{
  GtkPrintBackend *backend = GTK_PRINT_BACKEND (data);
  GtkPrinter *printer;
  PrinterObj *p = (PrinterObj *) user_data;

  cpdb_add_gtk_printer (backend, p);

  printer = gtk_print_backend_find_printer (backend, p->name);
  g_signal_emit_by_name (backend, "printer-added", printer);
  g_signal_emit_by_name (backend, "printer-list-changed");
}

static void
cpdb_printer_remove_list (gpointer data,          // data = GtkPrintBackend
                          gpointer user_data)     // user_data = PrinterObj
{
  cpdb_remove_gtk_printer (data, user_data);
}

static void
cpdb_printer_add_hash_table (gpointer key,        // key = printer name
                             gpointer value,      // value = PrinterObj
                             gpointer user_data)  // user_data = GtkPrintBackend
{
  cpdb_add_gtk_printer (user_data, value);
}

/*
 * Adds given printer to given GtkPrintBackend
 */
static void
cpdb_add_gtk_printer (GtkPrintBackend *backend, PrinterObj *p)
{
  GtkPrinter *printer;
  GtkPrinterCpdb *printer_cpdb;

  printer_cpdb = g_object_new (GTK_TYPE_PRINTER_CPDB,
                               "name", p->name,
                               "backend", backend,
                               NULL);
  gtk_printer_cpdb_set_pObj (printer_cpdb, p);

  printer = GTK_PRINTER (printer_cpdb);
  gtk_printer_set_icon_name (printer, "printer");
  gtk_printer_set_location (printer, p->location);
  gtk_printer_set_description (printer, p->info);
  gtk_printer_set_accepts_pdf (printer, TRUE);
  gtk_printer_set_accepts_ps (printer, TRUE);
  gtk_printer_set_is_active (printer, TRUE);
  gtk_printer_set_has_details (printer, FALSE);

  if (g_strcmp0 (p->state, "NA") == 0)
    {
      gtk_printer_set_is_accepting_jobs (printer, TRUE);
      gtk_printer_set_is_paused (printer, FALSE);
      gtk_printer_set_state_message (printer, "");
    }
  else
    {
      gtk_printer_set_is_accepting_jobs (printer, is_accepting_jobs (p));
      gtk_printer_set_is_paused (printer, g_strcmp0 (get_state (p), "stopped") == 0);
      set_state_message (printer, p);
    }

  gtk_print_backend_add_printer (backend, printer);

  g_object_unref (printer);
}

/*
 * Removes given printer from given GtkPrintBackend
 */
static void
cpdb_remove_gtk_printer (GtkPrintBackend *backend, PrinterObj *p)
{
  GtkPrinter *printer = gtk_print_backend_find_printer (backend, p->name);

  gtk_print_backend_remove_printer (backend, printer);
  g_signal_emit_by_name (backend, "printer-removed", printer);
  g_signal_emit_by_name (backend, "printer-list-changed");
}

/*
 * Sets printer status
 */
static void
set_state_message(GtkPrinter *printer, PrinterObj *p)
{
  gboolean stopped = g_strcmp0 (get_state (p), "stopped") == 0;
  gboolean accepting_jobs = is_accepting_jobs (p);

  if (stopped && !accepting_jobs)
      gtk_printer_set_state_message (printer, "Paused; Rejecting Jobs");
  else if (stopped && accepting_jobs)
    gtk_printer_set_state_message (printer, "Paused");
  else if (!accepting_jobs)
    gtk_printer_set_state_message (printer, "Rejecting Jobs");
  else
    gtk_printer_set_state_message (printer, "");
}

/*
 * Fills a gtk option from cpdb option
 */
static void
cpdb_fill_gtk_option (GtkPrinterOption *gtk_option,
                      Option *cpdb_option,
                      PrinterObj *p)
{

  gtk_printer_option_allocate_choices (gtk_option, cpdb_option->num_supported);
  for (int i = 0; i < cpdb_option->num_supported; i++)
    {
      gtk_option->choices[i] = g_strdup (cpdb_option->supported_values[i]);
      gchar *display_name = get_human_readable_choice_name (p,
                                                            (char *)cpdb_option->option_name,
                                                            cpdb_option->supported_values[i]);

      gtk_option->choices_display[i] = g_strdup (display_name);
    }

  if (g_strcmp0 (cpdb_option->default_value, "NA") != 0)
    {
      if (g_strcmp0 (cpdb_option->option_name, "job-sheets") == 0)
        {
          char **default_val = g_strsplit (cpdb_option->default_value, ",", 2);

          if (g_strcmp0 (gtk_option->name, "gtk-cover-before") == 0)
            gtk_printer_option_set (gtk_option, default_val[0]);
          else if (g_strcmp0 (gtk_option->name, "gtk-cover-after") == 0)
            gtk_printer_option_set (gtk_option, default_val[1]);
        }

      else gtk_printer_option_set (gtk_option, g_strdup (cpdb_option->default_value));
     }

}

/*
 * Convert localtime to utctime
 */
static char *
localtime_to_utctime (const char *local_time)
{
  const char *formats_0[] = {" %I : %M : %S %p ", " %p %I : %M : %S ",
                             " %H : %M : %S ",
                             " %I : %M %p ", " %p %I : %M ",
                             " %H : %M ",
                             " %I %p ", " %p %I "};
  const char *formats_1[] = {" %H : %M : %S ", " %H : %M "};
  const char *end = NULL;
  struct tm  *actual_local_time;
  struct tm  *actual_utc_time;
  struct tm   local_print_time;
  struct tm   utc_print_time;
  struct tm   diff_time;
  char       *utc_time = NULL;
  int         i, n;

  if (local_time == NULL || local_time[0] == '\0')
    return NULL;

  n = supports_am_pm () ? G_N_ELEMENTS (formats_0) : G_N_ELEMENTS (formats_1);

  for (i = 0; i < n; i++)
    {
      local_print_time.tm_hour = 0;
      local_print_time.tm_min  = 0;
      local_print_time.tm_sec  = 0;

      if (supports_am_pm ())
        end = strptime (local_time, formats_0[i], &local_print_time);
      else
        end = strptime (local_time, formats_1[i], &local_print_time);

      if (end != NULL && end[0] == '\0')
        break;
    }

  if (end != NULL && end[0] == '\0')
    {
      time_t rawtime;
      time (&rawtime);

      actual_utc_time = g_memdup2 (gmtime (&rawtime), sizeof (struct tm));
      actual_local_time = g_memdup2 (localtime (&rawtime), sizeof (struct tm));

      diff_time.tm_hour = actual_utc_time->tm_hour - actual_local_time->tm_hour;
      diff_time.tm_min  = actual_utc_time->tm_min  - actual_local_time->tm_min;
      diff_time.tm_sec  = actual_utc_time->tm_sec  - actual_local_time->tm_sec;

      utc_print_time.tm_hour = ((local_print_time.tm_hour + diff_time.tm_hour) + 24) % 24;
      utc_print_time.tm_min  = ((local_print_time.tm_min  + diff_time.tm_min)  + 60) % 60;
      utc_print_time.tm_sec  = ((local_print_time.tm_sec  + diff_time.tm_sec)  + 60) % 60;

      utc_time = g_strdup_printf ("%02d:%02d:%02d",
                                  utc_print_time.tm_hour,
                                  utc_print_time.tm_min,
                                  utc_print_time.tm_sec);
    }

  return utc_time;
}

static gboolean
supports_am_pm (void)
{
  struct tm tmp_tm = { 0 };
  char   time[8];
  int    length;

  length = strftime (time, sizeof (time), "%p", &tmp_tm);

  return length != 0;
}


/*
 * Generate a random string of "size" length
 */
static char *
random_string(int size)
{
  const char charset[] =  "abcdefghijklmnopqrstuvwxyz"
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "0123456789";

  char *str = g_malloc(size+1);
  getrandom(str, size, 0);
  for (int i=0; i<size; i++)
    {
      int rand = str[i] + 128;
      int idx = rand % ((int) sizeof charset);
      str[i] = charset[idx];
    }
  str[size] = '\0';

  return str;
}
