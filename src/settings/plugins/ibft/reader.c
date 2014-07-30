/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * Copyright (C) 2014 Red Hat, Inc.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <netinet/ether.h>
#include <linux/if.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <nm-connection.h>
#include <NetworkManager.h>
#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-ip6-config.h>
#include <nm-setting-vlan.h>
#include <nm-setting-wired.h>
#include <nm-utils.h>

#include "nm-platform.h"
#include "nm-posix-signals.h"
#include "NetworkManagerUtils.h"
#include "nm-logging.h"

#include "errors.h"
#include "reader.h"

#define PARSE_WARNING(msg...) nm_log_warn (LOGD_SETTINGS, "    " msg)

static gboolean
get_int_full (const char *str, int *value, int min, int max)
{
	char *e;
	long int tmp;

	errno = 0;
	tmp = strtol (str, &e, 0);
	if (errno || *e != '\0' || tmp > max || tmp < min)
		return FALSE;
	if (value)
		*value = (int) tmp;
	return TRUE;
}

static void
iscsiadm_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process here; set a different process group to
	 * ensure signal isolation between child and parent.
	 */
	pid_t pid = getpid ();
	setpgid (pid, pid);

	/*
	 * We blocked signals in main(). We need to restore original signal
	 * mask for iscsiadm here so that it can receive signals.
	 */
	nm_unblock_posix_signals (NULL);
}

/**
 * read_ibft_blocks:
 * @iscsiadm_path: path to iscsiadm program
 * @error: location for an error on failure
 *
 * Parses iscsiadm output and returns a list of arrays, each of which
 * contains the lines from an iscsiadm interface block.
 *
 * Returns: a #GSList, each element of which is a #GPtrArray containing the
 * lines from an iscsiadm interface block. %NULL is returned on errors.
 * Caller owns the returned list and should call g_ptr_array_unref() on each
 * element, and then free the list, when no longer used.
 */
GSList *
read_ibft_blocks (const char *iscsiadm_path, GError **error)
{
	const char *argv[4] = { iscsiadm_path, "-m", "fw", NULL };
	const char *envp[1] = { NULL };
	GSList *blocks = NULL;
	char *out = NULL, *err = NULL;
	gint status = 0;
	char **lines = NULL, **iter;
	GPtrArray *block_lines = NULL;

	g_return_val_if_fail (iscsiadm_path != NULL, NULL);

	if (!g_spawn_sync ("/", (char **) argv, (char **) envp, 0,
	                   iscsiadm_child_setup, NULL, &out, &err, &status, error))
		goto done;

	if (!WIFEXITED (status)) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: %s exited abnormally.", iscsiadm_path);
		goto done;
	}

	if (WEXITSTATUS (status) != 0) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: %s exited with error %d.  Message: '%s'",
		             iscsiadm_path, WEXITSTATUS (status), err ? err : "(none)");
		goto done;
	}

	lines = g_strsplit_set (out, "\n\r", -1);
	for (iter = lines; iter && *iter; iter++) {
		if (!*iter[0])
			continue;

		if (!g_ascii_strcasecmp (*iter, "# BEGIN RECORD")) {
			if (block_lines) {
				PARSE_WARNING ("malformed iscsiadm record: missing END RECORD.");
				g_ptr_array_unref (block_lines);
			}
			/* Start new record */
			block_lines = g_ptr_array_new_full (15, g_free);
		} else if (!g_ascii_strcasecmp (*iter, "# END RECORD")) {
			if (block_lines) {
				if (block_lines->len)
					blocks = g_slist_prepend (blocks, block_lines);
				else
					g_ptr_array_unref (block_lines);
				block_lines = NULL;
			}
		} else if (block_lines) {
			if (strchr (*iter, '='))
				g_ptr_array_add (block_lines, g_strstrip (g_strdup (*iter)));
			else {
				PARSE_WARNING ("malformed iscsiadm record: no = in '%s'.", *iter);
				g_clear_pointer (&block_lines, g_ptr_array_unref);
			}
		}
	}

	if (block_lines) {
		PARSE_WARNING ("malformed iscsiadm record: missing # END RECORD.");
		g_clear_pointer (&block_lines, g_ptr_array_unref);
	}

