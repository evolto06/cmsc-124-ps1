/*
 * main.c: Complete program entry point.
 *
 * The program uses the laboratory run contract. Codes 0, 65, and 70 report
 * success, validation failure, and execution failure, respectively.
 */

#include "driver.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: run <case-file>\n");
        return 65;
    }
    return dt_driver_run(argv[1]);
}
