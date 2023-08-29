/*
	Copyright (C) 2022-2023 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License version 2 as published by
	the Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc., 59
	Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#pragma once

#include <stdbool.h>
#include <stddef.h>

extern const char *
enotnull(const char *str, const char *name);

extern void *
xmalloc(size_t size);

extern void *
xcalloc(size_t nmemb, size_t size);

extern char *
xstrdup(const char *str);

extern char *
xprompt(const char *prompt);

extern bool
is_writeable(const char *path);

extern char *
expand_path(const char *path);
