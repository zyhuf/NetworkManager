/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service - keyfile plugin
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

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <nm-utils.h>
#include <nm-setting-connection.h>
#include <nm-setting-wired.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-vlan.h>

#include "NetworkManagerUtils.h"

#include "errors.h"
#include "reader.h"
#include "nm-logging.h"

#include "nm-test-utils.h"

#if 0
static void
connection_diff (NMConnection *a, NMConnection *b)
{
	GHashTable *hash;
	GHashTableIter iter, siter;
	const char *setting_name, *key;
	GHashTable *setting_hash = NULL;

	if (!nm_connection_diff (a, b, NM_SETTING_COMPARE_FLAG_EXACT, &hash)) {
		g_hash_table_iter_init (&iter, hash);
		while (g_hash_table_iter_next (&iter, (gpointer) &setting_name, (gpointer) &setting_hash)) {
			g_hash_table_iter_init (&siter, setting_hash);
			while (g_hash_table_iter_next (&siter, (gpointer) &key, NULL))
				g_message (":: %s :: %s", setting_name,key);
		}
		g_hash_table_destroy (hash);
	}
}
#endif

static inline const char *
inet_ntoa32 (guint32 a)
{
	return inet_ntoa (*((const struct in_addr *) &a));
}

static GPtrArray *
read_block (const char *iscsiadm_path, const guint8 hwaddr[ETH_ALEN])
{
	GSList *blocks, *iter;
	GPtrArray *block = NULL;
	GError *error = NULL;

	blocks = read_ibft_blocks (iscsiadm_path, &error);
	g_assert_no_error (error);
	g_assert (blocks);

	for (iter = blocks; iter; iter = iter->next) {
		const struct ether_addr *e;
		const char *s_hwaddr = NULL;

		if (!parse_ibft_config (iter->data, NULL, "iface.hwaddress", &s_hwaddr, NULL))
			continue;
		g_assert (s_hwaddr);
		e = ether_aton (s_hwaddr);
		if (!e)
			continue;
		if (memcmp (e->ether_addr_octet, hwaddr, ETH_ALEN) == 0) {
			block = g_ptr_array_ref (iter->data);
			break;
		}
	}

	g_slist_foreach (blocks, (GFunc) g_ptr_array_unref, NULL);
	return block;
}

static void
test_read_ibft_dhcp (void)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;
	NMSettingIP4Config *s_ip4;
	GError *error = NULL;
	const GByteArray *array;
	const guint8 expected_mac_address[ETH_ALEN] = { 0x00, 0x33, 0x21, 0x98, 0xb9, 0xf1 };
	gboolean success;
	GPtrArray *block;

	block = read_block (TEST_IBFT_DIR "/iscsiadm-test-dhcp", expected_mac_address);

	connection = connection_from_block (block, &error);
	g_assert_no_error (error);
	g_assert (connection);

	success = nm_connection_verify (connection, &error);
	g_assert_no_error (error);
	g_assert (success);

	/* ===== CONNECTION SETTING ===== */
	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	g_assert_cmpstr (nm_setting_connection_get_id (s_con), ==, "iBFT eth1");
	g_assert_cmpint (nm_setting_connection_get_timestamp (s_con), ==, 0);
	g_assert (nm_setting_connection_get_autoconnect (s_con));
	g_assert (nm_setting_connection_get_read_only (s_con));

	/* ===== WIRED SETTING ===== */
	s_wired = nm_connection_get_setting_wired (connection);
	g_assert (s_wired);
	array = nm_setting_wired_get_mac_address (s_wired);
	g_assert (array);
	g_assert_cmpint (array->len, ==, ETH_ALEN);
	g_assert (memcmp (array->data, &expected_mac_address[0], ETH_ALEN) == 0);
	g_assert_cmpint (nm_setting_wired_get_mtu (s_wired), ==, 0);

	/* ===== IPv4 SETTING ===== */
	s_ip4 = nm_connection_get_setting_ip4_config (connection);
	g_assert (s_ip4);
	g_assert_cmpstr (nm_setting_ip4_config_get_method (s_ip4), ==, NM_SETTING_IP4_CONFIG_METHOD_AUTO);

	g_object_unref (connection);
}

