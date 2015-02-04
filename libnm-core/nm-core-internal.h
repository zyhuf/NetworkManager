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
 * (C) Copyright 2014 Red Hat, Inc.
 */

#ifndef NM_CORE_NM_INTERNAL_H
#define NM_CORE_NM_INTERNAL_H

/* This header file contain functions that are provided as private API
 * by libnm-core. It will contain functions to give privileged access to
 * libnm-core. This can be useful for NetworkManager and libnm.so
 * which both are special users of libnm-core.
 * It also exposes some utility functions for reuse.
 *
 * Only _nm_core_private_call() is exported, but it is only useful to code
 * that has access to this header (libnm-core, libnm, NetworkManager
 * and some test programs).
 **/

#include "nm-connection.h"
#include "nm-core-enum-types.h"
#include "nm-dbus-interface.h"
#include "nm-setting-8021x.h"
#include "nm-setting-adsl.h"
#include "nm-setting-bluetooth.h"
#include "nm-setting-bond.h"
#include "nm-setting-bridge-port.h"
#include "nm-setting-bridge.h"
#include "nm-setting-cdma.h"
#include "nm-setting-connection.h"
#include "nm-setting-dcb.h"
#include "nm-setting-generic.h"
#include "nm-setting-gsm.h"
#include "nm-setting-infiniband.h"
#include "nm-setting-ip4-config.h"
#include "nm-setting-ip6-config.h"
#include "nm-setting-olpc-mesh.h"
#include "nm-setting-ppp.h"
#include "nm-setting-pppoe.h"
#include "nm-setting-serial.h"
#include "nm-setting-team-port.h"
#include "nm-setting-team.h"
#include "nm-setting-vlan.h"
#include "nm-setting-vpn.h"
#include "nm-setting-wimax.h"
#include "nm-setting-wired.h"
#include "nm-setting-wireless-security.h"
#include "nm-setting-wireless.h"
#include "nm-setting.h"
#include "nm-simple-connection.h"
#include "nm-utils.h"
#include "nm-version.h"
#include "nm-vpn-dbus-interface.h"

#define NM_UTILS_CLEAR_CANCELLABLE(c) \
	if (c) { \
		g_cancellable_cancel (c); \
		g_clear_object (&c); \
	}

/* NM_SETTING_COMPARE_FLAG_INFERRABLE: check whether a device-generated
 * connection can be replaced by a already-defined connection. This flag only
 * takes into account properties marked with the %NM_SETTING_PARAM_INFERRABLE
 * flag.
 */
#define NM_SETTING_COMPARE_FLAG_INFERRABLE 0x80000000

#define NM_SETTING_SECRET_FLAGS_ALL \
	(NM_SETTING_SECRET_FLAG_NONE | \
	 NM_SETTING_SECRET_FLAG_AGENT_OWNED | \
	 NM_SETTING_SECRET_FLAG_NOT_SAVED | \
	 NM_SETTING_SECRET_FLAG_NOT_REQUIRED)

typedef gpointer (*NMUtilsCopyFunc) (gpointer);

#define NM_UTILS_UUID_TYPE_LEGACY            0
#define NM_UTILS_UUID_TYPE_VARIANT3          1

typedef struct {
	guint32 (*_nm_setting_get_setting_priority) (NMSetting *setting);
	gboolean (*_nm_setting_get_property) (NMSetting *setting, const char *name, GValue *value);

	GSList * (*_nm_utils_hash_values_to_slist) (GHashTable *hash);

	GHashTable * (*_nm_utils_copy_strdict) (GHashTable *strdict);
	GPtrArray * (*_nm_utils_copy_slist_to_array) (const GSList *list,
	                                              NMUtilsCopyFunc copy_func,
	                                              GDestroyNotify unref_func);
	GSList * (*_nm_utils_copy_array_to_slist) (const GPtrArray *array,
	                                           NMUtilsCopyFunc copy_func);
	GPtrArray * (*_nm_utils_copy_array) (const GPtrArray *array,
	                                     NMUtilsCopyFunc copy_func,
	                                     GDestroyNotify free_func);
	GPtrArray * (*_nm_utils_copy_object_array) (const GPtrArray *array);

	gboolean (*_nm_utils_string_in_list) (const char *str,
	                                      const char **valid_strings);

	char ** (*_nm_utils_strsplit_set) (const char *str,
	                                   const char *delimiters,
	                                   int max_tokens);

	char * (*_nm_utils_uuid_generate_from_string) (const char *s,
	                                               gssize slen,
	                                               int uuid_type,
	                                               gpointer type_args);

	GByteArray * (*_nm_utils_rsa_key_encrypt) (const guint8 *data,
	                                           gsize len,
	                                           const char *in_password,
	                                           char **out_password,
	                                           GError **error);

	void (*_nm_utils_set_is_manager_process) (gboolean is_manager);

} NMCoreInternalFunctions;

NMCoreInternalFunctions *_nm_core_internal_functions_get (void);

#ifndef LIBNM_CORE_COMPILATION

#define _nm_setting_get_setting_priority(setting) (_nm_core_internal_functions_get ()->_nm_setting_get_setting_priority (setting))
#define _nm_setting_get_property(setting, name, value) (_nm_core_internal_functions_get ()->_nm_setting_get_property (setting, name, value))
#define _nm_utils_hash_values_to_slist(hash) (_nm_core_internal_functions_get ()->_nm_utils_hash_values_to_slist (hash))
#define _nm_utils_copy_strdict(strdict) (_nm_core_internal_functions_get ()->_nm_utils_copy_strdict (strdict))
#define _nm_utils_copy_slist_to_array(list, copy_func, unref_func) (_nm_core_internal_functions_get ()->_nm_utils_copy_slist_to_array (list, copy_func, unref_func))
#define _nm_utils_copy_array_to_slist(array, copy_func) (_nm_core_internal_functions_get ()->_nm_utils_copy_array_to_slist (array, copy_func))
#define _nm_utils_copy_array(array, copy_func, free_func) (_nm_core_internal_functions_get ()->_nm_utils_copy_array (array, copy_func, free_func))
#define _nm_utils_copy_object_array(array) (_nm_core_internal_functions_get ()->_nm_utils_copy_object_array (array))
#define _nm_utils_string_in_list(str, valid_strings) (_nm_core_internal_functions_get ()->_nm_utils_string_in_list (str, valid_strings))
#define _nm_utils_strsplit_set(str, delimiters, max_tokens) (_nm_core_internal_functions_get ()->_nm_utils_strsplit_set (str, delimiters, max_tokens))
#define _nm_utils_uuid_generate_from_string(s, slen, uuid_type, type_args) (_nm_core_internal_functions_get ()->_nm_utils_uuid_generate_from_string (s, slen, uuid_type, type_args))
#define _nm_utils_rsa_key_encrypt(data, len, in_password, out_password, error) (_nm_core_internal_functions_get ()->_nm_utils_rsa_key_encrypt (data, len, in_password, out_password, error))
#define _nm_utils_set_is_manager_process(is_manager) (_nm_core_internal_functions_get ()->_nm_utils_set_is_manager_process (is_manager))

#endif /* LIBNM_CORE_COMPILATION */

#endif
