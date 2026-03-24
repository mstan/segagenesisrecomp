#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "disassembler.h"

static long ReadCallback(void *user_data)
{
	unsigned char bytes[2];

	(void)user_data;

	if (fread(bytes, 2, 1, (FILE*)user_data) != 1)
		return -1;

	return ((long)bytes[0] << 8) | bytes[1];
}

static void PrintCallback(void *user_data, const char *string)
{
	(void)user_data;

	fprintf(stdout, "%s\n", string);
}

int main(const int argc, char** const argv)
{
	int exit_code = EXIT_FAILURE;

	if (argc < 3)
	{
		fprintf(stderr, "Usage: %s [path to file] [address]\n", argv[0]);
	}
	else
	{
		FILE* const file = fopen(argv[1], "rb");

		if (file == NULL)
		{
			fputs("Error: Could not open file for reading!\n", stderr);
		}
		else
		{
			const unsigned long address = strtoul(argv[2], NULL, 0);

			fseek(file, address, SEEK_SET);
			Clown68000_Disassemble(address, 1000, ReadCallback, PrintCallback, file);
			fclose(file);

			exit_code = EXIT_SUCCESS;
		}
	}

	return exit_code;
}
