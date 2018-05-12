/*
 * fault-injection-profiler.c
 *
 *  Created on: 18.12.2015
 *      Author: Andrea Hoeller
 */

#include "fault-injection-profiler.h"

int open_memory_addresses_file = 0;
int open_register_file = 0;
int open_debuglog = 0;

FILE *outfile_memory;
FILE *outfile_registers;
FILE *outfile_debuglog;

void profiler_log(CPUArchState *env, hwaddr *addr, uint32_t *value, AccessType access_type)
{
	if (access_type == write_access_type || access_type == read_access_type)
	{
		if (*addr <= (hwaddr) 15) //GP Register
		{
			if (profile_registers)
				profiler_log_register_access(env, addr, value, access_type);
		}
		else
		{
			if (profile_ram_addresses)
				profiler_log_memory_access(env, addr, value, access_type);
		}
	}
}

void profiler_log_memory_access(CPUArchState *env, hwaddr *addr, uint32_t *value, AccessType access_type)
{
	if (!open_memory_addresses_file)
	{
		outfile_memory = fopen(OUTPUT_FILE_NAME_ACCESSED_MEMORY_ADDRESSES, "w+");
		if (outfile_memory == NULL)
		{
			printf("Error opening file\n");
			perror("Error");
		}
		else
		  open_memory_addresses_file = 1;
	}

	if (access_type == write_access_type)
	{
		fprintf(outfile_memory, "0x%08x w 0x%08x \n", (int)*addr, value ? (int)*value : 0);
	}
	else
	{
		char access_str = (access_type == read_access_type) ? 'r' : 'e';
	 	fprintf(outfile_memory, "0x%08x %c 0x%08x \n", (int)*addr, access_str, value ? (int)*value : 0);
	}
}

void profiler_log_register_access(CPUArchState *env, hwaddr *addr, uint32_t *value, AccessType access_type)
{
	if (!open_register_file)
	{
		outfile_registers = fopen(OUTPUT_FILE_NAME_ACCESSED_REGS, "w+");
		open_register_file = 1;
	}

	if (access_type == write_access_type)
	{
		fprintf(outfile_registers, "0x%08x w 0x%08x \n", (int)*addr, value ? (int)*value : 0);
	}
	else
	{
		char access_str = (access_type == read_access_type) ? 'r' : 'e';
	 	fprintf(outfile_registers, "0x%08x %c 0x%08x \n", (int)*addr, access_str, value ? (int)*value : 0);
	}
}

void profiler_debuglog(const char* msg, ...)
{
    va_list args;
    
    if (!open_debuglog)
    {
	outfile_debuglog = fopen(OUTPUT_FILE_NAME_DEBUGLOG, "w+");
	open_debuglog = 1;
    }
    
    va_start(args, msg);
    vfprintf(outfile_debuglog,msg,args);
    va_end(args);

}

void profiler_close_files(void)
{
	if (open_memory_addresses_file)
	{
		fclose(outfile_memory);
		open_memory_addresses_file = 0;
	}
	if (open_register_file)
	{
		fclose(outfile_registers);
		open_register_file = 0;
	}
        if (open_debuglog)
	{
		fclose(outfile_debuglog);
		open_debuglog = 0;
	}
}