done:
	g_strfreev (lines);
	g_free (out);
	return blocks;
}

#define ISCSI_HWADDR_TAG     "iface.hwaddress"
#define ISCSI_BOOTPROTO_TAG  "iface.bootproto"
#define ISCSI_IPADDR_TAG     "iface.ipaddress"
#define ISCSI_SUBNET_TAG     "iface.subnet_mask"
#define ISCSI_GATEWAY_TAG    "iface.gateway"
#define ISCSI_DNS1_TAG       "iface.primary_dns"
#define ISCSI_DNS2_TAG       "iface.secondary_dns"
#define ISCSI_VLAN_ID_TAG    "iface.vlan_id"
#define ISCSI_IFACE_TAG      "iface.net_ifacename"

static char *
match_iscsiadm_tag (char *line, const char *tag)
{
	char *p = NULL;

	if (g_ascii_strncasecmp (line, tag, strlen (tag)) == 0) {
		p = strchr (line, '=');
		g_assert (p);
		p = g_strstrip (p + 1);
	}
	return p;
}

/**
 * parse_ibft_config:
 * @data: an array of iscsiadm interface block lines
 * @error: return location for errors
 * @...: pairs of key (const char *) : location (const char **) indicating the
 * key to look for and the location to store the retrieved value in
 *
 * Parses an iscsiadm interface block into variables requested by the caller.
 * Callers should verify the returned data is complete and valid.  Returned
 * strings are owned by @data and should not be used after @data is freed.
 *
 * Returns: %TRUE if at least , %FALSE on failure
 */
gboolean
parse_ibft_config (const GPtrArray *data, GError **error, ...)
{
	gboolean success = FALSE;
	char **out_value, *p;
	va_list ap;
	const char *key;
	guint i;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data->len > 0, FALSE);

	/* Find requested keys and populate return values */
	va_start (ap, error);
	while ((key = va_arg (ap, const char *))) {
		out_value = va_arg (ap, char **);
		*out_value = NULL;
		for (i = 0; i < data->len; i++) {
			p = match_iscsiadm_tag (g_ptr_array_index (data, i), key);
			if (p) {
				*out_value = p;
				success = TRUE;
				break;
			}
		}
	}
	va_end (ap);

	if (!success) {
		g_set_error_literal (error, IBFT_PLUGIN_ERROR, 0,
		                     "iBFT: failed to match at least one iscsiadm block field");
	}
	return success;
}