static void
test_read_ibft_static (void)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;
	NMSettingIP4Config *s_ip4;
	GError *error = NULL;
	const GByteArray *array;
	const guint8 expected_mac_address[ETH_ALEN] = { 0x00, 0x33, 0x21, 0x98, 0xb9, 0xf0 };
	NMIP4Address *ip4_addr;
	gboolean success;
	GPtrArray *block;

	block = read_block (TEST_IBFT_DIR "/iscsiadm-test-static", expected_mac_address);

	connection = connection_from_block (block, &error);
	g_assert_no_error (error);
	g_assert (connection);

	success = nm_connection_verify (connection, &error);
	g_assert_no_error (error);
	g_assert (success);

	/* ===== CONNECTION SETTING ===== */
	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	g_assert_cmpstr (nm_setting_connection_get_id (s_con), ==, "iBFT eth0");
	g_assert_cmpint (nm_setting_connection_get_timestamp (s_con), ==, 0);
	g_assert (nm_setting_connection_get_autoconnect (s_con));
	g_assert (nm_setting_connection_get_read_only (s_con));

	/* ===== WIRED SETTING ===== */
	s_wired = nm_connection_get_setting_wired (connection);
	g_assert (s_wired);
	array = nm_setting_wired_get_mac_address (s_wired);
	g_assert (array);
	g_assert_cmpint (array->len, ==, ETH_ALEN);
	g_assert (memcmp (array->data, &expected_mac_address[0], ETH_ALEN) == 0);
	g_assert_cmpint (nm_setting_wired_get_mtu (s_wired), ==, 0);

	/* ===== IPv4 SETTING ===== */
	s_ip4 = nm_connection_get_setting_ip4_config (connection);
	g_assert (s_ip4);
	g_assert_cmpstr (nm_setting_ip4_config_get_method (s_ip4), ==, NM_SETTING_IP4_CONFIG_METHOD_MANUAL);

	g_assert_cmpint (nm_setting_ip4_config_get_num_dns (s_ip4), ==, 2);
	g_assert_cmpstr (inet_ntoa32 (nm_setting_ip4_config_get_dns (s_ip4, 0)), ==, "10.16.255.2");
	g_assert_cmpstr (inet_ntoa32 (nm_setting_ip4_config_get_dns (s_ip4, 1)), ==, "10.16.255.3");

	g_assert_cmpint (nm_setting_ip4_config_get_num_addresses (s_ip4), ==, 1);
	ip4_addr = nm_setting_ip4_config_get_address (s_ip4, 0);
	g_assert (ip4_addr);
	g_assert_cmpstr (inet_ntoa32 (nm_ip4_address_get_address (ip4_addr)), ==, "192.168.32.72");
	g_assert_cmpint (nm_ip4_address_get_prefix (ip4_addr), ==, 22);
	g_assert_cmpstr (inet_ntoa32 (nm_ip4_address_get_gateway (ip4_addr)), ==, "192.168.35.254");

	g_object_unref (connection);
	g_ptr_array_unref (block);
}

static void
test_read_ibft_malformed (gconstpointer user_data)
{
	const char *iscsiadm_path = user_data;
	GSList *blocks;
	GError *error = NULL;

	g_assert (g_file_test (iscsiadm_path, G_FILE_TEST_EXISTS));

	g_test_expect_message ("NetworkManager", G_LOG_LEVEL_WARNING, "*malformed iscsiadm record*");

	blocks = read_ibft_blocks (iscsiadm_path, &error);
	g_assert_no_error (error);
	g_assert (blocks == NULL);

	g_test_assert_expected_messages ();
}

static void
test_read_ibft_bad_address (gconstpointer user_data)
{
	const char *iscsiadm_path = user_data;
	NMConnection *connection;
	const guint8 expected_mac_address[ETH_ALEN] = { 0x00, 0x33, 0x21, 0x98, 0xb9, 0xf0 };
	GPtrArray *block;
	GError *error = NULL;

	g_assert (g_file_test (iscsiadm_path, G_FILE_TEST_EXISTS));

	block = read_block (iscsiadm_path, expected_mac_address);

	connection = connection_from_block (block, &error);
	g_assert_error (error, IBFT_PLUGIN_ERROR, 0);
	g_assert (strstr (error->message, "iBFT: malformed iscsiadm record: invalid"));
	g_clear_error (&error);
	g_assert (connection == NULL);

	g_ptr_array_unref (block);
}

