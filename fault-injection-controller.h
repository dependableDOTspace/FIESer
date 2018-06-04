/*
 * fault-injection-controller.h
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 17.08.2014
 *      Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#ifndef FAULT_INJECTION_CONTROLLER_H_
#define FAULT_INJECTION_CONTROLLER_H_

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "cpu.h"
#include "exec/exec-all.h"

#include "fault-injection-infrastructure.h"

/**
 * see corresponding c-file for documentation
 */
extern void FIESER_hook(CPUArchState *env, hwaddr *addr,
        uint32_t *value, InjectionMode injection_mode,
        AccessType access_type);
extern int64_t FIESER_timer_get(void);
extern int64_t FIESER_normalize_time_to_int64(const char* val, int* success);
extern void FIESER_timer_init(void);
extern void FIESER_helper_init_ops_on_cell(int size);
extern void FIESER_helper_destroy_ops_on_cell(void);
extern int FIESER_helper_ends_with(const char *string, const char *ending);
extern int FIESER_timer_to_int(const char *string);

extern void FIESER_timed_terminate_check(CPUArchState *env);
extern void FIESER_init(void);
extern void FIESER_setMonitor(Monitor *mon);

//void fault_reload_arg();

#endif /* FAULT_INJECTION_CONTROLLER_H_ */
