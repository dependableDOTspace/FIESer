/*
 * fault-injection-library.c
 * 
 *  FIESer by Christian M. Fuchs 2017/2018
 * 
 *  Created on: 07.08.2014
 *      Author: Gerhard Schoenfelder
 * 
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "monitor/monitor.h"

#include "fault-injection-infrastructure.h"
#include "fault-injection-library.h"
#include "fault-injection-controller.h"
#include "fault-injection-data-analyzer.h"
#include "fault-injection-profiler.h"

#include <libxml/xmlreader.h>

/**
 * Linked list pointer to the first entry
 */
static FaultList *head = NULL;
/**
 * Linked list pointer to the current entry
 */
static FaultList *curr = NULL;
/**
 * num_list_elements contains the number of the
 * stored entries in the linked list.
 */
static int num_list_elements = 0;

#include "fault-injection-enums2string.h"

const char * FaultComponent2STR(enum FaultComponent which)
{
    return FaultComponent_STR[which];
}

const char * FaultTarget2STR(enum FaultTarget which)
{
    return FaultTarget_STR[which];
}

const char * FaultMode2STR(enum FaultMode which)
{
    return FaultMode_STR[which];
}

const char * FaultTrigger2STR(enum FaultTrigger which)
{
    return FaultTrigger_STR[which];
}

const char * FaultType2STR(enum FaultType which)
{
    return FaultType_STR[which];
}

/**
 * Allocates the size for a new entry in the linked list and parses the elements to it.
 *
 * @param[in] mon - monitor handle for error logging
 * 
 * @param[in] fault - all necessary elements for defining a fault, which are held
 *                              by the struct "Fault"
 * @param[out] ptr - pointer to the linked list entry (added element)
 */
static FaultList* add_to_fault_list(FaultList *fault_to_add)
{

    FaultList *fault = (FaultList*) malloc(sizeof (FaultList));
    if (!fault)
    {
        qemu_log("Node creation failed\n");
        return NULL;
    }

    *fault = *fault_to_add;

    fault->was_triggered = 0;
    fault->next = NULL;

    if (head == NULL)
        head = fault;
    else
        curr->next = fault;

    curr = fault;

    num_list_elements++;

    return curr;
}

#if defined(DEBUG_FAULT_LIST)

/**
 * Prints all entries in the linked list to the standard  out - only for debug purpose.
 */
static void print_fault_list(void)
{
    FaultList *ptr = head;

    printf("\n -------Printing list Start------- \n");
    while (ptr != NULL)
    {
        printf("id [%d] \n", ptr->id);
        printf("component [%s] \n", ptr->component);
        printf("target [%s] \n", ptr->target);
        printf("mode [%s] \n", ptr->mode);
        printf("trigger [%s] \n", ptr->trigger);
        printf("timer [%s] \n", ptr->timer);
        printf("type [%s] \n", ptr->type);
        printf("duration [%s] \n", ptr->duration);
        printf("interval [%s] \n", ptr->interval);
        printf("params.address [%x] \n", ptr->params.address);
        printf("params.cf_address [%x] \n", ptr->params.cf_address);
        printf("params.mask [%x] \n", ptr->params.mask);
        printf("params.instruction [%x] \n", ptr->params.instruction);
        printf("params.set_bit [%x] \n", ptr->params.set_bit);
        printf("is_active [%d] \n", ptr->is_active);
        ptr = ptr->next;
        printf("\n");
    }
    printf("\n -------Printing list End------- \n");

    return;
}
#endif

/**
 * Deletes the linked list and all included elements
 */
void delete_fault_list(void)
{
    FaultList *ptr;

    while ((ptr = head))
    {
        head = ptr->next;
        if (ptr)
            free(ptr);
    }

    num_list_elements = 0;
}

/**
 * Returns the size of the linked list (included elements)
 *
 * @param[out] num_list_elements - number of included elements
 */
int getNumFaultListElements(void)
{
    return num_list_elements;
}

