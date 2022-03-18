#include <glib-object.h>
#include <cpdb-libs-frontend.h>
#include <gtk/gtkprinterprivate.h>

G_BEGIN_DECLS

#define GTK_TYPE_PRINTER_CPDB           (gtk_printer_cpdb_get_type ())
G_DECLARE_FINAL_TYPE                    (GtkPrinterCpdb, gtk_printer_cpdb, GTK, PRINTER_CPDB, GtkPrinter)

PrinterObj *gtk_printer_cpdb_get_pObj   (GtkPrinterCpdb *cpdb_printer);

void gtk_printer_cpdb_set_pObj          (GtkPrinterCpdb *cpdb_printer,
                                         PrinterObj *p);

G_END_DECLS
