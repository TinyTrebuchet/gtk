
#include <config.h>
#include <glib/gi18n-lib.h>

#include "gtkprintbackendcpdb.h"

#include <cairo.h>
#include <cairo-pdf.h>

#define GTK_PRINT_BACKEND_CPDB_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))
#define GTK_IS_PRINT_BACKEND_CPDB_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PRINT_BACKEND_CPDB))
#define GTK_PRINT_BACKEND_CPDB_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PRINT_BACKEND_CPDB, GtkPrintBackendCpdbClass))

#define _CPDB_MAX_CHUNK_SIZE 8192

struct _GtkPrintBackendCpdbClass
{
  GtkPrintBackendClass parent_class;
  
};

struct _GtkPrintBackendCpdb
{
  GtkPrintBackend parent_instance;
  FrontendObj *f;
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


// created for add_printer_callback and remove_printer_callback
static GtkPrintBackend *gtkPrintBackend;

static GObjectClass *backend_parent_class;


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

/**
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

  // TODO: add counter for class instances using global hash table, for multiple frontends

  backend_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gtk_print_backend_cpdb_finalize;

  backend_class->request_printer_list = cpdb_request_printer_list;
  backend_class->printer_get_capabilities = cpdb_printer_get_capabilities;
  backend_class->printer_get_options = cpdb_printer_get_options;
  backend_class->printer_list_papers = cpdb_printer_list_papers;
  backend_class->printer_get_settings_from_options = cpdb_printer_get_settings_from_options;
  backend_class->printer_prepare_for_print = cpdb_printer_prepare_for_print;
  backend_class->printer_create_cairo_surface = cpdb_printer_create_cairo_surface;
  backend_class->print_stream = cpdb_print_stream;
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
  g_print ("Initialzing CPDB backend object\n");

  g_print ("Creating frontendObj for CPDB backend\n");
  cpdb_backend->f = get_new_FrontendObj  (NULL,
                                         (event_callback) add_printer_callback,
                                         (event_callback) remove_printer_callback);

  ignore_last_saved_settings(cpdb_backend->f);

  gtkPrintBackend = GTK_PRINT_BACKEND (cpdb_backend);

  g_print ("Connecting to DBUS\n");
  connect_to_dbus (cpdb_backend->f); // TODO: add disconnect_from_dbus?
  
  //TODO: fix bug, cancelling print dialog and reopening doesn't get printers
}

static void
gtk_print_backend_cpdb_finalize (GObject *object)
{
  //TODO: backend not being finalized when cpdb is closed
  g_print("Finalizing CPDB backend object\n");

  GtkPrintBackendCpdb *backend_cpdb = GTK_PRINT_BACKEND_CPDB (object);

  disconnect_from_dbus(backend_cpdb->f);

  backend_parent_class->finalize (object);
}

/*
 * This function is responsible for displaying the printer list obtained from CPDB backend on the print dialog.
 * Currently, this is implemented through refresh_printer_list,
 * which gets a new printer list from backend,
 * and calls add_printer_callback and remove_printer_callback
 * for each printer added or removed in the newly obtained printer list.
 * refresh_printer_list is internally implemented as an async function.
 */
static void
cpdb_request_printer_list (GtkPrintBackend *backend)
{
  g_print ("Reguesting printer list\n");
  GtkPrintBackendCpdb *cpdb_backend = GTK_PRINT_BACKEND_CPDB (backend);
  
  g_print ("Refreshing printer list\n");
  refresh_printer_list (cpdb_backend->f);

  gtk_print_backend_set_list_done (backend);
}


/*
 * This function is responsible for specifying which features the
 * print dialog should offer for the given printer.
 */
static GtkPrintCapabilities
cpdb_printer_get_capabilities (GtkPrinter *printer)
{
  g_print ("Getting print capabilities\n");

  GtkPrintCapabilities capabilities = 0;
  Option *cpdb_option;
  GtkPrinterCpdb *cpdb_printer = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (cpdb_printer);

  cpdb_option = get_Option (p, (gchar *) "page-set");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_PAGE_SET;
  }

  cpdb_option = get_Option (p, (gchar *) "copies");
  if (cpdb_option != NULL && g_strcmp0 (cpdb_option->supported_values[0], "1-1") != 0)
  {
    capabilities |= GTK_PRINT_CAPABILITY_COPIES;
  }

