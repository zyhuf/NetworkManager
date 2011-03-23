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
#include "nm-compat-device-wifi.h"
#include "nm-dbus-glib-types.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"

G_DEFINE_TYPE (NMCompatDeviceWifi, nm_compat_device_wifi, NM_TYPE_COMPAT_DEVICE)

#define NM_COMPAT_DEVICE_WIFI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                  NM_TYPE_COMPAT_DEVICE_WIFI, \
                                                  NMCompatDeviceWifiPrivate))

typedef struct {
	gboolean disposed;
} NMCompatDeviceWifiPrivate;

enum {
	PROP_0,
	PROP_HW_ADDRESS,
	PROP_PERM_HW_ADDRESS,
	PROP_MODE,
	PROP_BITRATE,
	PROP_ACTIVE_ACCESS_POINT,
	PROP_CAPABILITIES,
	LAST_PROP
};

enum {
	ACCESS_POINT_ADDED,
	ACCESS_POINT_REMOVED,
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static gboolean impl_compat_device_get_access_points (NMCompatDeviceWifi *device,
                                                      GPtrArray **aps,
                                                      GError **err);

#include "nm-compat-device-wifi-glue.h"

/*************************************************************************/

static gboolean
impl_compat_device_get_access_points (NMCompatDeviceWifi *self,
                                      GPtrArray **aps,
                                      GError **err)
{
	NMDevice *parent = nm_compat_device_get_parent (NM_COMPAT_DEVICE (self));

	*aps = nm_device_wifi_get_access_points (NM_DEVICE_WIFI (parent));
	return TRUE;
}

/*************************************************************************/

static void
prop_reemit_cb (GObject *object, GParamSpec *pspec, NMCompatDevice *self)
{
	g_object_notify (G_OBJECT (self), pspec->name);
}

static void
ap_added_cb (GObject *object, NMAccessPoint *ap, NMCompatDeviceWifi *self)
{
	g_signal_emit (self, signals[ACCESS_POINT_ADDED], 0, ap);
}

static void
ap_removed_cb (GObject *object, NMAccessPoint *ap, NMCompatDeviceWifi *self)
{
	g_signal_emit (self, signals[ACCESS_POINT_REMOVED], 0, ap);
}

NMCompatDeviceWifi *
nm_compat_device_wifi_new (NMDeviceWifi *parent)
{
	NMCompatDeviceWifi *self;

	self = (NMCompatDeviceWifi *) g_object_new (NM_TYPE_COMPAT_DEVICE_WIFI,
	                                            NM_COMPAT_DEVICE_PARENT, parent,
	                                            NULL);
	if (self) {
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_HW_ADDRESS, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_PERMANENT_HW_ADDRESS, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_MODE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_BITRATE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_WIFI_CAPABILITIES, G_CALLBACK (prop_reemit_cb), self);

		g_signal_connect (parent, "access-point-added", G_CALLBACK (ap_added_cb), self);
		g_signal_connect (parent, "access-point-removed", G_CALLBACK (ap_removed_cb), self);
	}

	return self;
}

static void
nm_compat_device_wifi_init (NMCompatDeviceWifi *self)
{
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMCompatDeviceWifi *self = NM_COMPAT_DEVICE_WIFI (object);
//	NMCompatDeviceWifiPrivate *priv = NM_COMPAT_DEVICE_WIFI_GET_PRIVATE (self);
//	char *str;
//	guint32 u;
	NMDevice *parent;

	parent = nm_compat_device_get_parent (NM_COMPAT_DEVICE (self));
	if (parent)
		g_object_get_property (G_OBJECT (parent), pspec->name, value);

#if 0
	if (priv->parent == NULL)
		return;

	switch (prop_id) {
	case PROP_HW_ADDRESS:
		g_object_get (priv->parent, NM_DEVICE_WIFI_HW_ADDRESS, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_PERM_HW_ADDRESS:
		g_object_get (priv->parent, NM_DEVICE_WIFI_PERMANENT_HW_ADDRESS, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_MODE:
		g_object_get (priv->parent, NM_DEVICE_WIFI_MODE, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_BITRATE:
		g_object_get (priv->parent, NM_DEVICE_WIFI_BITRATE, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_CAPABILITIES:
		g_object_get (priv->parent, NM_DEVICE_WIFI_CAPABILITIES, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_ACTIVE_ACCESS_POINT:
		g_object_get (priv->parent, NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT, &str, NULL);
		g_value_take_boxed (value, str);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
#endif
}

static void
nm_compat_device_wifi_class_init (NMCompatDeviceWifiClass *compat_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (compat_class);

	g_type_class_add_private (compat_class, sizeof (NMCompatDeviceWifiPrivate));

	object_class->get_property = get_property;

	/* properties */
	g_object_class_install_property (object_class, PROP_HW_ADDRESS,
		g_param_spec_string (NM_DEVICE_WIFI_HW_ADDRESS,
		                     "Active MAC Address",
		                     "Currently set hardware MAC address",
		                     NULL,
		                     G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_PERM_HW_ADDRESS,
		g_param_spec_string (NM_DEVICE_WIFI_PERMANENT_HW_ADDRESS,
		                     "Permanent MAC Address",
		                     "Permanent hardware MAC address",
		                     NULL,
		                     G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_MODE,
		g_param_spec_uint (NM_DEVICE_WIFI_MODE,
		                   "Mode",
		                   "Mode",
		                   NM_802_11_MODE_UNKNOWN,
		                   NM_802_11_MODE_INFRA,
		                   NM_802_11_MODE_INFRA,
		                   G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_BITRATE,
		g_param_spec_uint (NM_DEVICE_WIFI_BITRATE,
		                   "Bitrate",
		                   "Bitrate",
		                   0, G_MAXUINT32, 0,
		                   G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_ACTIVE_ACCESS_POINT,
		g_param_spec_boxed (NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT,
		                    "Active access point",
		                    "Currently active access point",
		                    DBUS_TYPE_G_OBJECT_PATH,
		                    G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_CAPABILITIES,
		g_param_spec_uint (NM_DEVICE_WIFI_CAPABILITIES,
		                   "Wireless Capabilities",
		                   "Wireless Capabilities",
		                   0, G_MAXUINT32, NM_WIFI_DEVICE_CAP_NONE,
		                   G_PARAM_READABLE));

	/* Signals */
	signals[ACCESS_POINT_ADDED] =
		g_signal_new ("access-point-added",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[ACCESS_POINT_REMOVED] =
		g_signal_new ("access-point-removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatDeviceWifiClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (compat_class), &dbus_glib_nm_compat_device_wifi_object_info);
}

