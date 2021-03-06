/*
 * fault-injection-controller.c
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 17.08.2014
 *     Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "fault-injection-infrastructure.h"
#include "fault-injection-controller.h"
#include "fault-injection-library.h"
#include "fault-injection-injector.h"
#include "fault-injection-data-analyzer.h"
#include "fault-injection-config.h"
#include "fault-injection-profiler.h"

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/timer.h"
#include "include/monitor/monitor.h"
#include "hmp.h"

//#define DEBUG_FAULT_INJECTION

/**
 * State of the different CPUs
 */
static CPUState *next_cpu;
int shutting_down = false;
    
static Monitor *qemu_serial_monitor;
//unsigned int sbst_cycle_count_address = 0;
//unsigned int fault_counter_address = 0;
unsigned int file_input_to_use = 0;
unsigned int file_input_to_use_address = 0;
char *fault_library_name;

/**
 * Maybe useless
 */
static hwaddr address_in_use = UINT64_MAX;

/**
 * The timer value, which controls the time-triggered
 * fault injection experiments.
 */
static int64_t timer_value = 0;

/**
 * Array, which stores the previous
 * memory cell operations for
 * dynamic faults.
 */
static int **ops_on_memory_cell;

/**
 * Array, which stores the previous
 * register cell operations for
 * dynamic faults.
 */
static int **ops_on_register_cell;

/**
 * Declares the different types of previous
 * cell operations for dynamic faults.
 */
typedef enum
{
    OPs_0w0,
    OPs_0w1,
    OPs_1w0,
    OPs_1w1,
} CellOps;

/**
 * Allocates and initializes the ops_on_memory_cell- and
 * ops_on_register_cell-array.
 *
 * @param[in] ids - the maximal id number.
 */
void FIESER_helper_init_ops_on_cell(int ids)
{
    int i = 0, j = 0;

    ops_on_memory_cell = malloc(ids * sizeof (int *));
    ops_on_register_cell = malloc(ids * sizeof (int *));

    for (i = 0; i < ids; i++)
    {
        ops_on_memory_cell[i] = malloc(MEMORY_WIDTH * sizeof (int *));
        ops_on_register_cell[i] = malloc(MEMORY_WIDTH * sizeof (int *));
    }

    for (i = 0; i < ids; i++)
    {
        for (j = 0; j < MEMORY_WIDTH; j++)
        {
            ops_on_memory_cell[i][j] = -1;
            ops_on_register_cell[i][j] = -1;
        }
    }

}

/**
 * Deletes the ops_on_memory_cell- and
 * ops_on_register_cell-array.
 */
void FIESER_helper_destroy_ops_on_cell(void)
{
    int i = 0;

    for (i = 0; i < getMaxIDInFaultList(); i++)
    {
        free(ops_on_memory_cell[i]);
        free(ops_on_register_cell[i]);
    }

    free(ops_on_memory_cell);
    free(ops_on_register_cell);
}

/**
 * Reads the content of a specified register.
 *
 * @param[in] env - the information of the CPU-state.
 * @param[in] regno - the register address
 * @param[out] - the content of  the specified register.
 */
static uint32_t FIESER_helper_read_cpu_register(CPUArchState *env, hwaddr regno)
{
#if defined(TARGET_ARM)
    return ((CPUARMState *) env)->regs[(int) regno];
#else
#error unsupported target CPU
#endif
}

/**
 * Compares the ending of a string with a given ending.
 *
 * @param[in] string - the whole string
 * @param[in] ending - the string containing the postfix
 * @param[out] - 1 if the string contains the  given ending, 0 otherwise
 */
int FIESER_helper_ends_with(const char *string, const char *ending)
{
    int string_len = strlen(string);
    int ending_len = strlen(ending);

    if (ending_len > string_len)
        return 0;

    return !strcmp(&string[string_len - ending_len], ending);
}

/**
 * Extracts the ending of the given string and converts
 * the result to an interger value.
 *
 * @param[in] string - the given string
 * @param[out] - the timer value as integer
 */
int FIESER_timer_to_int(const char *string)
{
    int string_len = strlen(string);
    char timer_string[string_len - 1];

    if (string_len < 3)
        return 0;

    memset(timer_string, '\0', sizeof (timer_string));
    strncpy(timer_string, string, string_len - 2);

    return (int) strtol(timer_string, NULL, 10);
}

/**
 * Normalizes the timer value to a uniform value (ns).
 *
 * @param[in] time - time value prefixed
 */
int64_t FIESER_normalize_time_to_int64(const char* val, int* success)
{
    int64_t time = (int64_t) FIESER_timer_to_int(val);

    if (FIESER_helper_ends_with(val, "MS"))
    {
        time *= SCALE_MS;
    }
    else if (FIESER_helper_ends_with(val, "US"))
    {
        time *= SCALE_US;
    }
    else if (FIESER_helper_ends_with(val, "NS"))
    {
        time *= SCALE_NS;
    }
    else
    {
        *success = FALSE;
        time = 0;
    }

    return time;
}

/**
 * Returns the elapsed time after loading a fault-config file.
 *
 * @param[out] - the elapsed time as int64
 */
int64_t FIESER_timer_get(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer_value;
}

