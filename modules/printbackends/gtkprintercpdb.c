#include <gtkprintercpdb.h>

struct _GtkPrinterCpdb
{
  GtkPrinter parent_instance;
  PrinterObj *pObj;
};

G_DEFINE_TYPE (GtkPrinterCpdb, gtk_printer_cpdb, GTK_TYPE_PRINTER)


static void
gtk_printer_cpdb_class_init (GtkPrinterCpdbClass *klass)
{
}

static void
gtk_printer_cpdb_init (GtkPrinterCpdb *self)
{
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
