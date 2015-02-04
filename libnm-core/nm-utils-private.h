/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
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
 * Copyright 2005 - 2014 Red Hat, Inc.
 */

#ifndef __NM_UTILS_PRIVATE_H__
#define __NM_UTILS_PRIVATE_H__

#include "nm-setting-private.h"

gboolean    _nm_utils_string_slist_validate (GSList *list,
                                             const char **valid_values);

/* D-Bus transform funcs */

GVariant *  _nm_utils_hwaddr_to_dbus   (const GValue *prop_value);
void        _nm_utils_hwaddr_from_dbus (GVariant *dbus_value,
                                        GValue *prop_value);

GVariant *  _nm_utils_strdict_to_dbus   (const GValue *prop_value);
void        _nm_utils_strdict_from_dbus (GVariant *dbus_value,
                                         GValue *prop_value);

GVariant *  _nm_utils_bytes_to_dbus     (const GValue *prop_value);
void        _nm_utils_bytes_from_dbus   (GVariant *dbus_value,
                                         GValue *prop_value);

GSList *    _nm_utils_strv_to_slist (char **strv);
char **     _nm_utils_slist_to_strv (GSList *slist);

GPtrArray * _nm_utils_strv_to_ptrarray (char **strv);
char **     _nm_utils_ptrarray_to_strv (GPtrArray *ptrarray);

char *      _nm_utils_hwaddr_canonical_or_invalid (const char *mac, gssize length);

void _nm_dbus_errors_init (void);

extern gboolean _nm_utils_is_manager_process;

guint32 _nm_setting_get_setting_priority (NMSetting *setting);
gboolean _nm_setting_get_property (NMSetting *setting, const char *name, GValue *value);

GSList *_nm_utils_hash_values_to_slist (GHashTable *hash);

GHashTable *_nm_utils_copy_strdict (GHashTable *strdict);
GPtrArray *_nm_utils_copy_slist_to_array (const GSList *list,
                                          NMUtilsCopyFunc copy_func,
                                          GDestroyNotify unref_func);
GSList *_nm_utils_copy_array_to_slist (const GPtrArray *array,
                                       NMUtilsCopyFunc copy_func);
GPtrArray *_nm_utils_copy_array (const GPtrArray *array,
                                 NMUtilsCopyFunc copy_func,
                                 GDestroyNotify free_func);
GPtrArray *_nm_utils_copy_object_array (const GPtrArray *array);

gboolean _nm_utils_string_in_list (const char *str,
                                   const char **valid_strings);

char **_nm_utils_strsplit_set (const char *str,
                               const char *delimiters,
                               int max_tokens);

char *_nm_utils_uuid_generate_from_string (const char *s,
                                           gssize slen,
                                           int uuid_type,
                                           gpointer type_args);

GByteArray *_nm_utils_rsa_key_encrypt (const guint8 *data,
                                       gsize len,
                                       const char *in_password,
                                       char **out_password,
                                       GError **error);

void _nm_utils_set_is_manager_process (gboolean is_manager);

#endif
