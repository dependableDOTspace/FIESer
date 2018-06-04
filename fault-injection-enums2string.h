/* 
 * File:   fault-injection-enums2string.h
 * Author: Christian M. Fuchs <christian.fuchs@cfuchs.net>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 * 
 * Created on February 3, 2018, 2:42 AM
 */

// INTENTIONALLY NO HEADER GUARD
// below declarations/definitions must ONLY be included once before the stringify functions
//#ifndef FAULT_INJECTION_ENUMS2STRING_H
//#define FAULT_INJECTION_ENUMS2STRING_H

#ifdef __cplusplus
extern "C" {
#endif

static const char * FaultComponent_STR[] = {
    "NONE",
    "CPU",
    "RAM",
    "REGISTER"
};
const char * FaultTarget_STR[] = {
    "NONE", 
    "REGISTER CELL", 
    "CONDITION FLAGS", 
    "INSTRUCTION EXECUTION", 
    "INSTRUCTION DECODER", 
    "MEMORY CELL", 
    "ADDRESS DECODER", 
    "RW LOGIC", 
    "TRACE MEMORY", 
    "TRACE REGISTERS",
    "TRACE PC", 
    "TRACE CPSR"
};
const char * FaultMode_STR[] = {
    "NONE", 
    "NEW VALUE", 
    "BITFLIP", 
    "STATE FAULT", 
    "COUPLING FAULT", 
    "CPSR VF", 
    "CPSR ZF", 
    "CPSR NF", 
    "CPSR QF"
};
const char * FaultTrigger_STR[] = {
    "NONE",
    "PC",
    "ACCESS",
    "TIME"
};
const char * FaultType_STR[] = {
    "NONE",
    "TRANSIENT",
    "PERMANENT",
    "INTERMITTENT"
};

#ifdef __cplusplus
}
#endif

//#endif /* FAULT_INJECTION_ENUMS2STRING_H */
// INTENTIONALLY NO HEADER GUARD!