static gboolean
ip4_setting_add_from_block (const GPtrArray *block,
                            NMConnection *connection,
                            GError **error)
{
	NMSettingIP4Config *s_ip4 = NULL;
	NMIP4Address *addr;
	const char *s_method = NULL;
	const char *s_ipaddr = NULL;
	const char *s_gateway = NULL;
	const char *s_dns1 = NULL;
	const char *s_dns2 = NULL;
	const char *s_netmask = NULL;
	guint32 ipaddr = 0;
	guint32 netmask = 0;
	guint32 gateway = 0;
	guint32 dns1 = 0;
	guint32 dns2 = 0;
	guint32 prefix;

	g_assert (block);

	if (!parse_ibft_config (block, error,
	                        ISCSI_BOOTPROTO_TAG, &s_method,
	                        ISCSI_IPADDR_TAG,    &s_ipaddr,
	                        ISCSI_SUBNET_TAG,    &s_netmask,
	                        ISCSI_GATEWAY_TAG,   &s_gateway,
	                        ISCSI_DNS1_TAG,      &s_dns1,
	                        ISCSI_DNS2_TAG,      &s_dns2,
	                        NULL))
		goto error;

	if (!s_method) {
		g_set_error_literal (error, IBFT_PLUGIN_ERROR, 0,
		                     "iBFT: malformed iscsiadm record: missing " ISCSI_BOOTPROTO_TAG);
		goto error;
	}

	s_ip4 = (NMSettingIP4Config *) nm_setting_ip4_config_new ();

	if (!g_ascii_strcasecmp (s_method, "dhcp")) {
		g_object_set (s_ip4, NM_SETTING_IP4_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
		goto success;
	} else if (g_ascii_strcasecmp (s_method, "static") != 0) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: unknown " ISCSI_BOOTPROTO_TAG " '%s'.",
		             s_method);
		goto error;
	}

	/* Static configuration stuff */
	g_object_set (s_ip4, NM_SETTING_IP4_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_MANUAL, NULL);

	/* IP address */
	if (!s_ipaddr || inet_pton (AF_INET, s_ipaddr, &ipaddr) != 1) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid IP address '%s'.",
		             s_ipaddr);
		goto error;
	}

	/* Subnet/prefix */
	if (!s_netmask || inet_pton (AF_INET, s_netmask, &netmask) != 1) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid subnet mask '%s'.",
		             s_netmask);
		goto error;
	}
	prefix = nm_utils_ip4_netmask_to_prefix (netmask);

	if (!s_gateway || inet_pton (AF_INET, s_gateway, &gateway) != 1) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid IP gateway '%s'.",
		             s_gateway);
		goto error;
	}

	if (s_dns1 && inet_pton (AF_INET, s_dns1, &dns1) != 1) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid DNS1 address '%s'.",
		             s_dns1);
		goto error;
	}

	if (s_dns2 && inet_pton (AF_INET, s_dns2, &dns2) != 1) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid DNS2 address '%s'.",
		             s_dns2);
		goto error;
	}

	addr = nm_ip4_address_new ();
	nm_ip4_address_set_address (addr, ipaddr);
	nm_ip4_address_set_prefix (addr, prefix);
	nm_ip4_address_set_gateway (addr, gateway);
	nm_setting_ip4_config_add_address (s_ip4, addr);
	nm_ip4_address_unref (addr);

	if (dns1)
		nm_setting_ip4_config_add_dns (s_ip4, dns1);
	if (dns2)
		nm_setting_ip4_config_add_dns (s_ip4, dns2);

success:
	nm_connection_add_setting (connection, NM_SETTING (s_ip4));
	return TRUE;

error:
	g_clear_object (&s_ip4);
	return FALSE;
}

static gboolean
connection_setting_add (const GPtrArray *block,
                        NMConnection *connection,
                        const char *type,
                        const char *prefix,
                        const char *iface,
                        GError **error)
{
	NMSetting *s_con;
	char *id, *uuid, *uuid_data;
	const char *s_hwaddr = NULL, *s_ip4addr = NULL, *s_vlanid;

	if (!parse_ibft_config (block, error,
	                        ISCSI_VLAN_ID_TAG, &s_vlanid,
	                        ISCSI_HWADDR_TAG,  &s_hwaddr,
	                        ISCSI_IPADDR_TAG,  &s_ip4addr,
	                        NULL))
		return FALSE;
	if (!s_hwaddr) {
		g_set_error_literal (error, IBFT_PLUGIN_ERROR, 0,
		                     "iBFT: malformed iscsiadm record: missing " ISCSI_HWADDR_TAG);
		return FALSE;
	}

	id = g_strdup_printf ("iBFT%s%s %s",
	                      prefix ? " " : "",
	                      prefix ? prefix : "",
	                      iface);

	uuid_data = g_strdup_printf ("%s%s%s",
	                             s_vlanid ? s_vlanid : "0",
	                             s_hwaddr,
	                             s_ip4addr ? s_ip4addr : "DHCP");
	uuid = nm_utils_uuid_generate_from_string (uuid_data);
	g_free (uuid_data);

	s_con = nm_setting_connection_new ();
	g_object_set (s_con,
	              NM_SETTING_CONNECTION_TYPE, type,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_ID, id,
	              NM_SETTING_CONNECTION_READ_ONLY, TRUE,
	              NULL);

	g_free (uuid);
	g_free (id);

	nm_connection_add_setting (connection, NM_SETTING (s_con));
	return TRUE;
}

