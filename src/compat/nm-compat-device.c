/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
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
 * Copyright (C) 2011 Red Hat, Inc.
 */

#include "config.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "NetworkManager.h"
#include "nm-compat-device.h"
#include "nm-dbus-glib-types.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"
#include "nm-device-interface.h"
#include "nm-device-modem.h"

G_DEFINE_TYPE (NMCompatDevice, nm_compat_device, G_TYPE_OBJECT)

#define NM_COMPAT_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_COMPAT_DEVICE, NMCompatDevicePrivate))

typedef struct {
	gboolean disposed;

	NMDevice *parent;
	char *path;
} NMCompatDevicePrivate;

enum {
	PROP_0,
	PROP_UDI,
	PROP_IFACE,
	PROP_IP_IFACE,
	PROP_DRIVER,
	PROP_CAPABILITIES,
	PROP_IP4_ADDRESS,
	PROP_IP4_CONFIG,
	PROP_DHCP4_CONFIG,
	PROP_IP6_CONFIG,
	PROP_DHCP6_CONFIG,
	PROP_STATE,
	PROP_MANAGED,
	PROP_DEVICE_TYPE,
	LAST_PROP
};

enum {
	STATE_CHANGED,
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void impl_compat_device_disconnect (NMCompatDevice *self,
                                           DBusGMethodInvocation *context);

#include "nm-compat-device-glue.h"

/*************************************************************************/

static void
impl_compat_device_disconnect (NMCompatDevice *self,
                               DBusGMethodInvocation *context)
{
	NMCompatDevicePrivate *priv = NM_COMPAT_DEVICE_GET_PRIVATE (self);

	g_signal_emit_by_name (priv->parent, NM_DEVICE_INTERFACE_DISCONNECT_REQUEST, context);
}

#define NM_OLD_DEVICE_STATE_UNKNOWN      0
#define NM_OLD_DEVICE_STATE_UNMANAGED    1
#define NM_OLD_DEVICE_STATE_UNAVAILABLE  2
#define NM_OLD_DEVICE_STATE_DISCONNECTED 3
#define NM_OLD_DEVICE_STATE_PREPARE      4
#define NM_OLD_DEVICE_STATE_CONFIG       5
#define NM_OLD_DEVICE_STATE_NEED_AUTH    6
#define NM_OLD_DEVICE_STATE_IP_CONFIG    7
#define NM_OLD_DEVICE_STATE_ACTIVATED    8
#define NM_OLD_DEVICE_STATE_FAILED       9

static guint32
new_state_to_old (NMDeviceState state)
{
	switch (state) {
	case NM_DEVICE_STATE_UNMANAGED:
		return NM_OLD_DEVICE_STATE_UNMANAGED;
	case NM_DEVICE_STATE_UNAVAILABLE:
		return NM_OLD_DEVICE_STATE_UNAVAILABLE;
	case NM_DEVICE_STATE_DEACTIVATING:
	case NM_DEVICE_STATE_DISCONNECTED:
		return NM_OLD_DEVICE_STATE_DISCONNECTED;
	case NM_DEVICE_STATE_PREPARE:
		return NM_OLD_DEVICE_STATE_PREPARE;
	case NM_DEVICE_STATE_CONFIG:
		return NM_OLD_DEVICE_STATE_CONFIG;
	case NM_DEVICE_STATE_NEED_AUTH:
		return NM_OLD_DEVICE_STATE_NEED_AUTH;
	case NM_DEVICE_STATE_IP_CONFIG:
	case NM_DEVICE_STATE_IP_CHECK:
	case NM_DEVICE_STATE_SECONDARIES:
		return NM_OLD_DEVICE_STATE_IP_CONFIG;
	case NM_DEVICE_STATE_ACTIVATED:
		return NM_OLD_DEVICE_STATE_ACTIVATED;
	case NM_DEVICE_STATE_FAILED:
		return NM_OLD_DEVICE_STATE_FAILED;
	default:
		return NM_STATE_UNKNOWN;
	}
	g_assert_not_reached ();
}

static void
state_changed_cb (NMDevice *parent, NMDeviceState state, NMCompatDevice *self)
{
	g_signal_emit (self, signals[STATE_CHANGED], 0, new_state_to_old (state));
}

static void
prop_reemit_cb (GObject *object, GParamSpec *pspec, NMCompatDevice *self)
{
	g_object_notify (G_OBJECT (self), pspec->name);
}

/*************************************************************************/

const char *
nm_compat_device_get_path (NMCompatDevice *compat)
{
	return NM_COMPAT_DEVICE_GET_PRIVATE (compat)->path;
}

void
nm_compat_device_export (NMCompatDevice *self, DBusGConnection *bus)
{
	NMCompatDevicePrivate *priv = NM_COMPAT_DEVICE_GET_PRIVATE (self);
	static guint32 idx = 0;

	priv->path = g_strdup_printf ("/org/freedesktop/NetworkManagerCompat/Devices/%d", idx++);
	dbus_g_connection_register_g_object (bus, priv->path, G_OBJECT (self));
}

NMCompatDevice *
nm_compat_device_new (NMDevice *parent)
{
	NMCompatDevice *self;

	self = (NMCompatDevice *) g_object_new (NM_TYPE_COMPAT_DEVICE, NULL);
	if (self) {
		NM_COMPAT_DEVICE_GET_PRIVATE (self)->parent = parent;
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_UDI, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_IFACE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_IP_IFACE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_DRIVER, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_CAPABILITIES, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_IP4_ADDRESS, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_IP4_CONFIG, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_DHCP4_CONFIG, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_IP6_CONFIG, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_DHCP6_CONFIG, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_STATE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_DEVICE_TYPE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_INTERFACE_MANAGED, G_CALLBACK (prop_reemit_cb), self);

		g_signal_connect (parent, "state-changed", G_CALLBACK (state_changed_cb), self);
	}

