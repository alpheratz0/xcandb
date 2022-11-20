#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

extern void
die(const char *fmt, ...)
{
	va_list args;

	fputs("xcandb: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

extern const char *
enotnull(const char *str, const char *name)
{
	if (NULL == str)
		die("%s cannot be null", name);
	return str;
}