/**
 * Returns the corresponding FaultList entry to the linked list.
 *
 * @param[in] element - defines  the index of the desired FaultList entry
 * @param[out] fault_element - corresponding pointer to the entry in the linked list.
 */
FaultList* getFaultListElement(int element)
{
    FaultList *ptr = head, *fault_element = NULL;
    int index = 0;

    while (ptr != NULL)
    {
        if (element == index)
            fault_element = ptr;

        index++;
        ptr = ptr->next;
    }

    return fault_element;
}

/**
 * Searches the maximal fault id number in the linked list.
 *
 * @param[out] max_id - the maximal id number
 */
int getMaxIDInFaultList(void)
{
    FaultList *ptr = head;
    int max_id = 0;

    while (ptr != NULL)
    {
        if (ptr->id > max_id)
            max_id = ptr->id;

        ptr = ptr->next;
    }

    return max_id;
}

/**
 * Checks the data types and the content of the parsed XML-parameters
 * for correctness. IMPORTANT: it does not check, if all necessary parameters
 * are defined.
 */
static int validateFaultList(void)
{
    int ret = true;
    FaultList *fault = head;
    char msg_template[] = "FIESER: fault id %d semantic error: %s\n";

    while (fault != NULL)
    {
        if (!fault->component)
        {
            qemu_log(msg_template, fault->id, "<component> not defined");
            ret = false;
        }
        if (!fault->target)
        {
            qemu_log(msg_template, fault->id, "<target> not defined");
            ret = false;
        }
        if (!fault->mode)
        {
            qemu_log(msg_template, fault->id, "<mode> not defined");
            ret = false;
        }
        if (!fault->params.address_defined)
        {
            // we almost always need the address field as trigger for access of PC or the victim address.
            if (fault->target == FI_TAGT_CONDITION_FLAGS && fault->trigger == FI_TRGR_TIME)
            {
                // exception: it is possible to trigger CPSR faults based on time-only, then we don't have an address field
            }
            else
            {
                qemu_log(msg_template, fault->id, "<address> not defined");
                ret = false;
            }
        }


        if (fault->component == FI_COMP_CPU)
        {
            // PC at which to trigger is in <address>
            switch (fault->target)
            {
            case FI_TAGT_INSTRUCTION_DECODER:
                // new opcode for replacement is in <struction>
                if (fault->mode != FI_MODE_NEW_VALUE)
                {
                    //TODO CF: this is a non-sensical limitation of FIES, should allow bit-flips too!
                    qemu_log(msg_template, fault->id, "wrong fault mode selected, <target> is INSTRUCTION DECODER supporting only NEW VALUE");
                    ret = false;
                }
                if (!fault->params.instruction_defined)
                {
                    qemu_log(msg_template, fault->id, "<target> is INSTRUCTION DECODER but <instruction> for replacing the value not defined");
                    ret = false;
                }
            case FI_TAGT_INSTRUCTION_EXECUTION:
                // replaces the opcode at PC simply with a NOP-equivalent, or two in case the instruction was 32bit Thumb
                break;
            case FI_TAGT_CONDITION_FLAGS:
                switch (fault->mode)
                {
                case FI_MODE_CPSR_CF:
                case FI_MODE_CPSR_VF:
                case FI_MODE_CPSR_ZF:
                case FI_MODE_CPSR_NF:
                case FI_MODE_CPSR_QF:
                    break;
                default:
                    qemu_log(msg_template, fault->id, "<target> is CONDITION FLAGS, mode can only be VF, ZF, CF, NF, QF.");
                    ret = false;
                }

                // which flag to flip is stored in set_bit
                if (!fault->params.set_bit_defined)
                {
                    qemu_log(msg_template, fault->id, "target is CONDITION FLAGS but <set_bit> mask for CPSR not defined");
                    ret = false;
                }
                break;
            default:
                qemu_log(msg_template, fault->id, "<component> CPU only supports targets INSTRUCTION DECODER, INSTRUCTION EXECUTION, or CONDITION FLAGS");
                ret = false;
            }
        }



        else if (fault->component == FI_COMP_RAM)
        {

            switch (fault->target)
            {
            case FI_TAGT_MEMORY_CELL:
            case FI_TAGT_ADDRESS_DECODER:
                /**
                 * faults are triggered using the address variable, 
                 * so instruction contains the address/regnum of the victim
                 */
                if (!fault->params.instruction_defined && (fault->trigger == FI_TRGR_PC || fault->trigger == FI_TRGR_TIME))
                {
                    qemu_log(msg_template, fault->id, "target is RAM, trigger is PC or TIME, expected victim address in <instruction> as trigger uses <address>");
                    ret = false;
                }
                break;
            case FI_TAGT_RW_LOGIC:
                break;
            default:
                qemu_log(msg_template, fault->id, "<component> RAM only supports targets MEMORY CELL, ADDRESS DECODER, R/W LOGIC");
                ret = false;
            }

            switch (fault->mode)
            {
            case FI_MODE_NEW_VALUE:
            case FI_MODE_BITFLIP:
            case FI_MODE_STATE_FAULT:
                break;
            default:
                qemu_log(msg_template, fault->id, "<component> RAM only supports modes NEW VALUE, SF, BIT-FLIP");
                ret = false;
            }
        }





        else if (fault->component == FI_COMP_REGISTER)
        {

            switch (fault->target)
            {
            case FI_TAGT_REGISTER_CELL:
                /**
                 * faults are triggered using the address variable, 
                 * so instruction contains the address/regnum of the victim
                 */
                if (!fault->params.instruction_defined && (fault->trigger == FI_TRGR_PC || fault->trigger == FI_TRGR_TIME))
                {
                    qemu_log(msg_template, fault->id, "target is REGISTER CELL, trigger is PC or TIME, expected victim address in <instruction> as trigger uses <address>");
                    ret = false;
                }
                break;
            case FI_TAGT_ADDRESS_DECODER:
                break;
            default:
                qemu_log(msg_template, fault->id, "<component> REGISTER only supports targets MEMORY CELL, ADDRESS DECODER");
                ret = false;
            }

            switch (fault->mode)
            {
            case FI_MODE_NEW_VALUE:
            case FI_MODE_BITFLIP:
            case FI_MODE_STATE_FAULT:
                break;
            default:
                qemu_log(msg_template, fault->id, "<component> RAM only supports modes NEW VALUE, SF, BIT-FLIP");
                ret = false;
            }
        }
        else
        {
            qemu_log(msg_template, fault->id, "<component> has to be CPU, RAM, REGISTER");
            ret = false;
        }

        if (fault->mode == FI_MODE_BITFLIP)
        {
            if (!fault->params.mask_defined)
            {
                qemu_log(msg_template, fault->id, "<mode> BIT-FLIP requires <mask> containing a bitmask indicating for which bits to flip in the target.");
                ret = false;
            }
        }
        else if (fault->mode == FI_MODE_NEW_VALUE)
        {
            if (!fault->params.mask_defined && fault->component != FI_COMP_CPU)
            {
                qemu_log(msg_template, fault->id, "<mode> NEW VALUE requires <mask> containing a the new value to be inserted. Kind of stupid to re-use mask for it, instead of inflating the fault struct with sacrificing one more integer... no?");
                ret = false;
            }
        }
        else if (fault->mode == FI_MODE_STATE_FAULT)
        {
            if (!fault->params.mask_defined)
            {
                qemu_log(msg_template, fault->id, "<mode> SF (state faults) requires <mask> containing a bitmask indicating for which bits to flip in CPSR.");
                ret = false;
            }

            if (!fault->params.set_bit_defined)
            {
                qemu_log(msg_template, fault->id, "<mode> SF (state faults) requires <set_bit> containing a bitmask indicating if the flag should be set or unset.");
                ret = false;
            }
        }

        if (fault->trigger == FI_TRGR_TIME || (fault->trigger == FI_TRGR_ACCESS && fault->component != FI_COMP_CPU))
        {

            switch (fault->type)
            {
            case FI_TYPE_INTERMITTENT:
                if (fault->interval < 0)
                {
                    qemu_log(msg_template, fault->id, "<type> is INTERMITTENT and requires <interval>");
                    ret = false;
                }
            case FI_TYPE_TRANSIENT:
                if (fault->timer < 0)
                {
                    qemu_log(msg_template, fault->id, "<type> is TRANSIENT or INTERMITTENT and requires <timer> as start time after which the fault should come into effect. This can be 0 to not initial delay.");
                    ret = false;
                }
                if (fault->duration < 0)
                {
                    qemu_log(msg_template, fault->id, "<type> is TRANSIENT or INTERMITTENT and requires <duration> as absolute STOP time after which the fault should stop being in effect. This is NOT the duration, but rather a fixed point in time unrelated to the start timer. The original FIES devs just called it like that... sigh...");
                    ret = false;
                }
            case FI_TYPE_PERMANENT:
                // permanent fault activated upon first access
                break;
            default:
                qemu_log(msg_template, fault->id, "<trigger> is TIME or ACCESS, and requires <type> to be set to TRANSIENT, PERMANENT, INTERMITTEND");
                ret = false;
            }
        }

        fault = fault->next;
    }
    return ret;
}

