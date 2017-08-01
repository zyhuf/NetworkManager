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
 * Copyright 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-setting-ovs-interface.h"

#include "nm-connection-private.h"
#include "nm-setting-connection.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-ovs-interface
 * @short_description: Describes connection properties for OpenVSwitch ports.
 *
 * The #NMSettingOvsInterface object is a #NMSetting subclass that describes properties
 * necessary for OpenVSwitch ports.
 **/

/**
 * NMSettingOvsInterface:
 *
 * OvsInterface Link Settings
 */
struct _NMSettingOvsInterface {
	NMSetting parent;

	char *fail_mode;
	gboolean mcast_snooping_enable;
	gboolean rstp_enable;
	gboolean stp_enable;
};

struct _NMSettingOvsInterfaceClass {
	NMSettingClass parent;
};

G_DEFINE_TYPE_WITH_CODE (NMSettingOvsInterface, nm_setting_ovs_interface, NM_TYPE_SETTING,
                         _nm_register_setting (OVS_INTERFACE, NM_SETTING_PRIORITY_HW_BASE))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_OVS_INTERFACE)

enum {
	PROP_0,
	PROP_FAIL_MODE,
	PROP_MCAST_SNOOPING_ENABLE,
	PROP_RSTP_ENABLE,
	PROP_STP_ENABLE,
	LAST_PROP
};

/**
 * nm_setting_ovs_interface_new:
 *
 * Creates a new #NMSettingOvsInterface object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingOvsInterface object
 *
 * Since: 1.10
 **/
NMSetting *
nm_setting_ovs_interface_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_OVS_INTERFACE, NULL);
}

/**
 * nm_setting_ovs_interface_get_fail_mode:
 * @s_ovs_interface: the #NMSettingOvsInterface
 *
 * Returns: the #NMSettingOvsInterface:fail_mode property of the setting
 *
 * Since: 1.10
 **/
const char *
nm_setting_ovs_interface_get_fail_mode (NMSettingOvsInterface *s_ovs_interface)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_INTERFACE (s_ovs_interface), NULL);

	return s_ovs_interface->fail_mode;
}

/**
 * nm_setting_ovs_interface_get_mcast_snooping_enable:
 * @s_ovs_interface: the #NMSettingOvsInterface
 *
 * Returns: the #NMSettingOvsInterface:mcast_snooping_enable property of the setting
 *
 * Since: 1.10
 **/
gboolean
nm_setting_ovs_interface_get_mcast_snooping_enable (NMSettingOvsInterface *s_ovs_interface)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_INTERFACE (s_ovs_interface), FALSE);

	return s_ovs_interface->mcast_snooping_enable;
}

/**
 * nm_setting_ovs_interface_get_rstp_enable:
 * @s_ovs_interface: the #NMSettingOvsInterface
 *
 * Returns: the #NMSettingOvsInterface:rstp_enable property of the setting
 *
 * Since: 1.10
 **/
gboolean
nm_setting_ovs_interface_get_rstp_enable (NMSettingOvsInterface *s_ovs_interface)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_INTERFACE (s_ovs_interface), FALSE);

	return s_ovs_interface->rstp_enable;
}

/**
 * nm_setting_ovs_interface_get_stp_enable:
 * @s_ovs_interface: the #NMSettingOvsInterface
 *
 * Returns: the #NMSettingOvsInterface:stp_enable property of the setting
 *
 * Since: 1.10
 **/