/**
 * Initializes the timer value after loading a fault-config file (new
 * fault injection experiment) to the current virtual time in QEMU.
 */
void FIESER_timer_init(void)
{
    timer_value = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

/**
 * Sets bit-flip faults active for the different triggering-methods, extract the necessary
 * information (e.g. set bits in the fault mask), calls the appropriate functions in the
 * fault-injector module and increments the counter for the single fault types in the
 * analyzer-module.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address or the buffer, where the fault is injected.
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] fi_info - information for performing faults.
 * @param[in] pc - pc-value, when a fault should be triggered for pc-triggered faults
 *                          (could be zero in case of no usage).
 */
static void FIESER_inject_bitflip(CPUArchState *env, hwaddr *addr,
                                  FaultList *fault, FaultInjectionInfo fi_info,
                                  uint32_t pc)
{
    int64_t current_timer_value = 0;
    int mask = fault->params.mask, set_bit = 0;

    fi_info.bit_flip = 1;

    if (fault->trigger == FI_TRGR_PC)
    {
        if (pc == fault->params.address)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_TRANSIENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_INTERMITTENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration
                && (current_timer_value / fault->interval) % 2 == 0)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_PERMANENT)
    {
        /**
         * search the set bits in mask (integer)
         */
        while (mask)
        {
            /**
             * extract least significant bit of 2s complement
             */
            set_bit = mask & -mask;

            /**
             * toggle the bit off
             */
            mask ^= set_bit;

            /**
             * determine the position of the set bit
             */
            fi_info.injected_bit = log2(set_bit);
            do_inject_memory_register(env, addr, fi_info);

            if (fi_info.fault_on_register)
                incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_PERMANENT);
            else
                incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_PERMANENT);
        }
        fault->was_triggered = 1;
    }
    else
    {
        return;
    }
}

/**
 * Sets faults active for the different triggering-methods and increments the
 * counter for the single fault types in the analyzer-module.
 *
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] fault_component - the name of the  fault injection component (cpu, ram or reg).
 * @param[in] pc - pc-value, when a fault should be triggered for pc-triggered faults
 *                          (could be zero in case of no usage).
 */
static void FIESER_check_fault_trigger(FaultList *fault, enum FaultComponent fault_component,
                                       unsigned int pc)
{
    int64_t current_timer_value = 0;

    if (pc == fault->params.address
            && (
            fault->trigger == FI_TRGR_PC
            || (fault->trigger == FI_TRGR_ACCESS && (fault->target == FI_TAGT_INSTRUCTION_DECODER || fault->target == FI_TAGT_INSTRUCTION_EXECUTION))
            ))
    {
        incr_num_injected_faults(fault->id, fault_component, FI_TYPE_TRANSIENT);
        fault->was_triggered = 1;
    }
    else if (fault->type == FI_TYPE_TRANSIENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration)
        {
            incr_num_injected_faults(fault->id, fault_component, FI_TYPE_TRANSIENT);
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_INTERMITTENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration
                && (current_timer_value / fault->interval) % 2 == 0)
        {
            incr_num_injected_faults(fault->id, fault_component, FI_TYPE_TRANSIENT);
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_PERMANENT)
    {
        incr_num_injected_faults(fault->id, fault_component, FI_TYPE_PERMANENT);
        fault->was_triggered = 1;
    }
    else
        return;

}

/**
 * Sets new-value faults active for the different triggering-methods, prepares the necessary
 * information (e.g. copy new-value to bit_value), calls the appropriate functions in the
 * fault-injector module and increments the counter for the single fault types in the
 * analyzer-module.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address or the buffer, where the fault is injected.
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] fi_info - information for performing faults.
 * @param[in] pc - pc-value, when a fault should be triggered for pc-triggered faults
 *                          (could be zero in case of no usage).
 */
static void FIESER_inject_new_value(CPUArchState *env, hwaddr *addr,
                                    FaultList *fault, FaultInjectionInfo fi_info,
                                    uint32_t pc)
{
    int64_t current_timer_value = 0;

    fi_info.bit_flip = 0;
    fi_info.new_value = 1;

    if (fault->trigger == FI_TRGR_PC)
    {
        if (pc == fault->params.address)
        {
            /**
             * copy the new value, which is stored in the mask-variable of
             * the linked list, to the bit_value  variable of the FaultInjectionInfo
             * struct, which will be written by the appropriate function from
             * fault-injector module.
             */
            fi_info.bit_value = fault->params.mask;
            do_inject_memory_register(env, addr, fi_info);

            if (fi_info.fault_on_register)
                incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
            else
                incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);

            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_TRANSIENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration)
        {
            /**
             * copy the new value, which is stored in the mask-variable of
             * the linked list, to the bit_value  variable of the FaultInjectionInfo
             * struct, which will be written by the appropriate function from
             * fault-injector module.
             */
            fi_info.bit_value = fault->params.mask;
            do_inject_memory_register(env, addr, fi_info);

            if (fi_info.fault_on_register)
                incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
            else
                incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);

            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_INTERMITTENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration
                && (current_timer_value / fault->interval) % 2 == 0)
        {
            /**
             * copy the new value, which is stored in the mask-variable of
             * the linked list, to the bit_value  variable of the FaultInjectionInfo
             * struct, which will be written by the appropriate function from
             * fault-injector module.
             */
            fi_info.bit_value = fault->params.mask;
            do_inject_memory_register(env, addr, fi_info);

            if (fi_info.fault_on_register)
                incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
            else
                incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);

            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_PERMANENT)
    {
        /**
         * copy the new value, which is stored in the mask-variable of
         * the linked list, to the bit_value  variable of the FaultInjectionInfo
         * struct, which will be written by the appropriate function from
         * fault-injector module.
         */

        fi_info.bit_value = fault->params.mask;
        do_inject_memory_register(env, addr, fi_info);

        if (fi_info.fault_on_register)
            incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_PERMANENT);
        else
            incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_PERMANENT);

        fault->was_triggered = 1;
    }
    else
    {
        return;
    }
}

