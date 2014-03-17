/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Dan Williams <dcbw@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2014 Red Hat, Inc.
 */

#ifndef __NM_STRING_INDEX_H__
#define __NM_STRING_INDEX_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct NMStringIndex NMStringIndex;

typedef int (*NMStringIndexCompareFunc) (const char *s1, const char *s2);
typedef gboolean (*NMStringIndexForeachFunc) (const char *key, gpointer *pdata, int idx, void *user_data);

typedef struct {
	const char *key;
	void *data;
} NMStringIndexItem;

NMStringIndex *nm_string_index_new (NMStringIndexCompareFunc compare, int argc, const NMStringIndexItem *args);
NMStringIndex *nm_string_index_new_keys_only (NMStringIndexCompareFunc compare, int argc, const char **args);

guint nm_string_index_size (NMStringIndex *table);
NMStringIndexCompareFunc nm_string_index_get_compare_func (NMStringIndex *table);
gboolean nm_string_index_lookup_by_key (NMStringIndex *table, const char *key, int *out_idx, const char **out_key, gpointer **out_data);
gboolean nm_string_index_lookup_by_index (NMStringIndex *table, int idx, const char **out_key, gpointer **out_data);

gpointer nm_string_index_get_data_by_key (NMStringIndex *table, const char *key);

const char *const*nm_string_index_get_keys (NMStringIndex *table);
gpointer *nm_string_index_get_data (NMStringIndex *table);

void nm_string_index_foreach (NMStringIndex *table, NMStringIndexForeachFunc func, void *user_data);

G_END_DECLS

#endif /* __NM_STRING_INDEX_H__ */