static gboolean
is_ibft_vlan_device (const GPtrArray *block)
{
	char *s_vlan_id = NULL;

	if (parse_ibft_config (block, NULL, ISCSI_VLAN_ID_TAG, &s_vlan_id, NULL)) {
		g_assert (s_vlan_id);

		/* VLAN 0 is normally a valid VLAN ID, but in the iBFT case it
		 * means "no VLAN".
		 */
		return get_int_full (s_vlan_id, NULL, 1, 4095);
	}
	return FALSE;
}

static gboolean
vlan_setting_add_from_block (const GPtrArray *block,
                             NMConnection *connection,
                             GError **error)
{
	NMSetting *s_vlan = NULL;
	const char *vlan_id_str = NULL;
	gint vlan_id = -1;

	g_assert (block);
	g_assert (connection);

	/* This won't fail since this function shouldn't be called unless the
	 * iBFT VLAN ID exists and is > 0.
	 */
	g_assert (parse_ibft_config (block, NULL, ISCSI_VLAN_ID_TAG, &vlan_id_str, NULL));
	g_assert (vlan_id_str);

	/* VLAN 0 is normally a valid VLAN ID, but in the iBFT case it means "no VLAN" */
	if (!get_int_full (vlan_id_str, &vlan_id, 1, 4095)) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0, "Invalid VLAN_ID '%s'", vlan_id_str);
		return FALSE;
	}

	s_vlan = nm_setting_vlan_new ();
	g_object_set (s_vlan, NM_SETTING_VLAN_ID, vlan_id, NULL);
	nm_connection_add_setting (connection, NM_SETTING (s_vlan));

	return TRUE;
}

static gboolean
wired_setting_add_from_block (const GPtrArray *block,
                              NMConnection *connection,
                              GError **error)
{
	NMSetting *s_wired = NULL;
	const char *hwaddr_str = NULL;
	GByteArray *hwaddr;

	g_assert (block);
	g_assert (connection);

	if (!parse_ibft_config (block, NULL, ISCSI_HWADDR_TAG, &hwaddr_str, NULL)) {
		g_set_error_literal (error, IBFT_PLUGIN_ERROR, 0,
		                     "iBFT: malformed iscsiadm record: missing " ISCSI_HWADDR_TAG);
		return FALSE;
	}

	hwaddr = nm_utils_hwaddr_atoba (hwaddr_str, ARPHRD_ETHER);
	if (!hwaddr) {
		g_set_error (error, IBFT_PLUGIN_ERROR, 0,
		             "iBFT: malformed iscsiadm record: invalid " ISCSI_HWADDR_TAG " '%s'.",
		             hwaddr_str);
		return FALSE;
	}

	s_wired = nm_setting_wired_new ();
	g_object_set (s_wired, NM_SETTING_WIRED_MAC_ADDRESS, hwaddr, NULL);
	g_byte_array_unref (hwaddr);

	nm_connection_add_setting (connection, s_wired);
	return TRUE;
}

NMConnection *
connection_from_block (const GPtrArray *block, GError **error)
{
	NMConnection *connection = NULL;
	gboolean is_vlan = FALSE;
	const char *iface = NULL;

	g_assert (block);

	if (!parse_ibft_config (block, error, ISCSI_IFACE_TAG, &iface, NULL)) {
		g_set_error_literal (error, IBFT_PLUGIN_ERROR, 0,
		                     "iBFT: malformed iscsiadm record: missing " ISCSI_IFACE_TAG);
		return NULL;
	}

	connection = nm_connection_new ();

	is_vlan = is_ibft_vlan_device (block);
	if (is_vlan && !vlan_setting_add_from_block (block, connection, error))
		goto error;

	/* Always have a wired setting; for VLAN it defines the parent */
	if (!wired_setting_add_from_block (block, connection, error))
		goto error;

	if (!ip4_setting_add_from_block (block, connection, error))
		goto error;

	if (!connection_setting_add (block,
	                             connection,
	                             is_vlan ? NM_SETTING_VLAN_SETTING_NAME : NM_SETTING_WIRED_SETTING_NAME,
	                             is_vlan ? "VLAN" : NULL,
	                             iface,
	                             error))
		goto error;

	if (!nm_connection_normalize (connection, NULL, NULL, error))
		goto error;

	return connection;

error:
	g_object_unref (connection);
	return NULL;
}