/**
 * Sets State Faults (SFs) active for the different triggering-methods, extracts the necessary
 * information (e.g. single, set bits in the mask), calls the appropriate functions in the
 * fault-injector module and increments the counter for the single fault types in the
 * analyzer-module.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address or the buffer, where the fault is injected.
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] fi_info - information for performing faults.
 * @param[in] pc - pc-value, when a fault should be triggered for pc-triggered faults
 *                          (could be zero in case of no usage).
 */
static void FIESER_inject_state_register(CPUArchState *env, hwaddr *addr,
                                         FaultList *fault, FaultInjectionInfo fi_info,
                                         uint32_t pc)
{
    int64_t current_timer_value = 0;
    int mask = fault->params.mask, set_bit = 0;

    fi_info.bit_flip = 0;

    if (fault->trigger == FI_TRGR_PC)
    {
        if (pc == fault->params.address)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);

                /**
                 * copy the information, if a bit should be set or reset, which is stored
                 * in the set_bit-variable of the linked list to the bit-value-variable of
                 * the FaultInjectionInfo struct, if the mask is set at this position.
                 * The double negation (!!) is used to convert an integer to a single
                 * logical value (0 or 1).
                 */
                fi_info.bit_value = !!(fault->params.set_bit & set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_TRANSIENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);

                /**
                 * copy the information, if a bit should be set or reset, which is stored
                 * in the set_bit-variable of the linked list to the bit-value-variable of
                 * the FaultInjectionInfo struct, if the mask is set at this position.
                 * The double negation (!!) is used to convert an integer to a single
                 * logical value (0 or 1).
                 */
                fi_info.bit_value = !!(fault->params.set_bit & set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_INTERMITTENT)
    {
        current_timer_value = FIESER_timer_get();

        if (current_timer_value > fault->timer
                && current_timer_value < fault->duration
                && (current_timer_value / fault->interval) % 2 == 0)
        {
            /**
             * search the set bits in mask (integer)
             */
            while (mask)
            {
                /**
                 * extract least significant bit of 2s complement
                 */
                set_bit = mask & -mask;

                /**
                 * toggle the bit off
                 */
                mask ^= set_bit;

                /**
                 * determine the position of the set bit
                 */
                fi_info.injected_bit = log2(set_bit);

                /**
                 * copy the information, if a bit should be set or reset, which is stored
                 * in the set_bit-variable of the linked list to the bit-value-variable of
                 * the FaultInjectionInfo struct, if the mask is set at this position.
                 * The double negation (!!) is used to convert an integer to a single
                 * logical value (0 or 1).
                 */
                fi_info.bit_value = !!(fault->params.set_bit & set_bit);
                do_inject_memory_register(env, addr, fi_info);

                if (fi_info.fault_on_register)
                    incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_TRANSIENT);
                else
                    incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_TRANSIENT);
            }
            fault->was_triggered = 1;
        }
        else
        {
            fault->was_triggered = 0;
        }
    }
    else if (fault->type == FI_TYPE_PERMANENT)
    {
        /* search the set bits in mask (integer) */
        while (mask)
        {
            set_bit = mask & -mask; // extract least significant bit of 2s complement
            mask ^= set_bit; // toggle the bit off

            fi_info.injected_bit = (uint32_t) log2(set_bit); // determine the position of the set bit
            fi_info.bit_value = !!(fault->params.set_bit & set_bit); // determine if bit should be set or reset
            do_inject_memory_register(env, addr, fi_info);

            if (fi_info.fault_on_register)
                incr_num_injected_faults(fault->id, FI_COMP_REGISTER, FI_TYPE_PERMANENT);
            else
                incr_num_injected_faults(fault->id, FI_COMP_RAM, FI_TYPE_PERMANENT);
        }
        fault->was_triggered = 1;
    }
    else
    {
        return;
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a fault in the address decoder of the main memory (RAM) and if the accessed
 * address is defined in the XML-file.
 *
 * @param[in] addr - the address or the buffer, where the fault is injected.
 */
static void FIESER_controller_memory_address(CPUArchState *env, hwaddr *addr)
{
    FaultList *fault;
    int element = 0;
    FaultInjectionInfo fi_info = {0, 0, 0, 0, 0, 0, 0};
    ARMCPU *cpu = arm_env_get_cpu(env);

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);

#if defined(DEBUG_FAULT_CONTROLLER_TLB_FLUSH)
        printf("flushing tlb address %x\n", (int) *addr);
#endif
        tlb_flush_page(CPU(cpu), (target_ulong) * addr);



        /*
         * accessed address is not the defined fault address or the trigger is set to
         * time- or pc-triggering.
         */
        if (fault->params.address != (int) *addr
                || fault->trigger != FI_TRGR_ACCESS)
            continue;

        if (fault->component == FI_COMP_RAM
                && fault->target == FI_TAGT_ADDRESS_DECODER)
        {
            /*
             * set/reset values
             */
            fi_info.access_triggered_content_fault = 1;
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 1;
            fi_info.fault_on_register = 0;

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("memory address before: 0x%08x\n", (uint32_t) * addr);
#endif

            if (fault->mode == FI_MODE_BITFLIP)
                FIESER_inject_bitflip(env, addr, fault, fi_info, 0);
            else if (fault->mode == FI_MODE_NEW_VALUE)
                FIESER_inject_new_value(env, addr, fault, fi_info, 0);
            else if (fault->mode == FI_MODE_STATE_FAULT)
                FIESER_inject_state_register(env, addr, fault, fi_info, 0);

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("memory address after: 0x%08x\n", (uint32_t) * addr);
#endif
        }

        //      tlb_flush_page(env, (target_ulong)fault->params.address);
        //      tlb_flush_page(env, (target_ulong)fault->params.cf_address);
    }
}