#ifdef LIBXML_READER_ENABLED

/**
 * Parses the fault parameters from the XML file.
 * 
 * @param[in] mon - monitor handle for error logging
 * 
 * @param[in] doc - A structure containing the tree created by a parsed
 *                            doc.
 * @param[in] cur - A structure containing a single node.
 * 
 * @return[out] success, true or false
 */
static int parseFaultFromXML(xmlDocPtr doc, xmlNodePtr cur)
{
    char *key = NULL;
    xmlNodePtr grandchild_node;
    FaultList fault;

    fault.id = -1;
    fault.component = FI_UNDEF;
    fault.target = FI_UNDEF;
    fault.mode = FI_UNDEF;
    fault.trigger = FI_UNDEF;
    fault.type = FI_UNDEF;
    fault.timer = -1;
    fault.duration = -1;
    fault.interval = -1;
    fault.params.address = 0;
    fault.params.address_defined = FI_UNDEF;
    fault.params.cf_address = 0;
    fault.params.cf_address_defined = FI_UNDEF;
    fault.params.mask = 0;
    fault.params.mask_defined = FI_UNDEF;
    fault.params.instruction = 0;
    fault.params.instruction_defined = FI_UNDEF;
    fault.params.set_bit = 0;
    fault.params.set_bit_defined = FI_UNDEF;
    fault.was_triggered = 0;
    fault.next = NULL;

    int ret = true;

    cur = cur->xmlChildrenNode;
    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) "id"))
        {
            long int id;
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
            id = strtol((char *) key, NULL, 10);

            //TODO CF: rework how IDs are used today, this is really wonky and also the above mentioned stuff makes little sense
            // by spec, strtol would also notify out of range errors with ((id == LONG_MAX || id == LONG_MIN) && errno == ERANGE), but the above covers that too.
            fault.id = (id < 1 || id > INT_MAX) ? 0 : (int) id;

            // not allowing 0 is wired, but currently necessary due to old 
            // legacy code from FIES deep down in how transient faults are 
            // handled in an array where we use the ID-1 as [index] 
            // to track set faults

            if (!fault.id)
            {
                ret = false;
                qemu_log("FIESER: fault ENTRY %d: id '%s' is not an integer > 0\n", num_list_elements, key);
            }

            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "component"))
        {
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

            if (!strcmp(key, "CPU"))
            {
                fault.component = FI_COMP_CPU;
            }
            else if (!strcmp(key, "RAM"))
            {
                fault.component = FI_COMP_RAM;
            }
            else if (!strcmp(key, "REGISTER"))
            {
                fault.component = FI_COMP_REGISTER;
            }
            else
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <component> has to be \"CPU, REGISTER or RAM\", was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "target"))
        {
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

            if (!strcmp(key, "REGISTER CELL"))
            {
                fault.target = FI_TAGT_REGISTER_CELL;
            }
            else if (!strcmp(key, "MEMORY CELL"))
            {
                fault.target = FI_TAGT_MEMORY_CELL;
            }
            else if (!strcmp(key, "CONDITION FLAGS"))
            {
                fault.target = FI_TAGT_CONDITION_FLAGS;
            }
            else if (!strcmp(key, "INSTRUCTION EXECUTION"))
            {
                fault.target = FI_TAGT_INSTRUCTION_EXECUTION;
            }
            else if (!strcmp(key, "INSTRUCTION DECODER"))
            {
                fault.target = FI_TAGT_INSTRUCTION_DECODER;
            }
            else if (!strcmp(key, "ADDRESS DECODER"))
            {
                fault.target = FI_TAGT_ADDRESS_DECODER;
            }
            else if (!strcmp(key, "RW LOGIC"))
            {
                fault.target = FI_TAGT_RW_LOGIC;
            }
            else if (!strcmp(key, "TRACE MEMORY"))
            {
                fault.target = FI_TAGT_TRACE_MEMORY;
                profile_ram_addresses = 1;
            }
            else if (!strcmp(key, "TRACE REGISTERS"))
            {
                fault.target = FI_TAGT_TRACE_REGISTERS;
                profile_registers = 1;
            }
            else if (!strcmp(key, "TRACE PC"))
            {
                fault.target = FI_TAGT_TRACE_PC;
                profile_pc_status = 1;
            }
            else if (!strcmp(key, "TRACE CPSR"))
            {
                fault.target = FI_TAGT_TRACE_CPSR;
                profile_condition_flags = 1;
            }
            else
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <target> has to be \"REGISTER CELL, MEMORY CELL, "
                         "CONDITION FLAGS, INSTRUCTION EXECUTION, INSTRUCTION DECODER, "
                         "ADDRESS DECODER, FI_TAGT_RW_LOGIC, TRACE MEM ACCESS/REGISTERS/PC/CPSR\", was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "mode"))
        {
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

            if (!strcmp(key, "NEW VALUE"))
            {
                fault.mode = FI_MODE_NEW_VALUE;
            }
            else if (!strcmp(key, "BITFLIP"))
            {
                fault.mode = FI_MODE_BITFLIP;
            }
            else if (!strcmp(key, "STATE FAULT"))
            {
                fault.mode = FI_MODE_STATE_FAULT;
            }
            else if (!strcmp(key, "CPSR CF"))
            {
                fault.mode = FI_MODE_CPSR_CF;
            }
            else if (!strcmp(key, "CPSR VF"))
            {
                fault.mode = FI_MODE_CPSR_VF;
            }
            else if (!strcmp(key, "CPSR ZF"))
            {
                fault.mode = FI_MODE_CPSR_ZF;
            }
            else if (!strcmp(key, "CPSR NF"))
            {
                fault.mode = FI_MODE_CPSR_NF;
            }
            else if (!strcmp(key, "CPSR QF"))
            {
                fault.mode = FI_MODE_CPSR_QF;
            }
            else
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <mode> not recognized: %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "trigger"))
        {
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

            if (!strcmp(key, "ACCESS"))
            {
                fault.trigger = FI_TRGR_ACCESS;
            }
            else if (!strcmp(key, "TIME"))
            {
                fault.trigger = FI_TRGR_TIME;
            }
            else if (!strcmp(key, "PC"))
            {
                fault.trigger = FI_TRGR_PC;
            }
            else
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <trigger> has to be \"ACCESS, TIME or PC\", was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "type"))
        {
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);

            if (!strcmp(key, "TRANSIENT"))
            {
                fault.type = FI_TYPE_TRANSIENT;
            }
            else if (!strcmp(key, "PERMANENT"))
            {
                fault.type = FI_TYPE_PERMANENT;
            }
            else if (!strcmp(key, "INTERMITTENT"))
            {
                fault.type = FI_TYPE_INTERMITTENT;
            }
            else
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <type> has to be \"TRANSIENT, PERMANENT or INTERMITTENT\", was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "timer"))
        {
            int ok = true;
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
            fault.timer = FIESER_normalize_time_to_int64(key, &ok);

            if (!ok)
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <timer> has to be a positive integer ending in NS/MS/US, was %s\n", fault.id, key);
            }

            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "duration"))
        {
            int ok = true;
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
            fault.duration = FIESER_normalize_time_to_int64(key, &ok);

            if (!ok)
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <duration> has to be a positive integer ending in NS/MS/US, was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "interval"))
        {
            int ok = true;
            key = (char *) xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
            fault.interval = FIESER_normalize_time_to_int64(key, &ok);

            if (!ok)
            {
                ret = false;
                qemu_log("FIESER: fault %d syntax error: <interval> has to be a positive integer ending in NS/MS/US, was %s\n", fault.id, key);
            }
            xmlFree(key);
        }
        else if (!xmlStrcmp(cur->name, (const xmlChar *) "params"))
        {
            grandchild_node = cur->xmlChildrenNode;
            while (grandchild_node != NULL)
            {
                if (!xmlStrcmp(grandchild_node->name, (const xmlChar *) "address"))
                {
                    key = (char *) xmlNodeListGetString(doc, grandchild_node->xmlChildrenNode, 1);
                    fault.params.address = (int) strtoul((char *) key, NULL, 16);
                    fault.params.address_defined = true;
                    xmlFree(key);
                }
                else if (!xmlStrcmp(grandchild_node->name, (const xmlChar *) "cf_address"))
                {
                    key = (char *) xmlNodeListGetString(doc, grandchild_node->xmlChildrenNode, 1);
                    fault.params.cf_address = (int) strtoul((char *) key, NULL, 16);
                    fault.params.cf_address_defined = true;
                    xmlFree(key);
                }
                else if (!xmlStrcmp(grandchild_node->name, (const xmlChar *) "mask"))
                {
                    key = (char *) xmlNodeListGetString(doc, grandchild_node->xmlChildrenNode, 1);
                    fault.params.mask = (int) strtol((char *) key, NULL, 16);
                    fault.params.mask_defined = true;
                    xmlFree(key);
                }
                else if (!xmlStrcmp(grandchild_node->name, (const xmlChar *) "instruction"))
                {
                    key = (char *) xmlNodeListGetString(doc, grandchild_node->xmlChildrenNode, 1);
                    fault.params.instruction = (int) strtoul((char *) key, NULL, 16);
                    fault.params.instruction_defined = true;
                    xmlFree(key);
                }
                else if (!xmlStrcmp(grandchild_node->name, (const xmlChar *) "set_bit"))
                {
                    key = (char *) xmlNodeListGetString(doc, grandchild_node->xmlChildrenNode, 1);
                    fault.params.set_bit = (int) strtol((char *) key, NULL, 16);
                    fault.params.set_bit_defined = true;
                    xmlFree(key);
                }
                else if (grandchild_node->type != XML_TEXT_NODE)
                {
                    qemu_log("FIESER: fault ENTRY %d syntax error in <param>: unknown element %s\n", num_list_elements, cur->name);
                    ret = false;
                }

                grandchild_node = grandchild_node->next;
            }
        }
        else if (cur->type != XML_TEXT_NODE)
        {
            qemu_log("FIESER: fault ENTRY %d syntax error: unknown element %s\n", num_list_elements, cur->name);
            ret = false;
        }

        cur = cur->next;
    }

    add_to_fault_list(&fault);

    return ret;
}

