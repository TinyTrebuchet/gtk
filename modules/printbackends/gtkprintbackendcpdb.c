
#include <config.h>
#include <glib/gi18n-lib.h>

#include "gtkprintbackendcpdb.h"

#include <cairo.h>
#include <cairo-ps.h>

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
  gpointer user_data;
  GDestroyNotify dnotify;
  
  GIOChannel *in;
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
  GtkPrintBackendClass *backend_class = GTK_PRINT_BACKEND_CLASS (klass);

  // For calling parent's dispose, finalize methods (to be implemented later)
  backend_parent_class = g_type_class_peek_parent (klass);

  backend_class->request_printer_list = cpdb_request_printer_list;
  backend_class->printer_get_capabilities = cpdb_printer_get_capabilities;
  backend_class->printer_get_options = cpdb_printer_get_options;
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

  gtkPrintBackend = GTK_PRINT_BACKEND (cpdb_backend);

  g_print ("Connecting to DBUS\n");
  connect_to_dbus (cpdb_backend->f); // TODO: add disconnect_from_dbus?
  
  //TODO: fix bug, cancelling print dialog and reopening doesn't get printers
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
 * This function is responsible for specifying which features the print dialog should offer.
 */
static GtkPrintCapabilities
cpdb_printer_get_capabilities (GtkPrinter *printer)
{
  GtkPrintCapabilities capabilities = 0;
  Option *cpdb_option;
  GtkPrinterCpdb *cpdb_printer = GTK_PRINTER_CPDB (printer);


  cpdb_option = get_Option (cpdb_printer, (gchar *) "page-set");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_PAGE_SET;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "copies");
  if (cpdb_option != NULL && g_strcmp0 (cpdb_option->supported_values, "1-1") != 0)
  {
    capabilities |= GTK_PRINT_CAPABILITY_COPIES;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "multiple-document-handling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_COLLATE;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "page-delivery");
  if (cpdb_option != NULL && cpdb_option->num_supported > 1)
  {
    capabilities |= GTK_PRINT_CAPABILITY_REVERSE;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "print-scaling");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
  {
    capabilities |= GTK_PRINT_CAPABILITY_SCALE;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "number-up");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
  {
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP;
  }

  cpdb_option = get_Option (cpdb_printer, (gchar *) "number-up-layout");
  if (cpdb_option != NULL && cpdb_option->num_supported > 0)
  {
    capabilities |= GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT;
  }

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

  //TODO: retrieve options from printer instead of hardcode

  GtkPrinterCpdb *cpdb_printer;
  PrinterObj *p;
  Option *cpdb_option;
  GtkPrinterOption *gtk_option;
  GtkPrinterOptionSet *gtk_option_set = gtk_printer_option_set_new();

  cpdb_printer = GTK_PRINTER_CPDB (printer);
  g_print ("Requesting printer object to retrieve options\n");
  p = gtk_printer_cpdb_get_pObj (cpdb_printer);
  if (p == NULL)
  {
    g_print ("Error: PrintObj not found!\n");
    //TODO: implement it properly
  }

  //TODO: default_value may not be in supported_values, handle that



  if (capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP) 
  {
    g_print ("Retrieving number-up option\n");
    cpdb_option = get_Option (p, (gchar *) "number-up");
    gtk_option = gtk_printer_option_new ("gtk-n-up", C_("printer option", "Pages per Sheet"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  if (capabilities & GTK_PRINT_CAPABILITY_NUMBER_UP_LAYOUT)
  {
    g_print ("Retrieving number-up-layout option\n");
    cpdb_option = get_Option (p, (gchar *) "number-up-layout");
    gtk_option = gtk_printer_option_new ("gtk-n-up-layout", C_("printer option", "Page Ordering"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  g_print ("Retrieving sides option\n");
  cpdb_option = get_Option (p, (gchar *) "sides");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-duplex", _("Duplex Printing"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  g_print ("Retrieving job-priority option\n");
  cpdb_option = get_Option (p, (gchar *) "job-priority");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-job-prio", _("Job Priority"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  g_print ("Retrieving job-sheets option\n");
  cpdb_option = get_Option (p, (gchar *) "job-sheets");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-cover-before", C_("printer option", "Before"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
    gtk_option = gtk_printer_option_new ("gtk-cover-before", C_("printer option", "After"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  g_print ("Retrieving printer-resolution option\n");
  cpdb_option = get_Option (p, (gchar *) "printer-resolution");
  if (cpdb_option != NULL)
  {
    gtk_option = gtk_printer_option_new ("gtk-resolution", _("Resolution"), GTK_PRINTER_OPTION_TYPE_PICKONE);
    cpdb_fill_gtk_option (gtk_option, cpdb_option);
    cpdb_option->group = g_strdup ("ImageQualityPage");
    gtk_printer_option_set_add (gtk_option_set, gtk_option);
    g_object_unref (gtk_option);
  }

  //TODO: Implement rest of functions.


  return gtk_option_set;
}


void func (GtkPrinterOption *option, gpointer user_data)
{
	printf("Name: %s\n",  option->display_text);
	printf("Value: %s\n", option->value);
}
	


/*
 * gtkprinter.h
 */
static void
cpdb_printer_get_settings_from_options (GtkPrinter *printer,
										                    GtkPrinterOptionSet *options,
										                    GtkPrintSettings *settings)
{
	GtkPrinterOption *option;
	
	printf("Getting printer settings from options\n");
	gtk_printer_option_set_foreach(options, func, NULL);
	

	option = gtk_printer_option_set_lookup(options, "gtk-n-up");
	if (option)
		gtk_print_settings_set ( settings, GTK_PRINT_SETTINGS_NUMBER_UP, option->value );

	option = gtk_printer_option_set_lookup( options, "gtk-n-up-layout");
	if (option)
		gtk_print_settings_set( settings, GTK_PRINT_SETTINGS_NUMBER_UP_LAYOUT, option->value);
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

  surface = cairo_ps_surface_create_for_stream (_cairo_write, cache_io, width, height);

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

  gtk_print_job_set_page_ranges (print_job, ranges, n_ranges);
  gtk_print_job_set_collate (print_job, gtk_print_settings_get_collate (settings));
  gtk_print_job_set_reverse (print_job, gtk_print_settings_get_reverse (settings));
  gtk_print_job_set_num_copies (print_job, gtk_print_settings_get_n_copies (settings));
  gtk_print_job_set_n_up (print_job, gtk_print_settings_get_number_up (settings));
  gtk_print_job_set_n_up_layout (print_job, gtk_print_settings_get_number_up_layout (settings));

  scale = gtk_print_settings_get_scale (settings);
  if (scale != 100.0)
    gtk_print_job_set_scale (print_job, scale / 100.0);

  gtk_print_job_set_page_set (print_job, gtk_print_settings_get_page_set (settings));
  gtk_print_job_set_rotate (print_job, TRUE);
}


static void 
cpdb_print_cb  (GtkPrintBackendCpdb *cpdb_backend, 
                GError *error, 
                gpointer user_data)
{
  _PrintStreamData *ps = (_PrintStreamData *) user_data;
 
  printf("Inside cpdb_print_cb\n");

  if (ps->in != NULL)
    g_io_channel_unref (ps->in);

  if (ps->callback)
    ps->callback (ps->job, ps->user_data, error);

  if (ps->dnotify)
    ps->dnotify (ps->user_data);

  gtk_print_job_set_status (ps->job,
                            error ? GTK_PRINT_STATUS_FINISHED_ABORTED
                                  : GTK_PRINT_STATUS_FINISHED);

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

      g_io_channel_write_chars (ps->in,
                                buf,
                                bytes_read,
                                &bytes_written,
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
  GError *print_error = NULL;
  _PrintStreamData *ps;
  //GtkPrintSettings *settings;
  int argc;
  int in_fd;
  char **argv = NULL;
  const char *cmd_line;

  printf("Generating print stream\n");
  
  cmd_line = "dd of=/tmp/test";
  
  //settings = gtk_print_job_get_settings (job);
  
  ps = g_new0 (_PrintStreamData, 1);
  ps->callback = callback;
  ps->user_data = user_data;
  ps->dnotify = dnotify;
  ps->job = g_object_ref(job);
  
  if (!g_shell_parse_argv (cmd_line, &argc, &argv, &print_error))
    goto out;
  
  if (!g_spawn_async_with_pipes  (NULL,
                                  argv,
                                  NULL,
                                  G_SPAWN_SEARCH_PATH,
                                  NULL,
                                  NULL,
                                  NULL,
                                  &in_fd,
                                  NULL,
                                  NULL,
                                  &print_error))
    goto out;
    
  ps->in = g_io_channel_unix_new (in_fd);
    
  g_io_channel_set_encoding (ps->in, NULL, &print_error);
  if (print_error != NULL)
    {
      if (ps->in != NULL)
        g_io_channel_unref (ps->in);
        
      goto out;
    }
      
  g_io_channel_set_close_on_unref (ps->in, TRUE);
    
  g_io_add_watch (data_io,
                  G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
                  (GIOFunc) cpdb_write,
                  ps);
    
  out:
    if (argv != NULL)
      g_strfreev (argv);
      
    if (print_error != NULL)
      {
        cpdb_print_cb (GTK_PRINT_BACKEND_CPDB (backend), print_error, ps);
        g_error_free (print_error);
      }
  
    // TODO: randomize file name
    // TODO: print file using cpdb libs, will do it at last when everything is completed :>
}


static void
add_printer_callback (PrinterObj *p)
{
  g_message("Found Printer %s : %s!\n", p->name, p->backend_name);

  cpdb_add_gtk_printer (p, gtkPrintBackend);
}

static void
remove_printer_callback (PrinterObj *p)
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
                     Option *cpdb_option)
{
  gtk_printer_option_choices_from_array (gtk_option, cpdb_option->num_supported, (const gchar **)cpdb_option->supported_values, (const gchar **)cpdb_option->supported_values);
  if (g_strcmp0 (cpdb_option->default_value, "NA") != 0)
  {
    gtk_printer_option_set (gtk_option, cpdb_option->default_value);
  }    
}