gboolean
nm_setting_ovs_interface_get_stp_enable (NMSettingOvsInterface *s_ovs_interface)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_INTERFACE (s_ovs_interface), FALSE);

	return s_ovs_interface->stp_enable;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingOvsInterface *s_ovs_interface = NM_SETTING_OVS_INTERFACE (object);

	switch (prop_id) {
	case PROP_FAIL_MODE:
		g_free (s_ovs_interface->fail_mode);
		s_ovs_interface->fail_mode = g_value_dup_string (value);
		break;
	case PROP_MCAST_SNOOPING_ENABLE:
		s_ovs_interface->mcast_snooping_enable = g_value_get_boolean (value);
		break;
	case PROP_RSTP_ENABLE:
		s_ovs_interface->rstp_enable = g_value_get_boolean (value);
		break;
	case PROP_STP_ENABLE:
		s_ovs_interface->stp_enable = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSettingOvsInterface *s_ovs_interface = NM_SETTING_OVS_INTERFACE (object);

	switch (prop_id) {
	case PROP_FAIL_MODE:
		g_value_set_string (value, s_ovs_interface->fail_mode);
		break;
	case PROP_MCAST_SNOOPING_ENABLE:
		g_value_set_boolean (value, s_ovs_interface->mcast_snooping_enable);
		break;
	case PROP_RSTP_ENABLE:
		g_value_set_boolean (value, s_ovs_interface->rstp_enable);
		break;
	case PROP_STP_ENABLE:
		g_value_set_boolean (value, s_ovs_interface->stp_enable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static int
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingOvsInterface *s_ovs_interface = NM_SETTING_OVS_INTERFACE (setting);

	if (!_nm_connection_verify_required_interface_name (connection, error))
		return FALSE;

	if (!NM_IN_STRSET (s_ovs_interface->fail_mode, "secure", "standalone", NULL)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not allowed in fail_mode"),
		             s_ovs_interface->fail_mode);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_OVS_INTERFACE_SETTING_NAME, NM_SETTING_OVS_INTERFACE_FAIL_MODE);
		return FALSE;
	}

	return TRUE;
}

static void
finalize (GObject *object)
{
	NMSettingOvsInterface *s_ovs_interface = NM_SETTING_OVS_INTERFACE (object);

	g_free (s_ovs_interface->fail_mode);

	G_OBJECT_CLASS (nm_setting_ovs_interface_parent_class)->finalize (object);
}

static void
nm_setting_ovs_interface_init (NMSettingOvsInterface *s_ovs_interface)
{
}

static void
nm_setting_ovs_interface_class_init (NMSettingOvsInterfaceClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;
	parent_class->verify = verify;

	/**
	 * NMSettingOvsInterface:fail_mode:
	 *
	 * The bridge failure mode. One of "secure", "standalone" or empty.
	 **/
	g_object_class_install_property
		(object_class, PROP_FAIL_MODE,
		 g_param_spec_string (NM_SETTING_OVS_INTERFACE_FAIL_MODE, "", "",
	                              NULL,
	                              G_PARAM_READWRITE |
	                              G_PARAM_CONSTRUCT |
	                              NM_SETTING_PARAM_INFERRABLE |
	                              G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsInterface:mcast-snooping_enable:
	 *
	 * Enable or disable multicast snooping.
	 **/
	g_object_class_install_property
	        (object_class, PROP_MCAST_SNOOPING_ENABLE,
	         g_param_spec_boolean (NM_SETTING_OVS_INTERFACE_MCAST_SNOOPING_ENABLE, "", "",
	                               FALSE,
	                               G_PARAM_READWRITE |
	                               G_PARAM_CONSTRUCT |
	                               G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsInterface:rstp-enable:
	 *
	 *
	 * Enable or disable RSTP.
	 **/
	g_object_class_install_property
	        (object_class, PROP_RSTP_ENABLE,
	         g_param_spec_boolean (NM_SETTING_OVS_INTERFACE_RSTP_ENABLE, "", "",
	                               FALSE,
	                               G_PARAM_READWRITE |
	                               G_PARAM_CONSTRUCT |
	                               G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsInterface:stp-enable:
	 *
	 * Enable or disable STP.
	 **/
	g_object_class_install_property
	        (object_class, PROP_STP_ENABLE,
	         g_param_spec_boolean (NM_SETTING_OVS_INTERFACE_STP_ENABLE, "", "",
	                               FALSE,
	                               G_PARAM_READWRITE |
	                               G_PARAM_CONSTRUCT |
	                               G_PARAM_STATIC_STRINGS));
}