/**
 * Stores the previous access-operations of a defined fault memory address. This information
 * is used for deciding, if a dynamic fault should be triggered or not.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] addr - the address of the accessed cell.
 * @param[in] value -  the value or buffer, which should be written to memory.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 */
static void FIESER_helper_log_cell_operations_memory(CPUArchState *env, FaultList *fault, hwaddr *addr,
                                                     uint32_t *value, AccessType access_type)
{
    unsigned memword = 0, mask = 0, set_bit, bit_pos = 0, id = 0;

    /**
     * only a write access can trigger a dynamic fault
     */
    if (access_type == write_access_type)
    {
        uint8_t *membytes = (uint8_t *) & memword;
        CPUState *cpu = ENV_GET_CPU(env);
        cpu_memory_rw_debug(cpu, *addr, membytes, (MEMORY_WIDTH / 8), 0);

        mask = fault->params.mask;

        /**
         * search the set bits in mask (integer)
         */
        while (mask)
        {
            /**
             * extract least significant bit of 2s complement
             */
            set_bit = mask & -mask;

            /**
             * toggle the bit off
             */
            mask ^= set_bit;

            /**
             * determine the position of the set bit
             */
            bit_pos = (uint32_t) log2(set_bit);
            id = fault->id - 1;

            if (!!(memword & set_bit) == 0 && !!(*value & set_bit) == 0)
                ops_on_memory_cell[id][bit_pos] = OPs_0w0;
            else if (!!(memword & set_bit) == 0 && !!(*value & set_bit) == 1)
                ops_on_memory_cell[id][bit_pos] = OPs_0w1;
            else if (!!(memword & set_bit) == 1 && !!(*value & set_bit) == 0)
                ops_on_memory_cell[id][bit_pos] = OPs_1w0;
            else if (!!(memword & set_bit) == 1 && !!(*value & set_bit) == 1)
                ops_on_memory_cell[id][bit_pos] = OPs_1w1;
            else
                ops_on_memory_cell[id][bit_pos] = -1;
        }
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a fault in the memory cells of the main memory (RAM) and if the accessed
 * address is defined in the XML-file.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address of the accessed cell.
 * @param[in] value -  the value or buffer, which should be written to register.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 */
static void FIESER_controller_memory_content(CPUArchState *env, hwaddr *addr,
                                             uint32_t *value, AccessType access_type)
{
    FaultList *fault;
    int element = 0;
    FaultInjectionInfo fi_info = {0, 0, 0, 0, 0, 0, 0};
    ARMCPU *cpu = arm_env_get_cpu(env);

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);


        /*
         * accessed address is not the defined fault address or the trigger is set to
         * time- or pc-triggering.
         */
        if (fault->params.address != (int) *addr
                || fault->trigger != FI_TRGR_ACCESS)
        {
            continue;
        }

#if defined(DEBUG_FAULT_CONTROLLER_TLB_FLUSH)
        printf("flushing tlb address %x\n", (int) *addr);
#endif
        tlb_flush_page(CPU(cpu), (target_ulong) * addr);

        if (fault->component == FI_COMP_RAM
                && (fault->target == FI_TAGT_MEMORY_CELL || fault->target == FI_TAGT_RW_LOGIC))
        {
#if defined(DEBUG_FAULT_CONTROLLER)
            printf("FAULT INJECTED TRIGGERED TO %x with addr %x \n", (int) *addr, fault->params.address);
#endif
            /* set/reset values */
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 0;
            fi_info.access_triggered_content_fault = 1;
            fi_info.fault_on_register = 0;

            FIESER_helper_log_cell_operations_memory(env, fault, addr, value, access_type);

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("-----------------------START-------------------------------\n");
            if (access_type == read_access_type)
                printf("value read from cell before fault injection: 0x%08x\n", *value);
            else if (access_type == write_access_type)
                printf("value to write into cell before fault injection: 0x%08x\n", *value);
#endif

            if (fault->mode == FI_MODE_BITFLIP)
            {
                uint64_t value64 = *value;
                FIESER_inject_bitflip(env, &value64, fault, fi_info, 0);
                *value = value64;
            }
            else if (fault->mode == FI_MODE_NEW_VALUE)
            {
                uint64_t value64 = *value;
                FIESER_inject_new_value(env, &value64, fault, fi_info, 0);
                *value = value64;
            }
            else if (fault->mode == FI_MODE_STATE_FAULT)
            {
                uint64_t value64 = *value;
                FIESER_inject_state_register(env, &value64, fault, fi_info, 0);
                *value = value64;
            }

#if defined(DEBUG_FAULT_CONTROLLER)
            unsigned memword = 0;
            uint8_t *membytes = (uint8_t *) & memword;
            CPUState *cpu = ENV_GET_CPU(env);

            if (fault->params.cf_address != -1)
            {
                cpu_memory_rw_debug(cpu, fault->params.cf_address, membytes, (MEMORY_WIDTH / 8), 0);
                printf("coupled cell content after fault injection: 0x%08x\n", memword);
            }

            if (access_type == read_access_type)
                printf("value read from cell after fault injection: 0x%08x\n", *value);
            else if (access_type == write_access_type)
                printf("value to write into cell after fault injection: 0x%08x\n", *value);

            cpu_memory_rw_debug(cpu, *addr, membytes, (MEMORY_WIDTH / 8), 0);
            printf("cell content after fault injection: 0x%08x\n", memword);
            printf("-----------------------END---------------------------------\n");
#endif
        }
        else
        {
            continue;
        }
        //
        //      tlb_flush_page(env, (target_ulong)fault->params.address);
        //      tlb_flush_page(env, (target_ulong)fault->params.cf_address);
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a fault in the instruction decoder or instruction execution of the CPU
 * and if the accessed address is defined in the XML-file.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the instruction number.
 */
static void FIESER_controller_insn(CPUArchState *env, hwaddr *addr, uint32_t *ins, InjectionMode injection_mode)
{
    FaultList *fault;
    int element = 0;
    uint32_t insn = 0;

    //printf("---------------------------HARTL------------------------------------------\n");
    //printf("instruction number before fault injection: 0x%08x\n", (unsigned int)*addr);

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);

        //printf("---------------------------HARTL2------------------------------------------\n");
        //printf("fault->params.address: 0x%08x\n", (unsigned int)fault->params.address);

        /**
         * accessed address is not the defined fault address or the trigger is set to
         * time- or pc-triggering.
         */
        if (fault->params.address != *addr
                || fault->trigger != FI_TRGR_ACCESS
                || fault->component != FI_COMP_CPU)
            continue;

        //printf("---------------------------HARTL3------------------------------------------\n");

#if defined(DEBUG_FAULT_CONTROLLER)
        printf("---------------------------START------------------------------------------\n");
        printf("instruction number before fault injection: 0x%08x\n", (unsigned int) *addr);
#endif

        if (fault->target == FI_TAGT_INSTRUCTION_DECODER)
        {

            FIESER_check_fault_trigger(fault, FI_COMP_CPU, (unsigned int) *addr);
            if (!fault->was_triggered)
                continue;

            /**
             * different data types sizes - cast will crash the system!
             */
            do_inject_insn(&insn, fault->params.instruction);
            *ins = (uint32_t) insn;

        }
        else if (fault->target == FI_TAGT_INSTRUCTION_EXECUTION)
        {

            FIESER_check_fault_trigger(fault, FI_COMP_CPU, 0);
            if (!fault->was_triggered)
                continue;

            switch (injection_mode)
            {
            case FI_INSTRUCTION_VALUE_ARM:
                /**
                 * No operation (ARM mode) "MOV r8, r8" - 0xe1a08008
                 *  is assembler syntax and defines a
                 * NOP-operation, which simulates a "no execution"
                 */
                do_inject_insn(&insn, 0xe1a08008);
                break;

                /**
                 * No operation (Thumb mode) "MOV r8, r8" - 0x46c0
                 * replaced 32bit instructions need two 16-bit NOPs
                 */
            case FI_INSTRUCTION_VALUE_THUMB32:
                insn |= (0x46c0 << 16);
            case FI_INSTRUCTION_VALUE_THUMB16:
                insn |= 0x46c0;
                do_inject_insn(&insn, insn);
                break;
            default:
                assert(0);
                break;
            }
            *ins = (uint32_t) insn;
        }

#if defined(DEBUG_FAULT_CONTROLLER)
        printf("instruction number after fault injection: 0x%08x\n", (unsigned int) *addr);
        printf("---------------------------END--------------------------------------------\n");
#endif
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a time- or pc-triggered fault and if the accessed address is defined in the XML-file.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the instruction number.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 */
static void FIESER_controller_pc_or_time(CPUArchState *env,
                                         hwaddr *addr,
                                         InjectionMode injection_mode,
                                         int access_type)
{
    FaultList *fault;
    int element = 0;
    unsigned int pc = (unsigned long) *addr;
    hwaddr reg_mem_addr = 0;
    FaultInjectionInfo fi_info = {0, 0, 0, 0, 0, 0, 0};

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);

        /**
         * trigger is not set to time- or pc-triggering.
         */
        if (fault->trigger != FI_TRGR_TIME
                && fault->trigger != FI_TRGR_PC)
            continue;

#if defined(DEBUG_FAULT_CONTROLLER)
        printf("---------------------------START------------------------------------------\n");
        printf("pc before fault injection: 0x%08x\n", (unsigned int) *addr);
#endif

        if (fault->component == FI_COMP_CPU
                && fault->target == FI_TAGT_CONDITION_FLAGS)
        {
            FIESER_check_fault_trigger(fault, FI_COMP_CPU, pc);
            if (!fault->was_triggered)
                continue;

            do_inject_condition_flags(env, fault->mode, fault->params.set_bit);
        }
        else if (fault->component == FI_COMP_CPU
                && (fault->target == FI_TAGT_INSTRUCTION_DECODER || fault->target == FI_TAGT_INSTRUCTION_EXECUTION))
        {
            FIESER_check_fault_trigger(fault, FI_COMP_CPU, pc);
            if (!fault->was_triggered)
            {
                continue;
            }

            /**
             * overwrites the pc directly in the CPUArchState.
             * This is needed, because the pc is not accessed
             * at this time (time- triggering).
             */
            do_inject_look_up_error(env, fault->params.instruction, (injection_mode == FI_PC_THUMB16) ? 2 : 4);
        }
        else if (fault->component == FI_COMP_REGISTER
                && fault->target == FI_TAGT_REGISTER_CELL)
        {
            /**
             * overwrites the value in register or memory directly
             * through CPUArchState. This is needed, because
             * the value is not accessed at this time (time-
             * or pc-triggering).
             */
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 0;
            fi_info.access_triggered_content_fault = 0;
            fi_info.fault_on_register = 1;

            /**
             * accessed memory or register address is stored
             * it the instruction-variable, because the address
             * variable contains the pc-value.
             */
            reg_mem_addr = fault->params.instruction;
#if defined(DEBUG_FAULT_CONTROLLER)
            unsigned memword = 0;
            memword = FIESER_helper_read_cpu_register(env, reg_mem_addr);
            printf("injecting fault on register %d with initial content 0x%08x\n", fault->params.instruction, memword);
#endif

            if (fault->mode == FI_MODE_BITFLIP)
            {
                FIESER_inject_bitflip(env, &reg_mem_addr, fault, fi_info, pc);
            }
            else if (fault->mode == FI_MODE_NEW_VALUE)
            {
                FIESER_inject_new_value(env, &reg_mem_addr, fault, fi_info, pc);
            }
            else if (fault->mode == FI_MODE_STATE_FAULT)
            {
                FIESER_inject_state_register(env, &reg_mem_addr, fault, fi_info, pc);
            }
#if defined(DEBUG_FAULT_CONTROLLER)
            printf("fault status: %d (1-active, 0-inactive)\n", fault->was_triggered);

            memword = FIESER_helper_read_cpu_register(env, reg_mem_addr);
            printf("cell content after fault injection: 0x%08x\n", memword);
#endif
        }
        else if (fault->component == FI_COMP_RAM
                && (fault->target == FI_TAGT_MEMORY_CELL || fault->target == FI_TAGT_RW_LOGIC))
        {
            /**
             * set/reset values
             */
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 0;
            fi_info.access_triggered_content_fault = 0;
            fi_info.fault_on_register = 0;

            reg_mem_addr = fault->params.instruction;
#if defined(DEBUG_FAULT_CONTROLLER)
            unsigned memword = 0;
            uint8_t *membytes = (uint8_t *) & memword;
            CPUState *cpu = ENV_GET_CPU(env);

            cpu_memory_rw_debug(cpu, reg_mem_addr, membytes, (MEMORY_WIDTH / 8), 0);
            printf("injecting fault on memory cell: 0x%08x with initial content 0x%08x\n",
                   fault->params.instruction, memword);
#endif

            if (fault->mode == FI_MODE_BITFLIP)
            {
                FIESER_inject_bitflip(env, &reg_mem_addr, fault, fi_info, pc);
            }
            else if (fault->mode == FI_MODE_NEW_VALUE)
            {
                FIESER_inject_new_value(env, &reg_mem_addr, fault, fi_info, pc);
            }
            else if (fault->mode == FI_MODE_STATE_FAULT)
            {
                FIESER_inject_state_register(env, &reg_mem_addr, fault, fi_info, pc);
            }
#if defined(DEBUG_FAULT_CONTROLLER)
            printf("fault status: %d (1-active, 0-inactive)\n", fault->was_triggered);

            memword = 0;
            membytes = (uint8_t *) & memword;

            cpu_memory_rw_debug(cpu, reg_mem_addr, membytes, (MEMORY_WIDTH / 8), 0);
            printf("cell content after fault injection: 0x%08x\n", memword);
#endif
        }
#if defined(DEBUG_FAULT_CONTROLLER)
        printf("pc after fault injection: 0x%08x\n", (unsigned int) *addr);
        printf("---------------------------END--------------------------------------------\n");
#endif
    }
}

