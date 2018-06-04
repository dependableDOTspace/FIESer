/*
 * fault-injection.c
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 05.08.2014
 *      Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "fault-injection-infrastructure.h"
#include "fault-injection-collector.h"

/**
 * Defines the name and path of the file, where the data collector writes
 * his information to.
 */
#define DATA_COLLECTOR_FILENAME "fies.log"

/**
 * The file, where the data collector writes
 * his information to.
 */
FILE *data_collector;

/**
 * A flag, which defines if the fault-collector should write his content
 * to the specified file or not.
 */
static int do_fault_injection = 0;

/**
 * Sets the flag, which decides if the collector should write
 * his content to the specified file or not. This flag is set in
 * the main-function if the argument-vector (argv) contains
 * the paramter "-fi".
 *
 * @param[in] flag - the value, which should be written to
 *                             do_fault_injection flag.
 */
void set_do_fault_injection(int flag)
{
    do_fault_injection = flag;
    //error_report("Set do fault injection");
}

/**
 * Get the flag, which decides if the collector should write
 * his content to the specified file or not. This flag is set in
 * the main-function if the argument-vector (argv) contains
 * the paramter "-fi".
 *
 * @param[out] - the value, of the do_fault_injection flag.
 */
int get_do_fault_injection(void)
{
    return do_fault_injection;
}