  cpdb_option = get_Option (p, (gchar *) "multiple-document-handling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_COLLATE;
  }

  cpdb_option = get_Option (p, (gchar *) "page-delivery");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_REVERSE;
  }

  cpdb_option = get_Option (p, (gchar *) "print-scaling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_SCALE;
  }

  cpdb_option = get_Option (p, (gchar *) "number-up");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP;
  }

  cpdb_option = get_Option (p, (gchar *) "number-up-layout");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT;
  }

  g_print("Capabilities polled: %d\n", capabilities);

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

  g_print ("Requesting printer options\n");

  GtkPrinterCpdb *printer_cpdb;
  PrinterObj *p;
  Option *cpdb_option;
  GtkPrinterOption *gtk_option;
  GtkPrinterOptionSet *gtk_option_set = gtk_printer_option_set_new();

  printer_cpdb = GTK_PRINTER_CPDB (printer);
  p = gtk_printer_cpdb_get_pObj (printer_cpdb);


  /** Page-Setup **/
  if (capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP) 
  {
    cpdb_option = get_Option (p, (gchar *) "number-up");
    gtk_option = gtk_printer_option_new ("gtk-n-up", "Pages per Sheet", GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  if (capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT)
  {
    cpdb_option = get_Option (p, (gchar *) "number-up-layout");
    gtk_option = gtk_printer_option_new ("gtk-n-up-layout", "Page Ordering", GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "sides");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-duplex", "Duplex Printing", GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "media-source");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-paper-source", "Paper source", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  cpdb_option = get_Option (p, (gchar *) "media-type");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-paper-type", "Paper Type", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  cpdb_option = get_Option (p, (gchar *) "output-bin");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("gtk-output-tray", "Output Tray", GTK_PRINTER_OPTION_TYPE_PICKONE);
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
    gtk_option = gtk_printer_option_new ("gtk-job-prio", "Job Priority", GTK_PRINTER_OPTION_TYPE_PICKONE);
    gtk_printer_option_choices_from_array (gtk_option, G_N_ELEMENTS (prio), prio, prio_display);
    gtk_printer_option_set (gtk_option, "50");
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  cpdb_option = get_Option (p, (gchar *) "job-sheets");
  if (cpdb_option != NULL) {
    gtk_option = gtk_printer_option_new ("gtk-cover-before", "Before", GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);

    gtk_option = gtk_printer_option_new ("gtk-cover-after", "After", GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  gtk_option = gtk_printer_option_new ("gtk-billing-info", "Billing Info", GTK_PRINTER_OPTION_TYPE_STRING);
  gtk_printer_option_set (gtk_option, "");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);

  const char *print_at[] = {"now", "at", "on-hold"};
  gtk_option = gtk_printer_option_new ("gtk-print-time", "Print at", GTK_PRINTER_OPTION_TYPE_PICKONE);
  gtk_printer_option_choices_from_array (gtk_option, G_N_ELEMENTS (print_at), print_at, print_at);
  gtk_printer_option_set (gtk_option, "now");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);

  gtk_option = gtk_printer_option_new ("gtk-print-time-text", "Print at time", GTK_PRINTER_OPTION_TYPE_STRING);
  gtk_printer_option_set (gtk_option, "");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);


  /** Image Quality **/
  cpdb_option = get_Option (p, (gchar *) "printer-resolution");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("printer-resolution", "Resolution", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ImageQualityPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }
  
  cpdb_option = get_Option (p, (gchar *) "print-quality");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
    {
      gtk_option = gtk_printer_option_new ("print-quality", "Print quality", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ImageQualityPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  /** Color **/
  cpdb_option = get_Option (p, (gchar *) "print-color-mode");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("print-color-mode", "Print Color Mode", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("ColorPage");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }
  
  /** Advanced **/
  cpdb_option = get_Option (p, (gchar *) "print-scaling");
  if (cpdb_option != NULL)
    {
      gtk_option = gtk_printer_option_new ("print-scaling", "Print Scaling", GTK_PRINTER_OPTION_TYPE_PICKONE);
      cpdb_fill_gtk_option (gtk_option, cpdb_option, p);
      gtk_option->group = g_strdup ("Advanced");
      gtk_printer_option_set_add (gtk_option_set, gtk_option);
      g_object_unref (gtk_option);
    }

  gtk_option = gtk_printer_option_new ("borderless", "Borderless", GTK_PRINTER_OPTION_TYPE_BOOLEAN);
  gtk_printer_option_allocate_choices (gtk_option, 2);
  // gtk_option->choices[0] = g_strdup("True");
  // gtk_option->choices_display[0] = g_strdup("True");
  // gtk_option->choices[1] = g_strdup("False");
  // gtk_option->choices_display[1] = g_strdup("False");
  gtk_option->group = g_strdup ("Advanced");
  gtk_printer_option_set_add (gtk_option_set, gtk_option);
  g_object_unref (gtk_option);

  return gtk_option_set;
}

static GList *
cpdb_printer_list_papers (GtkPrinter *printer)
{
  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (printer);
  PrinterObj *p = gtk_printer_cpdb_get_pObj (printer_cpdb);
  GList *result = NULL;
  Option *cpdb_option;
  GtkPageSetup *page_setup;
  GtkPaperSize *paper_size;

  cpdb_option = get_Option (p, (gchar *) "media");
  if (cpdb_option != NULL)
    {
      for (int i=0; i<cpdb_option->num_supported; i++)
        {
          // TODO: Add support for custom paper sizes
          if (!g_str_has_prefix (cpdb_option->supported_values[i], "custom_min") || 
              !g_str_has_prefix (cpdb_option->supported_values[i], "custom_max")) 
          {
            page_setup = gtk_page_setup_new ();
            paper_size = gtk_paper_size_new (cpdb_option->supported_values[i]);

            gtk_page_setup_set_paper_size (page_setup, paper_size);
            gtk_paper_size_free (paper_size);

            result = g_list_append (result, page_setup);
          }
        }
    }

  return result;
}


// TODO: remove func
void func (GtkPrinterOption *option, gpointer user_data)
{
	printf("Name: %s\n",  option->display_text);
	printf("Value: %s\n", option->value);
}

static void
gtk_print_backend_cpdb_configure_settings (GtkPrintJob *job)
{
	printf("Configuring print settings\n");

	GtkPrintSettings *settings;
	GtkPrinter *printer;

	settings = gtk_print_job_get_settings (job);
	printer = gtk_print_job_get_printer (job);

	gtk_print_settings_foreach (settings, gtk_printer_cpdb_configure_settings, printer);
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

  printf ("Adding setting: %s -> %s\n", key, value);
	add_setting_to_printer(p, (char *) key, (char *) value);
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
	
	printf("Getting printer settings from options\n");
	gtk_printer_option_set_foreach(options, func, NULL);
	

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
		  char *value = g_strdup_printf ("%s,%s", cover_before->value, cover_after->value);
		  gtk_print_settings_set (settings, "job-sheets", value);
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
cpdb_printer_prepare_for_print (GtkPrinter *printer,
                                GtkPrintJob *print_job,
                                GtkPrintSettings *settings,
                                GtkPageSetup *page_setup)
{
  double scale;
  GtkPrintPages pages;
  GtkPageRange *ranges;
  int n_ranges;

  printf("Preparing for print\n");

  pages = gtk_print_settings_get_print_pages (settings);
  gtk_print_job_set_pages (print_job, pages);

  if (pages == GTK_PRINT_PAGES_RANGES)
    ranges = gtk_print_settings_get_page_ranges (settings, &n_ranges);
  else
    {
      ranges = NULL;
      n_ranges = 0;
    }

  // use page-ranges feature offered by gtk_print_job
  gtk_print_job_set_page_ranges (print_job, ranges, n_ranges);

  // TODO: let cpdb handle scaling
  scale = gtk_print_settings_get_scale (settings);
  if (scale != 100.0)
    gtk_print_job_set_scale (print_job, scale / 100.0);


}


static void 
cpdb_print_cb  (GtkPrintBackendCpdb *cpdb_backend, 
                GError *error, 
                gpointer user_data)
{
  char *uri, *path;

  _PrintStreamData *ps = (_PrintStreamData *) user_data;
  GtkRecentManager *recent_manager;
  GtkPrinterCpdb *printer_cpdb;
  PrinterObj *p;
 
  printf("Inside cpdb_print_cb\n");

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
  uri = g_strdup ("file:///tmp/out.pdf");
  gtk_recent_manager_add_item (recent_manager, uri);
  g_free (uri);

  if (!error) {
    path = g_strdup("/tmp/out.pdf");
    printer_cpdb = GTK_PRINTER_CPDB (gtk_print_job_get_printer (ps->job));
    p = gtk_printer_cpdb_get_pObj (printer_cpdb);
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
  printf ("Writing from data_io\n");

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

  printf("Generating print stream\n");
  
  gtk_print_backend_cpdb_configure_settings (job);
  
  ps = g_new0 (_PrintStreamData, 1);
  ps->callback = callback;
  ps->user_data = user_data;
  ps->dnotify = dnotify;
  ps->job = g_object_ref(job);
  ps->backend = backend;
  
  // TODO: generate proper uri, maybe randomized
  error = NULL;
  uri = g_strdup ("file:///tmp/out.pdf");
  
  if (uri == NULL)
	  goto error;

  file = g_file_new_for_uri (uri);
  ps->target_io_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error);

  g_object_unref (file);
  g_free (uri);

error:
  if (error != NULL)
  {
    printf ("Error: %s\n", error->message);
    cpdb_print_cb (GTK_PRINT_BACKEND_CPDB (backend), error, ps);
    
    g_error_free (error);
    return;
  }

  g_io_add_watch (data_io,
                  G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                  (GIOFunc) cpdb_write,
                  ps);

}

static void
add_printer_callback (FrontendObj *f, PrinterObj *p)
{
  g_message("Found Printer %s : %s!\n", p->name, p->backend_name);

  cpdb_add_gtk_printer (p, gtkPrintBackend);
}

static void
remove_printer_callback (FrontendObj *f, PrinterObj *p)
{
  g_message("Lost Printer %s : %s!\n", p->name, p->backend_name);

  // TODO: free PrinterObj since cpdb-libs doesn't do it
  // TODO: implement cpdb_remove_gtk_printer
}

static void
cpdb_add_gtk_printer (PrinterObj *p, GtkPrintBackend *backend)
{
  GtkPrinterCpdb *cpdb_printer;
  GtkPrinter *printer;

  printf("Adding GtkPrinter\n");

  cpdb_printer = g_object_new (GTK_TYPE_PRINTER_CPDB,
                               "name", p->name,
                               "backend", GTK_PRINT_BACKEND_CPDB (gtkPrintBackend),
                               NULL);
  gtk_printer_cpdb_set_pObj (cpdb_printer, p);

  printer = GTK_PRINTER (cpdb_printer);

  gtk_printer_set_icon_name (printer, "printer");
  gtk_printer_set_state_message (printer, p->state);
  gtk_printer_set_location (printer, p->location);
  gtk_printer_set_description (printer, p->info);
  gtk_printer_set_is_accepting_jobs (printer, p->is_accepting_jobs);
  gtk_printer_set_job_count (printer, get_active_jobs_count(p));
  gtk_printer_set_has_details (printer, TRUE);
  gtk_printer_set_is_active (printer, TRUE);

  gtk_print_backend_add_printer (backend, printer);
  g_object_unref (printer);
}

static void
cpdb_fill_gtk_option (GtkPrinterOption *gtk_option,
                      Option *cpdb_option,
                      PrinterObj *p)
{

  gtk_printer_option_allocate_choices (gtk_option, cpdb_option->num_supported);
  for (int i = 0; i < cpdb_option->num_supported; i++)
  {
    gtk_option->choices[i] = g_strdup (cpdb_option->supported_values[i]);
    gchar *display_name = get_human_readable_choice_name (p, (char *)cpdb_option->option_name, cpdb_option->supported_values[i]);
    gtk_option->choices_display[i] = g_strdup (display_name);
  }

  // TODO: default options, handle for job-sheets also
  if (g_strcmp0 (cpdb_option->default_value, "NA") != 0)
  {
    if (g_strcmp0 (cpdb_option->option_name, "job-sheets") == 0)
    {
      char **default_val = g_strsplit (cpdb_option->default_value, ",", 2);

      if (g_strcmp0 (gtk_option->name, "gtk-cover-before") == 0) gtk_printer_option_set (gtk_option, default_val[0]);
      else if (g_strcmp0 (gtk_option->name, "gtk-cover-after") == 0) gtk_printer_option_set (gtk_option, default_val[1]);
    }

    else gtk_printer_option_set (gtk_option, g_strdup (cpdb_option->default_value));
   }

}

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