/**
 * Stores the previous access-operations of a defined fault register address. This information
 * is used for deciding, if a dynamic fault should be triggered or not.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] fault - pointer to the linked list entry.
 * @param[in] addr - the address of the accessed cell.
 * @param[in] value -  the value or buffer, which should be written to register.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 */
static void FIESER_helper_log_cell_operations_register(CPUArchState *env, FaultList *fault, hwaddr *addr,
                                                       uint32_t *value, AccessType access_type)
{
    unsigned memword = 0, mask = 0, set_bit, bit_pos = 0, id = 0;

    /**
     * only a write access can trigger a dynamic fault
     */
    if (access_type == write_access_type)
    {
        memword = FIESER_helper_read_cpu_register(env, *addr);
        mask = fault->params.mask;

        /**
         * search the set bits in mask (integer)
         */
        while (mask)
        {
            /**
             * extract least significant bit of 2s complement
             */
            set_bit = mask & -mask;

            /**
             * toggle the bit off
             */
            mask ^= set_bit;

            /**
             * determine the position of the set bit
             */
            bit_pos = (uint32_t) log2(set_bit);
            id = fault->id - 1;

            if (!!(memword & set_bit) == 0 && !!(*value & set_bit) == 0)
                ops_on_register_cell[id][bit_pos] = OPs_0w0;
            else if (!!(memword & set_bit) == 0 && !!(*value & set_bit) == 1)
                ops_on_register_cell[id][bit_pos] = OPs_0w1;
            else if (!!(memword & set_bit) == 1 && !!(*value & set_bit) == 0)
                ops_on_register_cell[id][bit_pos] = OPs_1w0;
            else if (!!(memword & set_bit) == 1 && !!(*value & set_bit) == 1)
                ops_on_register_cell[id][bit_pos] = OPs_1w1;
            else
                ops_on_register_cell[id][bit_pos] = -1;
        }
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a fault in the register cells of a register bank and if the accessed
 * address is defined in the XML-file.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address of the accessed cell.
 * @param[in] value -  the value or buffer, which should be written to register.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 */
static void FIESER_controller_register_content(CPUArchState *env, hwaddr *addr,
                                               uint32_t *value, AccessType access_type)
{
    FaultList *fault;
    int element = 0;
    FaultInjectionInfo fi_info;

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);

        /**
         * accessed address is not the defined fault address or the trigger is set to
         * time- or pc-triggering.
         */
        if (fault->params.address != (int) *addr
                || fault->trigger == FI_TRGR_TIME
                || fault->trigger == FI_TRGR_PC)
        {
            continue;
        }

        if (fault->component == FI_COMP_REGISTER
                && fault->target == FI_TAGT_REGISTER_CELL)
        {
            /**
             *  set/reset values
             */
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 0;
            fi_info.access_triggered_content_fault = 1;
            fi_info.fault_on_register = 1;

            FIESER_helper_log_cell_operations_register(env, fault, addr, value, access_type);

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("-----------------------START-------------------------------\n");
            if (access_type == read_access_type)
                printf("value read from cell before fault injection: 0x%08x\n", *value);
            else if (access_type == write_access_type)
                printf("value to write into cell before fault injection: 0x%08x\n", *value);
#endif

            if (fault->mode == FI_MODE_BITFLIP)
            {
                uint64_t value64 = *value;
                FIESER_inject_bitflip(env, &value64, fault, fi_info, 0);
                *value = value64;
            }
            else if (fault->mode == FI_MODE_NEW_VALUE)
            {
                uint64_t value64 = *value;
                FIESER_inject_new_value(env, &value64, fault, fi_info, 0);
                *value = value64;
            }
            else if (fault->mode == FI_MODE_STATE_FAULT)
            {
                uint64_t value64 = *value;
                FIESER_inject_state_register(env, &value64, fault, fi_info, 0);
                *value = value64;
            }

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("fault status: %d (1-active, 0-inactive)\n", fault->was_triggered);

            unsigned memword = 0;

            if (fault->params.cf_address != -1)
            {
                memword = FIESER_helper_read_cpu_register(env, fault->params.cf_address);
                printf("coupled cell content after fault injection: 0x%08x\n", memword);
            }

            if (access_type == read_access_type)
                printf("value read from cell after fault injection: 0x%08x\n", *value);
            else if (access_type == write_access_type)
                printf("value to write into cell after fault injection: 0x%08x\n", *value);

            memword = FIESER_helper_read_cpu_register(env, *addr);
            printf("cell content after fault injection: 0x%08x\n", memword);
            printf("-----------------------END---------------------------------\n");
#endif
        }
    }
}

