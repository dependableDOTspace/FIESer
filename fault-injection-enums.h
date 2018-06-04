/* 
 * File:   fault-injection-enums.h
 * Author: Christian M. Fuchs <christian.fuchs@cfuchs.net>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 * 
 * Created on February 3, 2018, 2:42 AM
 */

#ifndef FAULT_INJECTION_ENUMS_H
#define FAULT_INJECTION_ENUMS_H

#ifdef __cplusplus
extern "C" {
#endif

#define FI_UNDEF 0

enum FaultComponent{
    FI_COMP_NONE = 0,
    FI_COMP_CPU,
    FI_COMP_RAM,
    FI_COMP_REGISTER
};

enum FaultTarget{
    FI_TAGT_NONE = 0,
    FI_TAGT_REGISTER_CELL,
    FI_TAGT_CONDITION_FLAGS,
    FI_TAGT_INSTRUCTION_EXECUTION,
    FI_TAGT_INSTRUCTION_DECODER,
    FI_TAGT_MEMORY_CELL,
    FI_TAGT_ADDRESS_DECODER,
    FI_TAGT_RW_LOGIC,
    FI_TAGT_TRACE_MEMORY,
    FI_TAGT_TRACE_REGISTERS,
    FI_TAGT_TRACE_PC,
    FI_TAGT_TRACE_CPSR
};


enum FaultMode{
    FI_MODE_NONE = 0,
    FI_MODE_NEW_VALUE,
    FI_MODE_BITFLIP,
    FI_MODE_STATE_FAULT,
    FI_MODE_COUPLING_FAULT,
    FI_MODE_CPSR_CF,
    FI_MODE_CPSR_VF,
    FI_MODE_CPSR_ZF,
    FI_MODE_CPSR_NF,
    FI_MODE_CPSR_QF
};


enum FaultTrigger{
    FI_TRGR_NONE = 0,
    FI_TRGR_PC,
    FI_TRGR_ACCESS,
    FI_TRGR_TIME
};


enum FaultType{
    FI_TYPE_NONE = 0,
    FI_TYPE_TRANSIENT,
    FI_TYPE_PERMANENT,
    FI_TYPE_INTERMITTENT
};

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


#ifdef __cplusplus
}
#endif

#endif /* FAULT_INJECTION_ENUMS_H */