/**
 * Read the XML-file and checks the basic structure of the XML for
 * correctness. Starts the XML-parser.
 *
 * @param[in] mon - Reference to the QEMU-monitor
 * @param[in] filename - The name of the XML-file containing the fault definitions
 */
static int parseFile(const char *filename)
{
    int had_parser_errors = 0;
    int ret = -1;
    xmlDocPtr doc;
    xmlNodePtr cur;

    doc = xmlParseFile(filename);
    if (!doc)
    {
        qemu_log("Document not parsed successfully.\n");
        return -1;
    }

    cur = xmlDocGetRootElement(doc);
    if (!cur)
    {
        qemu_log("Empty document\n");
        goto fail;
    }


    if (xmlStrcmp(cur->name, (const xmlChar *) "injection"))
    {
        qemu_log("Document of the wrong type, root node != injection\n");
        goto fail;
    }

    /**
     * Starting new fault injection experiment -
     * Deleting current context
     */
    if (head)
        delete_fault_list();

    destroy_id_array();
    FIESER_helper_destroy_ops_on_cell();

    cur = cur->xmlChildrenNode;
    while (cur != NULL)
    {
        if (!xmlStrcmp(cur->name, (const xmlChar *) "fault"))
        {
            if (!parseFaultFromXML(doc, cur))
                had_parser_errors++;
        }
        else if (cur->type != XML_TEXT_NODE)
        {
            qemu_log("FIESER: Syntax error: unknown element %s\n", cur->name);
            had_parser_errors++;
        }
        cur = cur->next;
    }

    if (had_parser_errors)
    {
        qemu_log("FIESER: Fault parsing from XML failed. Failed to parse %d rules out of %d recognized fault entries.\n", had_parser_errors, num_list_elements);
        goto fail;
    }

    qemu_log("Fault parsing from XML successful.\n");

    if (!validateFaultList())
    {
        qemu_log("FIESER: Fault definition invalid, see above for detected logic issues.\n");
        goto fail;
    }

    ret = 0;

fail:
    xmlFreeDoc(doc);
    return ret;
}

