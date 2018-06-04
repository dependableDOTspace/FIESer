/*
 * fault-injection-library.h
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 07.08.2014
 *      Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#ifndef FAULT_INJECTION_LIBRARY_H_
#define FAULT_INJECTION_LIBRARY_H_

#include "fault-injection-infrastructure.h"
#include "qmp-commands.h"

/*
 * Fault struct definition moved to "fault-injection-infrastructure.h"
 */
/**
 * see corresponding c-file for documentation
 */
const char * FaultComponent2STR(enum FaultComponent which);
const char * FaultTarget2STR(enum FaultTarget which);
const char * FaultMode2STR(enum FaultMode which);
const char * FaultTrigger2STR(enum FaultTrigger which);
const char * FaultType2STR(enum FaultType which);

int getNumFaultListElements(void);
FaultList* getFaultListElement(int element);
void qmp_fault_reload(Monitor *mon, const char *filename, Error **errp);
void delete_fault_list(void);
int getMaxIDInFaultList(void);

#endif /* FAULT_INJECTION_LIBRARY_H_ */