static void
test_read_ibft_vlan (void)
{
	NMConnection *connection;
	NMSettingConnection *s_con;
	NMSettingWired *s_wired;
	NMSettingVlan *s_vlan;
	NMSettingIP4Config *s_ip4;
	const GByteArray *array;
	const guint8 expected_mac_address[ETH_ALEN] = { 0x00, 0x33, 0x21, 0x98, 0xb9, 0xf0 };
	NMIP4Address *ip4_addr;
	gboolean success;
	GError *error = NULL;
	GPtrArray *block;

	block = read_block (TEST_IBFT_DIR "/iscsiadm-test-vlan", expected_mac_address);

	connection = connection_from_block (block, &error);
	g_assert_no_error (error);
	g_assert (connection);

	success = nm_connection_verify (connection, &error);
	g_assert_no_error (error);
	g_assert (success);

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	g_assert_cmpstr (nm_setting_connection_get_connection_type (s_con), ==, NM_SETTING_VLAN_SETTING_NAME);

	/* ===== WIRED SETTING ===== */

	s_wired = nm_connection_get_setting_wired (connection);
	g_assert (s_wired);

	/* MAC address */
	array = nm_setting_wired_get_mac_address (s_wired);
	g_assert (array);
	g_assert_cmpint (array->len, ==, ETH_ALEN);
	g_assert (memcmp (array->data, &expected_mac_address[0], sizeof (expected_mac_address)) == 0);

	/* ===== VLAN SETTING ===== */
	s_vlan = nm_connection_get_setting_vlan (connection);
	g_assert (s_vlan);
	g_assert_cmpint (nm_setting_vlan_get_id (s_vlan), ==, 6);
	g_assert_cmpstr (nm_setting_vlan_get_parent (s_vlan), ==, NULL);
	g_assert_cmpstr (nm_setting_vlan_get_interface_name (s_vlan), ==, NULL);

	/* ===== IPv4 SETTING ===== */
	s_ip4 = nm_connection_get_setting_ip4_config (connection);
	g_assert (s_ip4);
	g_assert_cmpstr (nm_setting_ip4_config_get_method (s_ip4), ==, NM_SETTING_IP4_CONFIG_METHOD_MANUAL);

	g_assert_cmpint (nm_setting_ip4_config_get_num_dns (s_ip4), ==, 1);
	g_assert_cmpstr (inet_ntoa32 (nm_setting_ip4_config_get_dns (s_ip4, 0)), ==, "10.16.255.2");

	g_assert_cmpint (nm_setting_ip4_config_get_num_addresses (s_ip4), ==, 1);
	ip4_addr = nm_setting_ip4_config_get_address (s_ip4, 0);
	g_assert (ip4_addr);
	g_assert_cmpstr (inet_ntoa32 (nm_ip4_address_get_address (ip4_addr)), ==, "192.168.32.72");
	g_assert_cmpint (nm_ip4_address_get_prefix (ip4_addr), ==, 22);
	g_assert_cmpstr (inet_ntoa32 (nm_ip4_address_get_gateway (ip4_addr)), ==, "192.168.35.254");

	g_object_unref (connection);
	g_ptr_array_ref (block);
}

NMTST_DEFINE ();

#define TPATH "/settings/plugins/ibft/"

int main (int argc, char **argv)
{
	nmtst_init_assert_logging (&argc, &argv);

	g_test_add_func (TPATH "ibft/dhcp", test_read_ibft_dhcp);
	g_test_add_func (TPATH "ibft/static", test_read_ibft_static);
	g_test_add_func (TPATH "ibft/vlan", test_read_ibft_vlan);
	g_test_add_data_func (TPATH "ibft/bad-record-read", TEST_IBFT_DIR "/iscsiadm-test-bad-record", test_read_ibft_malformed);
	g_test_add_data_func (TPATH "ibft/bad-entry-read", TEST_IBFT_DIR "/iscsiadm-test-bad-entry", test_read_ibft_malformed);
	g_test_add_data_func (TPATH "ibft/bad-ipaddr-read", TEST_IBFT_DIR "/iscsiadm-test-bad-ipaddr", test_read_ibft_bad_address);
	g_test_add_data_func (TPATH "ibft/bad-gateway-read", TEST_IBFT_DIR "/iscsiadm-test-bad-gateway", test_read_ibft_bad_address);
	g_test_add_data_func (TPATH "ibft/bad-dns1-read", TEST_IBFT_DIR "/iscsiadm-test-bad-dns1", test_read_ibft_bad_address);
	g_test_add_data_func (TPATH "ibft/bad-dns2-read", TEST_IBFT_DIR "/iscsiadm-test-bad-dns2", test_read_ibft_bad_address);

	return g_test_run ();
}

