/* 
 * File:   fault-injection-infrastructure.h
 * Author: Christian M. Fuchs <christian@dependable.space>
 *
 * Created on January 2, 2018, 1:02 AM
 * 
 * Common defines and utilities needed by FIESer such as:
 * * common includes
 * * fault definitions
 * * parameter emums
 * * enum2string conversion
 * * ...
 * 
 * Some originally resided in fault-injection-library.h and fault-injection-controller.h
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#ifndef FAULT_INJECTION_INFRASTRUCTURE_H
#define FAULT_INJECTION_INFRASTRUCTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * enums are all defined in below file.
 * 
 * If you change this file, please also update fault-injection-enums2string.h
 * 
 * not really needed by makes structure nicer with the 2 string helpers
 */
#include "fault-injection-enums.h"
    
/**
 * The declaration of the linked list, which contains
 *  the fault parameters
 */
struct parameters {
    /**
     * The address, where a fault should been injected.
     * It could be a memory, register or instruction address.
     * In case of a pc-triggered fault, this variables
     * stores the pc-value, at which a fault should be
     * injected.
     */
    int address;
    int address_defined;

    /**
     * The coupling address, defines the second cell,
     * which is involved (aggressor or victim cell).
     * It could be a memory or register address.
     * It should been only defined if the defined
     * fault mode is a kind of Coupling Fault (CFxx).
     */
    int cf_address;
    int cf_address_defined;

    /**
     * The mask contains the position where a fault
     * should been injected at a specified address or
     * content (e.g. a mask = 0x2 defines that only
     * the second bit should be modified.
     * In case, that the  fault mode is "NEW VALUE",
     * the mask contains the new value, which should
     * be  written to a specified target.
     */
    int mask;
    int mask_defined;

    /**
     * The instruction contains the instruction number
     * which should be replaced in a CPU decoding or
     * execution fault. If the  instruction is defined as
     * 0xDEADBEEF a NOP instruction is injected, to
     * implement a "no execution fault".
     * In the case, that the fault is pc-triggered and
     * the address-variable contains the pc-value,
     * the address for the memory or register cell
     * is defined in the instruction variable.
     */
    int instruction;
    int instruction_defined;

    /**
     * This variable defines, if a specified bit (by mask) is
     * set or rest at that position (is only used for State
     * Faults or Condition Flag Faults).
     */
    int set_bit;
    int set_bit_defined;
};

struct Fault {
    /**
     * Stores the fault id.
     */
    int id;

    /**
     * Defines, the component of a fault. Should be a string containing
     * the keywords CPU, RAM or REGISTER.
     */
    enum FaultComponent component;

    /**
     * Defines, the target of a fault. Should be a string containing
     * the appropriate keywords.
     */
    enum FaultTarget target;

    /**
     * Defines, the fault mode. Should be a string containing
     * the appropriate keywords.
     */
    enum FaultMode mode;
    
    /**
     * Defines, how a  fault should been triggered.
     * Should be a string containing the keyword access,
     * pc or time.
     */
    enum FaultTrigger trigger;

    /**
     * The fault type for access- or time-triggered faults.
     * Should be a string containing the keyword transient,
     * permanent or intermittend.
     */
    enum FaultType type;
    
    /**
     * The time, where the fault should been active.
     * Should be a positive, real number containing a
     * time period in ms, us or ns.
     */
    int64_t timer;

    /**
     * The duration of transient or intermittend faults.
     * Should be a positive, real number containing a
     * time period in ms, us or ns.
     */
    int64_t duration;

    /**
     * The interval of intermittend faults. Should be  a
     * positive, real number containing a time period in ms,
     * us or ns.
     */
    int64_t interval;

    /**
     * struct, which contains important parameters
     */
    struct parameters params;

    /**
     * Visualizes if a fault was triggered (set) or not (reset)
     */
    int was_triggered;
    
    /**
     * Pointer to the next entry in the linked list.
     */
    struct Fault *next;
};

typedef struct Fault FaultList;

#ifdef __cplusplus
}
#endif

#endif /* FAULT_INJECTION_INFRASTRUCTURE_H */

