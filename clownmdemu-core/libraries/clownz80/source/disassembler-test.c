#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../libraries/clowncommon/clowncommon.h"

#include "disassembler.h"

static unsigned char *file_buffer;
static size_t file_size;
static unsigned long file_position;

static cc_bool FileToBuffer(const char* const file_path, unsigned char** const file_buffer, size_t* const file_size)
{
	cc_bool success = cc_false;
	FILE* const file = fopen(file_path, "rb");

	if (file == NULL)
	{
		fprintf(stderr, "Could not open file '%s'.", file_path);
	}
	else
	{
		fseek(file, 0, SEEK_END);
		*file_size = ftell(file);
		rewind(file);
		*file_buffer = (unsigned char*)malloc(*file_size);

		if (*file_buffer == NULL)
		{
			fprintf(stderr, "Could not allocate buffer for file '%s'.", file_path);
		}
		else
		{
			fread(*file_buffer, 1, *file_size, file);
			success = cc_true;
		}

		fclose(file);
	}

	return success;
}

static unsigned char ReadCallback(void* const user_data)
{
	(void)user_data;

	return file_buffer[file_position++];
}

static void PrintCallback(void* const user_data, const char* const format, ...)
{
	va_list args;

	(void)user_data;

	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
}

int main(const int argc, char** const argv)
{
	if (argc < 2)
	{
		fputs("Provide file path as argument.\n", stderr);
		return EXIT_FAILURE;
	}

	if (!FileToBuffer(argv[1], &file_buffer, &file_size))
		return EXIT_FAILURE;

	sscanf(argv[2], "%lX", &file_position);

	ClownZ80_Disassemble(file_position, 10000, ReadCallback, PrintCallback, NULL);

	free(file_buffer);

	return EXIT_SUCCESS;
}
