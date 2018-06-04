/*
 * fault-injection-collector.h
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 05.08.2014
 *      Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#ifndef FAULT_INJECTION_COLLECTOR_H_
#define FAULT_INJECTION_COLLECTOR_H_

/**
 * The file is opened at the init-function of the QEMU-monitor
 * but is held by the fault-collector.
 */
extern FILE *data_collector;

/**
 * see corresponding c-file for documentation
 */
void set_do_fault_injection(int flag);
int get_do_fault_injection(void);

#endif /* FAULT_INJECTION_COLLECTOR_H_ */