	return self;
}

static void
nm_compat_device_init (NMCompatDevice *self)
{
}

static void
set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec)
{
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

#define NM_OLD_DEVICE_TYPE_GSM 3
#define NM_OLD_DEVICE_TYPE_CDMA 4

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMCompatDevice *self = NM_COMPAT_DEVICE (object);
	NMCompatDevicePrivate *priv = NM_COMPAT_DEVICE_GET_PRIVATE (self);
	char *str;
	guint32 u;
	gboolean bool;

	switch (prop_id) {
	case PROP_UDI:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_UDI, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_IFACE:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_IFACE, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_IP_IFACE:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_IP_IFACE, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_DRIVER:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_DRIVER, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_CAPABILITIES:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_CAPABILITIES, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_IP4_ADDRESS:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_IP4_ADDRESS, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_IP4_CONFIG:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_IP4_CONFIG, &str, NULL);
		g_value_take_boxed (value, str);
		break;
	case PROP_DHCP4_CONFIG:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_DHCP4_CONFIG, &str, NULL);
		g_value_take_boxed (value, str);
		break;
	case PROP_IP6_CONFIG:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_IP6_CONFIG, &str, NULL);
		g_value_take_boxed (value, str);
		break;
	case PROP_DHCP6_CONFIG:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_DHCP6_CONFIG, &str, NULL);
		g_value_take_boxed (value, str);
		break;
	case PROP_STATE:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_STATE, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_MANAGED:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_MANAGED, &bool, NULL);
		g_value_set_boolean (value, bool);
		break;
	case PROP_DEVICE_TYPE:
		g_object_get (priv->parent, NM_DEVICE_INTERFACE_DEVICE_TYPE, &u, NULL);

		/* Handle GSM/CDMA */
		if (u == NM_DEVICE_TYPE_MODEM) {
			NMDeviceModemCapabilities caps = NM_DEVICE_MODEM_CAPABILITY_NONE;

			g_object_get (G_OBJECT (priv->parent), NM_DEVICE_MODEM_CURRENT_CAPABILITIES, &caps, NULL);
			if (caps & NM_DEVICE_MODEM_CAPABILITY_CDMA_EVDO)
				u = NM_OLD_DEVICE_TYPE_CDMA;
			else if (caps & NM_DEVICE_MODEM_CAPABILITY_GSM_UMTS)
				u = NM_OLD_DEVICE_TYPE_GSM;
		}
		g_value_set_uint (value, u);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	g_free (NM_COMPAT_DEVICE_GET_PRIVATE (object)->path);
	G_OBJECT_CLASS (nm_compat_device_parent_class)->finalize (object);
}

static void
nm_compat_device_class_init (NMCompatDeviceClass *compat_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (compat_class);

	g_type_class_add_private (compat_class, sizeof (NMCompatDevicePrivate));

	object_class->finalize = finalize;
	object_class->set_property = set_property;
	object_class->get_property = get_property;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_UDI,
		 g_param_spec_string (NM_DEVICE_INTERFACE_UDI,
							  "UDI",
							  "Unique Device Identifier",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_IFACE,
		 g_param_spec_string (NM_DEVICE_INTERFACE_IFACE,
							  "Interface",
							  "Interface",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_IP_IFACE,
		 g_param_spec_string (NM_DEVICE_INTERFACE_IP_IFACE,
		                      "IP Interface",
		                      "IP Interface",
		                      NULL,
		                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_DRIVER,
		 g_param_spec_string (NM_DEVICE_INTERFACE_DRIVER,
							  "Driver",
							  "Driver",
							  NULL,
							  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	g_object_class_install_property
		(object_class, PROP_CAPABILITIES,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_CAPABILITIES,
							"Capabilities",
							"Capabilities",
							0, G_MAXUINT32, NM_DEVICE_CAP_NONE,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_IP4_ADDRESS,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_IP4_ADDRESS,
							"IP4 address",
							"IP4 address",
							0, G_MAXUINT32, 0, /* FIXME */
							G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_IP4_CONFIG,
		 g_param_spec_boxed (NM_DEVICE_INTERFACE_IP4_CONFIG,
							  "IP4 Config",
							  "IP4 Config",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_DHCP4_CONFIG,
		 g_param_spec_boxed (NM_DEVICE_INTERFACE_DHCP4_CONFIG,
							  "DHCP4 Config",
							  "DHCP4 Config",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_IP6_CONFIG,
		 g_param_spec_boxed (NM_DEVICE_INTERFACE_IP6_CONFIG,
							  "IP6 Config",
							  "IP6 Config",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_DHCP6_CONFIG,
		 g_param_spec_boxed (NM_DEVICE_INTERFACE_DHCP6_CONFIG,
							  "DHCP6 Config",
							  "DHCP6 Config",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_STATE,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_STATE,
							"State",
							"State",
							0, G_MAXUINT32, NM_DEVICE_STATE_UNKNOWN,
							G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_DEVICE_TYPE,
		 g_param_spec_uint (NM_DEVICE_INTERFACE_DEVICE_TYPE,
							"DeviceType",
							"DeviceType",
							0, G_MAXUINT32, NM_DEVICE_TYPE_UNKNOWN,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | NM_PROPERTY_PARAM_NO_EXPORT));

	g_object_class_install_property
		(object_class, PROP_MANAGED,
		 g_param_spec_boolean (NM_DEVICE_INTERFACE_MANAGED,
	                           "Managed",
	                           "Managed",
	                           FALSE,
	                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  0, NULL, NULL,
					  _nm_marshal_VOID__UINT_UINT_UINT,
					  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatDeviceClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (compat_class), &dbus_glib_nm_compat_device_object_info);
}

