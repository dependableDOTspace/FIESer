/*
 * fault-injection-controller.h
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 17.08.2014
 *      Author: Gerhard Schoenfelder
 */

#ifndef FAULT_INJECTION_CONTROLLER_H_
#define FAULT_INJECTION_CONTROLLER_H_

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "cpu.h"
#include "exec/exec-all.h"

/**
 * The declaration of the InjectionMode, which specifies,
 * if the controller-function is called from softmmu (for
 * access-triggered memory address or content faults)
 * or from register-access-function (for access-triggered
 * register address or content faults) or from
 * decode-cpu-function (for access-triggered instruction
 * faults) or fromtb_find_fast-function for time-triggered
 * faults.
 */
typedef enum {
    FI_MEMORY_ADDR,
    FI_MEMORY_CONTENT,
    FI_REGISTER_ADDR,
    FI_REGISTER_CONTENT,
    FI_INSTRUCTION_VALUE_ARM,
    FI_INSTRUCTION_VALUE_THUMB32,
    FI_INSTRUCTION_VALUE_THUMB16,
    FI_PC_ARM,
    FI_PC_THUMB32,
    FI_PC_THUMB16,
    FI_TIME
} InjectionMode;

/**
 * The declaration of the AccessType, which specifies
 * a read-, write- or execution-access
 */
typedef enum {
    read_access_type,
    write_access_type,
    exec_access_type,
} AccessType;

/**
 * see corresponding c-file for documentation
 */
void FIESER_hook(CPUArchState *env, hwaddr *addr,
        uint32_t *value, InjectionMode injection_mode,
        AccessType access_type);
int64_t FIESER_timer_get(void);
void FIESER_timer_init(void);
void FIESER_helper_init_ops_on_cell(int size);
void FIESER_helper_destroy_ops_on_cell(void);
int FIESER_helper_ends_with(const char *string, const char *ending);
int FIESER_timer_to_int(const char *string);
void FIESER_setMonitor(Monitor *mon);
void FIESER_start_automatic_test_process(CPUArchState *env);
//void fault_reload_arg();

#endif /* FAULT_INJECTION_CONTROLLER_H_ */
