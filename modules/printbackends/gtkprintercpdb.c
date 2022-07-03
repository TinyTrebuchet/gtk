#include <gtkprintercpdb.h>

static void gtk_printer_cpdb_finalize (GObject *object);

struct _GtkPrinterCpdb
{
  GtkPrinter parent_instance;
  PrinterObj *pObj;
};

G_DEFINE_TYPE (GtkPrinterCpdb, gtk_printer_cpdb, GTK_TYPE_PRINTER)


static void
gtk_printer_cpdb_class_init (GtkPrinterCpdbClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gtk_printer_cpdb_finalize;
}

static void
gtk_printer_cpdb_init (GtkPrinterCpdb *self)
{
}

static void
gtk_printer_cpdb_finalize (GObject *object)
{
  printf ("Finalizing CPDB printer");

  GtkPrinterCpdb *printer_cpdb = GTK_PRINTER_CPDB (object);
  GObjectClass *backend_parent_class = gtk_printer_cpdb_parent_class;

  if (printer_cpdb->pObj != NULL)
    g_free (printer_cpdb->pObj);

  backend_parent_class->finalize (object);
}


PrinterObj *
gtk_printer_cpdb_get_pObj (GtkPrinterCpdb *cpdb_printer)
{
  return cpdb_printer->pObj;
}

void
gtk_printer_cpdb_set_pObj (GtkPrinterCpdb *cpdb_printer,
                           PrinterObj *p)
{
  cpdb_printer->pObj = p;
}
