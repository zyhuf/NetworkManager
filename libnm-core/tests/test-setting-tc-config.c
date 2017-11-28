/*
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
 * Copyright 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include <linux/pkt_sched.h>

#include "nm-utils.h"
#include "nm-setting-dummy.h"
#include "nm-setting-connection.h"
#include "nm-setting-tc-config.h"
#include "nm-simple-connection.h"

#include "nm-utils/nm-test-utils.h"

static void
test_tc_config_qdisc (void)
{
	NMTCQdisc *qdisc1, *qdisc2;
	char *str;
	GError *error = NULL;

	qdisc1 = nm_tc_qdisc_new ("fq_codel", AF_UNSPEC, TC_H_UNSPEC, TC_H_ROOT, 0, &error);
	nmtst_assert_success (qdisc1, error);
	qdisc2 = nm_tc_qdisc_new ("fq_codel", AF_UNSPEC, TC_H_UNSPEC, TC_H_ROOT, 0, &error);
	nmtst_assert_success (qdisc2, error);

	g_assert (nm_tc_qdisc_equal (qdisc1, qdisc2));

	nm_tc_qdisc_unref (qdisc2);
	qdisc2 = nm_tc_qdisc_dup (qdisc1);

	g_assert (nm_tc_qdisc_equal (qdisc1, qdisc2));

	g_assert (g_strcmp0 (nm_tc_qdisc_get_kind (qdisc1), "fq_codel") == 0);
	g_assert (nm_tc_qdisc_get_family (qdisc1) == AF_UNSPEC);
	g_assert (nm_tc_qdisc_get_handle (qdisc1) == TC_H_UNSPEC);
	g_assert (nm_tc_qdisc_get_parent (qdisc1) == TC_H_ROOT);
	g_assert (nm_tc_qdisc_get_info (qdisc1) == 0);

	str = nm_utils_tc_qdisc_to_str (qdisc1, &error);
	nmtst_assert_success (str, error);
	g_assert (strcmp (str, "root fq_codel") == 0);
	g_free (str);

	nm_tc_qdisc_set_kind (qdisc1, "ingress");
	nm_tc_qdisc_set_parent (qdisc1, TC_H_INGRESS);

	g_assert (!nm_tc_qdisc_equal (qdisc1, qdisc2));

	str = nm_utils_tc_qdisc_to_str (qdisc1, &error);
	nmtst_assert_success (str, error);
	g_assert (strcmp (str, "ingress") == 0);
	g_free (str);

	nm_tc_qdisc_unref (qdisc1);
	qdisc1 = nm_utils_tc_qdisc_from_str ("narodil sa kristus pan",  &error);
	nmtst_assert_no_success (qdisc1, error);
	g_clear_error (&error);

	qdisc1 = nm_utils_tc_qdisc_from_str ("handle 1234 parent fff1:1 pfifo_fast",  &error);
	nmtst_assert_success (qdisc1, error);

	g_assert (g_strcmp0 (nm_tc_qdisc_get_kind (qdisc1), "pfifo_fast") == 0);
	g_assert (nm_tc_qdisc_get_family (qdisc1) == AF_UNSPEC);
	g_assert (nm_tc_qdisc_get_handle (qdisc1) == TC_H_MAKE (0x1234 << 16, 0x0000));
	g_assert (nm_tc_qdisc_get_parent (qdisc1) == TC_H_MAKE (0xfff1 << 16, 0x0001));
	g_assert (nm_tc_qdisc_get_info (qdisc1) == 0);

	str = nm_utils_tc_qdisc_to_str (qdisc1, &error);
	nmtst_assert_success (str, error);
	g_assert (strcmp (str, "parent fff1:1 handle 1234 pfifo_fast") == 0);
	g_free (str);

	nm_tc_qdisc_unref (qdisc2);
	str = nm_utils_tc_qdisc_to_str (qdisc1, &error);
	nmtst_assert_success (str, error);
	qdisc2 = nm_utils_tc_qdisc_from_str (str, &error);
	nmtst_assert_success (qdisc2, error);
	g_free (str);

	g_assert (nm_tc_qdisc_equal (qdisc1, qdisc2));

	nm_tc_qdisc_unref (qdisc1);
	nm_tc_qdisc_unref (qdisc2);
}

static void
test_tc_config_action (void)
{
	NMTCAction *action1, *action2;
	char *str;
	GError *error = NULL;

	action1 = nm_tc_action_new ("drop", &error);
	nmtst_assert_success (action1, error);
	action2 = nm_tc_action_new ("drop", &error);
	nmtst_assert_success (action2, error);

	g_assert (nm_tc_action_equal (action1, action2));
	g_assert (g_strcmp0 (nm_tc_action_get_kind (action1), "drop") == 0);

	nm_tc_action_set_kind (action1, "simple");
	nm_tc_action_set_attribute (action1, "sdata", g_variant_new_bytestring ("Hello"));

	g_assert (!nm_tc_action_equal (action1, action2));

	str = nm_utils_tc_action_to_str (action1, &error);
	nmtst_assert_success (str, error);
	g_assert (strcmp (str, "simple sdata Hello") == 0);
	g_free (str);

	str = nm_utils_tc_action_to_str (action2, &error);
	nmtst_assert_success (str, error);
	g_assert (strcmp (str, "drop") == 0);
	g_free (str);

	nm_tc_action_unref (action2);
	action2 = nm_tc_action_dup (action1);

	g_assert (nm_tc_action_equal (action1, action2));

	nm_tc_action_unref (action1);
	action1 = nm_utils_tc_action_from_str ("narodil sa kristus pan",  &error);
	nmtst_assert_no_success (action1, error);
	g_clear_error (&error);

	action1 = nm_utils_tc_action_from_str ("simple sdata Hello",  &error);
	nmtst_assert_success (action1, error);

	g_assert (g_strcmp0 (nm_tc_action_get_kind (action1), "simple") == 0);
	g_assert (g_strcmp0 (g_variant_get_bytestring (nm_tc_action_get_attribute (action1, "sdata")),
	                     "Hello") == 0);

	nm_tc_action_unref (action1);
	nm_tc_action_unref (action2);
}

static void
test_tc_config_setting (void)
{
	NMSettingTCConfig *s_tc;
	NMTCQdisc *qdisc1, *qdisc2;
	GError *error = NULL;

	s_tc = (NMSettingTCConfig *) nm_setting_tc_config_new ();

	qdisc1 = nm_tc_qdisc_new ("fq_codel",
	                          AF_UNSPEC,
	                          TC_H_UNSPEC,
	                          TC_H_ROOT,
	                          0,
	                          &error);
	nmtst_assert_success (qdisc1, error);

	qdisc2 = nm_tc_qdisc_new ("pfifo_fast",
	                          AF_UNSPEC,
	                          TC_H_MAKE (0x1234 << 16, 0x0000),
	                          TC_H_MAKE (0xfff1 << 16, 0x0001),
	                          0,
	                          &error);
	nmtst_assert_success (qdisc2, error);

	g_assert (nm_setting_tc_config_get_num_qdiscs (s_tc) == 0);
	g_assert (nm_setting_tc_config_add_qdisc (s_tc, qdisc1) == TRUE);
	g_assert (nm_setting_tc_config_get_num_qdiscs (s_tc) == 1);
	g_assert (nm_setting_tc_config_get_qdisc (s_tc, 0) != NULL);
	g_assert (nm_setting_tc_config_remove_qdisc_by_value (s_tc, qdisc2) == FALSE);
	g_assert (nm_setting_tc_config_add_qdisc (s_tc, qdisc2) == TRUE);
	g_assert (nm_setting_tc_config_get_num_qdiscs (s_tc) == 2);
	g_assert (nm_setting_tc_config_remove_qdisc_by_value (s_tc, qdisc1) == TRUE);
	g_assert (nm_setting_tc_config_get_num_qdiscs (s_tc) == 1);
	nm_setting_tc_config_clear_qdiscs (s_tc);
	g_assert (nm_setting_tc_config_get_num_qdiscs (s_tc) == 0);

	nm_tc_qdisc_unref (qdisc1);
	nm_tc_qdisc_unref (qdisc2);
}

static void
test_tc_config_dbus (void)
{
	NMConnection *connection1, *connection2;
	NMSetting *s_tc;
	NMTCQdisc *qdisc1, *qdisc2;
	GVariant *dbus, *tc_dbus;
	GError *error = NULL;

	connection1 = nmtst_create_minimal_connection ("dummy",
	                                               NULL,
	                                               NM_SETTING_DUMMY_SETTING_NAME,
	                                               NULL);

	s_tc = nm_setting_tc_config_new ();

	qdisc1 = nm_tc_qdisc_new ("fq_codel",
	                          AF_UNSPEC,
	                          TC_H_MAKE (0x1234 << 16, 0x0000),
	                          TC_H_ROOT,
	                          0,
	                          &error);
	nmtst_assert_success (qdisc1, error);
	nm_setting_tc_config_add_qdisc (NM_SETTING_TC_CONFIG (s_tc), qdisc1);

	qdisc2 = nm_tc_qdisc_new ("ingress",
	                          AF_UNSPEC,
	                          TC_H_MAKE (TC_H_INGRESS, 0),
	                          TC_H_INGRESS,
	                          0,
	                          &error);
	nmtst_assert_success (qdisc2, error);
	nm_setting_tc_config_add_qdisc (NM_SETTING_TC_CONFIG (s_tc), qdisc2);


	nm_connection_add_setting (connection1, s_tc);

	dbus = nm_connection_to_dbus (connection1, NM_CONNECTION_SERIALIZE_ALL);

	tc_dbus = g_variant_lookup_value (dbus, "tc", G_VARIANT_TYPE_VARDICT);
	g_assert (tc_dbus);

	g_assert (g_variant_equal (g_variant_lookup_value (tc_dbus, "qdiscs", G_VARIANT_TYPE ("aa{sv}")),
	                           g_variant_new_parsed ("[{'kind':   <'fq_codel'>,"
	                                                 "  'family': <uint32 0>,"
	                                                 "  'handle': <uint32 0x12340000>,"
	                                                 "  'parent': <uint32 0xffffffff>,"
	                                                 "  'info':   <uint32 0>},"
	                                                 " {'kind':   <'ingress'>,"
	                                                 "  'family': <uint32 0>,"
	                                                 "  'handle': <uint32 0xffff0000>,"
	                                                 "  'parent': <uint32 0xfffffff1>,"
	                                                 "  'info':   <uint32 0>}]")));

	g_variant_unref (tc_dbus);
	connection2 = nm_simple_connection_new ();
	nmtst_assert_success (nm_connection_replace_settings (connection2, dbus, &error), error);

	g_assert (nm_connection_diff (connection1, connection2, NM_SETTING_COMPARE_FLAG_EXACT, NULL));

	g_variant_unref (dbus);

	nm_tc_qdisc_unref (qdisc1);
	nm_tc_qdisc_unref (qdisc2);

	g_object_unref (connection1);
	g_object_unref (connection2);
}

NMTST_DEFINE ();

int
main (int argc, char **argv)
{
	nmtst_init (&argc, &argv, TRUE);

	g_test_add_func ("/libnm/settings/tc_config/qdisc", test_tc_config_qdisc);
	g_test_add_func ("/libnm/settings/tc_config/action", test_tc_config_action);
	g_test_add_func ("/libnm/settings/tc_config/setting", test_tc_config_setting);
	g_test_add_func ("/libnm/settings/tc_config/dbus", test_tc_config_dbus);

	return g_test_run ();
}
