/*
 * driver.h: Declarations shared by the three driver files.
 * Not part of the assignment. None of your files include this.
 */

#ifndef DT_DRIVER_H
#define DT_DRIVER_H

#include "dt.h"

void dt_print_value(dt_value v);
void dt_print_line(dt_value v);

/* Runs one case file and returns 0, 65, or 70. */
int dt_driver_run(const char *path);

#endif /* DT_DRIVER_H */