/**
 * Iterates through the linked list and checks if an entry or element belongs to
 * a fault in the address decoder of the register banks and if the accessed
 * address is defined in the XML-file.
 *
 * @param[in] addr - the address of the register, where the fault is injected.
 */
static void FIESER_controller_register_address(CPUArchState *env, hwaddr *addr)
{
    FaultList *fault;
    int element = 0;
    FaultInjectionInfo fi_info = {0, 0, 0, 0, 0, 0, 0};

    for (element = 0; element < getNumFaultListElements(); element++)
    {
        fault = getFaultListElement(element);

        /**
         * accessed address is not the defined fault address or the trigger is set to
         * time- or pc-triggering.
         */
        if (fault->params.address != (int) *addr
                || fault->trigger != FI_TRGR_ACCESS)
            continue;

        if (fault->component == FI_COMP_REGISTER
                && fault->target == FI_TAGT_ADDRESS_DECODER)
        {
#if defined(DEBUG_FAULT_CONTROLLER)
            printf("-----------------------START-------------------------------\n");
            printf("register address before fault injection: 0x%08x\n", (unsigned int) *addr);
#endif
            /**
             *  set/reset values
             */
            fi_info.new_value = 0;
            fi_info.bit_flip = 0;
            fi_info.fault_on_address = 1;
            fi_info.fault_on_register = 1;

            if (fault->mode == FI_MODE_BITFLIP)
                FIESER_inject_bitflip(env, addr, fault, fi_info, 0);
            else if (fault->mode == FI_MODE_NEW_VALUE)
                FIESER_inject_new_value(env, addr, fault, fi_info, 0);
            else if (fault->mode == FI_MODE_STATE_FAULT)
                FIESER_inject_state_register(env, addr, fault, fi_info, 0);

#if defined(DEBUG_FAULT_CONTROLLER)
            printf("fault status: %d (1-active, 0-inactive)\n", fault->was_triggered);
            printf("register address before fault injection: 0x%08x\n", (unsigned int) *addr);
            printf("-----------------------END-------------------------------\n");
#endif
        }
    }
}

