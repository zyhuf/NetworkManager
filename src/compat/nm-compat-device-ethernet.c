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
#include "nm-compat-device-ethernet.h"
#include "nm-dbus-glib-types.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"

G_DEFINE_TYPE (NMCompatDeviceEthernet, nm_compat_device_ethernet, NM_TYPE_COMPAT_DEVICE)

#define NM_COMPAT_DEVICE_ETHERNET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                  NM_TYPE_COMPAT_DEVICE_ETHERNET, \
                                                  NMCompatDeviceEthernetPrivate))

typedef struct {
	gboolean disposed;
} NMCompatDeviceEthernetPrivate;

enum {
	PROP_0,
	PROP_HW_ADDRESS,
	PROP_PERM_HW_ADDRESS,
	PROP_SPEED,
	PROP_CARRIER,
	LAST_PROP
};

enum {
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

#include "nm-compat-device-ethernet-glue.h"

/*************************************************************************/

static void
prop_reemit_cb (GObject *object, GParamSpec *pspec, NMCompatDevice *self)
{
	g_object_notify (G_OBJECT (self), pspec->name);
}

NMCompatDeviceEthernet *
nm_compat_device_ethernet_new (NMDeviceEthernet *parent)
{
	NMCompatDeviceEthernet *self;

	self = (NMCompatDeviceEthernet *) g_object_new (NM_TYPE_COMPAT_DEVICE_ETHERNET,
	                                                NM_COMPAT_DEVICE_PARENT, parent,
	                                                NULL);
	if (self) {
		g_signal_connect (parent, "notify::" NM_DEVICE_ETHERNET_HW_ADDRESS, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_ETHERNET_PERMANENT_HW_ADDRESS, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_ETHERNET_SPEED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_DEVICE_ETHERNET_CARRIER, G_CALLBACK (prop_reemit_cb), self);
	}

	return self;
}

static void
nm_compat_device_ethernet_init (NMCompatDeviceEthernet *self)
{
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMCompatDeviceEthernet *self = NM_COMPAT_DEVICE_ETHERNET (object);
//	NMCompatDeviceEthernetPrivate *priv = NM_COMPAT_DEVICE_ETHERNET_GET_PRIVATE (self);
	NMDevice *parent;
//	char *str;
//	guint32 u;
//	gboolean b;

	parent = nm_compat_device_get_parent (NM_COMPAT_DEVICE (self));
	if (parent)
		g_object_get_property (G_OBJECT (parent), pspec->name, value);

#if 0
	switch (prop_id) {
	case PROP_HW_ADDRESS:
		g_object_get (priv->parent, NM_DEVICE_ETHERNET_HW_ADDRESS, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_PERM_HW_ADDRESS:
		g_object_get (priv->parent, NM_DEVICE_ETHERNET_PERMANENT_HW_ADDRESS, &str, NULL);
		g_value_take_string (value, str);
		break;
	case PROP_SPEED:
		g_object_get (priv->parent, NM_DEVICE_ETHERNET_SPEED, &u, NULL);
		g_value_set_uint (value, u);
		break;
	case PROP_CARRIER:
		g_object_get (priv->parent, NM_DEVICE_ETHERNET_CARRIER, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
#endif
}

static void
nm_compat_device_ethernet_class_init (NMCompatDeviceEthernetClass *compat_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (compat_class);

	g_type_class_add_private (compat_class, sizeof (NMCompatDeviceEthernetPrivate));

	object_class->get_property = get_property;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_HW_ADDRESS,
		 g_param_spec_string (NM_DEVICE_ETHERNET_HW_ADDRESS,
							  "Active MAC Address",
							  "Currently set hardware MAC address",
							  NULL,
							  G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_PERM_HW_ADDRESS,
		 g_param_spec_string (NM_DEVICE_ETHERNET_PERMANENT_HW_ADDRESS,
							  "Permanent MAC Address",
							  "Permanent hardware MAC address",
							  NULL,
							  G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_SPEED,
		 g_param_spec_uint (NM_DEVICE_ETHERNET_SPEED,
						   "Speed",
						   "Speed",
						   0, G_MAXUINT32, 0,
						   G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_CARRIER,
		 g_param_spec_boolean (NM_DEVICE_ETHERNET_CARRIER,
							   "Carrier",
							   "Carrier",
							   FALSE,
							   G_PARAM_READABLE));

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatDeviceEthernetClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (compat_class), &dbus_glib_nm_compat_device_ethernet_object_info);
}