/**
 * Read the XML-file and checks the basic structure of the XML for
 * correctness. Starts the XML-parser.
 *
 * @param[in] mon - Reference to the QEMU-monitor
 * @param[in] filename - The name of the XML-file containing the fault definitions
 * @param[in] errp - Reference for setting errors in QEMU
 */
void qmp_fault_reload(Monitor *mon, const char *filename, Error **errp)
{
    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    int max_id = 0;

    /**
     * Starting new fault injection experiment -
     * reset timer and statistics
     */
    FIESER_timer_init();
    set_num_injected_faults(0);
    set_num_detected_faults(0);
    set_num_injected_faults_ram_trans(0);
    set_num_injected_faults_ram_perm(0);
    set_num_injected_faults_cpu_trans(0);
    set_num_injected_faults_cpu_perm(0);
    set_num_injected_faults_register_trans(0);
    set_num_injected_faults_register_perm(0);

    LIBXML_TEST_VERSION

    if (parseFile(filename))
    {
        if (mon)
            monitor_printf(mon, "FIESER: Could not load configuration file\n");
        else
            qemu_log("FIESER: Could not load configuration file\n");
    }
    else
    {
        if (mon)
            monitor_printf(mon, "FIESER: Configuration file loaded successfully\n");
        else
            qemu_log("FIESER: Configuration file loaded successfully\n");
    }
#if defined(DEBUG_FAULT_LIST)
    print_fault_list();
#endif

    /**
     * Initialize the context for a new fault injection experiment
     */
    max_id = getMaxIDInFaultList();
    init_id_array(max_id);
    FIESER_helper_init_ops_on_cell(max_id);

    xmlCleanupParser();
}
#else

void qmp_fault_reload(Monitor *mon, const char *filename, Error **errp)
{
    error_setg(errp, "Error: Configuration file not loaded - XInclude support not compiled\n");
}
#endif