/**
 * Implements the interface to the appropriate controller functions.
 *
 * @param[in] env - Reference to the information of the CPU state.
 * @param[in] addr - the address of the accessed cell.
 * @param[in] value -  the value or buffer, which should be written to register or memory.
 * @param[in] injection_mode - defines the location, where the function is called from.
 * @param[in] access_type - if the access-operation is a write, read or execute.
 *
 */
void FIESER_hook(CPUArchState *env, hwaddr *addr,
                 uint32_t *value, InjectionMode injection_mode,
                 AccessType access_type)
{
    FaultList *fault;
    int element = 0;
    ARMCPU *cpu = arm_env_get_cpu(env);

    profiler_log(env, addr, value, access_type);

    if (*addr == address_in_use)
        return;

    switch (injection_mode)
    {
    case FI_MEMORY_ADDR:
        FIESER_controller_memory_address(env, addr);
        break;
    case FI_MEMORY_CONTENT:
        if (env)
        {
            FIESER_controller_memory_content(env, addr, value, access_type);
            return;
        }

        /**
         * get the CPUArchState of the current CPU (if not defined)
         */
        if (next_cpu == NULL)
            next_cpu = first_cpu;

        for (; next_cpu != NULL && !(next_cpu->exit_request); next_cpu = CPU_NEXT(next_cpu))
        {
            CPUState *cpu = next_cpu;
            CPUArchState *env = cpu->env_ptr;

            FIESER_controller_memory_content(env, addr, value, access_type);
        }
        break;
    case FI_INSTRUCTION_VALUE_ARM:
    case FI_INSTRUCTION_VALUE_THUMB32:
    case FI_INSTRUCTION_VALUE_THUMB16:
        FIESER_controller_insn(env, addr, value, injection_mode);
        break;
    case FI_REGISTER_ADDR:
        FIESER_controller_register_address(env, addr);
        break;
    case FI_REGISTER_CONTENT:
        FIESER_controller_register_content(env, addr, value, access_type);
        break;
    case FI_TIME:
    case FI_PC_ARM:
    case FI_PC_THUMB32:
    case FI_PC_THUMB16:
        for (element = 0; element < getNumFaultListElements(); element++)
        {
            fault = getFaultListElement(element);

#if defined(DEBUG_FAULT_CONTROLLER_TLB_FLUSH)
            printf("flushing tlb address %x\n", fault->params.address);
#endif
            tlb_flush_page(CPU(cpu), (target_ulong) fault->params.address);
            tlb_flush_page(CPU(cpu), (target_ulong) fault->params.cf_address);
        }

        FIESER_controller_pc_or_time(env, addr, injection_mode, access_type);
        break;
    default:
        fprintf(stderr, "Unknown fault injection target!\n");
        assert(0);
    }
}

void FIESER_timed_terminate_check(CPUArchState *env)
{

    CPUState *cpu;

    //if (sbst_cycle_count_value > SBST_CYCLES_BEFORE_EXIT && !shutting_down)
    if (unlikely(shutting_down))
    {

        hmp_info_faults(qemu_serial_monitor, NULL);

        if (env)
        {
            cpu = ENV_GET_CPU(env);
            cpu_dump_state(cpu, (FILE *) qemu_serial_monitor, (fprintf_function) monitor_printf, CPU_DUMP_FPU);
        }

        qmp_quit(NULL);
    }
}

void FIESER_init(void)
{
    static int already_set = false;

    if (likely(already_set))
        return;

    already_set = true;

    hmp_fault_reload(NULL, NULL);
}
