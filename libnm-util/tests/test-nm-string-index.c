/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 */

#include "nm-string-index.h"

#include <glib.h>
#include <glib-object.h>
#include <string.h>

static void
assert_table (NMStringIndex *table, guint size)
{
	const char *const* keys;
	guint i, j;
	NMStringIndexCompareFunc compare;
	gpointer *data;
	gboolean success;

	g_assert (table);
	g_assert_cmpint (size, ==, nm_string_index_size (table));

	compare = nm_string_index_get_compare_func (table);
	g_assert (compare);

	keys = nm_string_index_get_keys (table);
	data = nm_string_index_get_data (table);
	g_assert (keys);
	g_assert (data);
	for (i = 0; i < size; i++) {
		g_assert (keys[i]);
		g_assert (data[i]);
		if (i > 0) {
			g_assert (keys[i-1] < keys[i]);
			g_assert (keys[i] == &(keys[i-1][strlen (keys[i-1]) + 1]));
			g_assert_cmpint (compare (keys[i-1], keys[i]), <, 0);

			g_assert_cmpint (GPOINTER_TO_INT (data[i-1]), ==, GPOINTER_TO_INT (data[i]) - 1);
		}

		for (j = 0; ; j++) {
			const char *l_key;
			char *tmp_release = NULL;
			int out_idx = 4711 + j;
			const char *out_key = "axb";
			gpointer *out_data = GINT_TO_POINTER (2324+j);

			if (j == 0) {
				l_key = keys[i];
			} else if (j == 1) {
				l_key = tmp_release = g_strdup (keys[i]);
			} else if(j == 2 && strcmp (keys[i], "SUB") == 0) {
				success = nm_string_index_lookup_by_key (table, "SUBSUB", NULL, &l_key, NULL);
				g_assert (success);
				l_key = &l_key[3];
			} else
				break;
			success = nm_string_index_lookup_by_key (table, l_key, &out_idx, &out_key, &out_data);
			g_assert (success);
			g_assert_cmpint (i, ==, out_idx);
			g_assert (&data[i] == out_data);
			g_assert (keys[i] == out_key);

			g_assert (data[i] == nm_string_index_get_data_by_key (table, l_key));

			g_free (tmp_release);

			if (j == 0) {
				success = nm_string_index_lookup_by_index (table, i, &out_key, &out_data);
				g_assert (success);
				g_assert (&data[i] == out_data);
				g_assert (keys[i] == out_key);
			}
		}
	}
	g_assert (!keys[size]);
	g_assert (!data[size]);
}

static void
test_run ()
{
	NMStringIndex *table;
	const NMStringIndexItem args1[] = {
		{
			.key = "c",
			.data = GINT_TO_POINTER (5),
		},
		{
			.key = "SUBSUB",
			.data = GINT_TO_POINTER (2),
		},
		{
			.key = "SUB",
			.data = GINT_TO_POINTER (1),
		},
		{
			.key = "b",
			.data = GINT_TO_POINTER (4),
		},
		{
			.key = "a",
			.data = GINT_TO_POINTER (3),
		},
		{
			.key = "dddc",
			.data = GINT_TO_POINTER (6),
		},
		{
			.key = "dddd",
			.data = GINT_TO_POINTER (7),
		},
		{ 0 },
	};

	table = nm_string_index_new (NULL, -1, args1);
	assert_table (table, 7);

	g_free (table);
}

#define TPATH "/libnm-util/nm-string-index/"

int main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_type_init ();

	g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

	g_test_add_func (TPATH "run", test_run);

	return g_test_run ();
}

