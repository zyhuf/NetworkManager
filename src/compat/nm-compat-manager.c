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
#include "nm-compat-manager.h"
#include "nm-dbus-glib-types.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"
#include "nm-compat-device.h"
#include "nm-compat-active-connection.h"

G_DEFINE_TYPE (NMCompatManager, nm_compat_manager, G_TYPE_OBJECT)

#define NM_COMPAT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_COMPAT_MANAGER, NMCompatManagerPrivate))

typedef struct {
	gboolean disposed;

	NMManager *manager;
} NMCompatManagerPrivate;


enum {
	PROP_0,
	PROP_VERSION,
	PROP_STATE,
	PROP_NETWORKING_ENABLED,
	PROP_WIRELESS_ENABLED,
	PROP_WIRELESS_HARDWARE_ENABLED,
	PROP_WWAN_ENABLED,
	PROP_WWAN_HARDWARE_ENABLED,
	PROP_ACTIVE_CONNECTIONS,

	LAST_PROP
};

enum {
	DEVICE_ADDED,
	DEVICE_REMOVED,
	STATE_CHANGED,
	PROPERTIES_CHANGED,

	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static gboolean impl_compat_manager_get_devices (NMCompatManager *self,
                                                 GPtrArray **devices,
                                                 GError **err);

static void impl_compat_manager_activate_connection (NMCompatManager *self,
                                                     const char *service_name,
                                                     const char *connection_path,
                                                     const char *device_path,
                                                     const char *specific_object_path,
                                                     DBusGMethodInvocation *context);

static void impl_compat_manager_deactivate_connection (NMCompatManager *self,
                                                       const char *active_path,
                                                       DBusGMethodInvocation *context);

static void impl_compat_manager_sleep (NMCompatManager *self,
                                       gboolean do_sleep,
                                       DBusGMethodInvocation *context);

static void impl_compat_manager_enable (NMCompatManager *self,
                                        gboolean enable,
                                        DBusGMethodInvocation *context);

static gboolean impl_compat_manager_get_state (NMCompatManager *self,
                                               guint32 *state,
                                               GError **err);

#include "nm-compat-manager-glue.h"

/*************************************************************************/

static gboolean
impl_compat_manager_get_devices (NMCompatManager *self,
                                 GPtrArray **devices,
                                 GError **err)
{
	NMCompatManagerPrivate *priv = NM_COMPAT_MANAGER_GET_PRIVATE (self);
	GSList *list, *iter;

	list = nm_manager_get_devices (priv->manager);
	*devices = g_ptr_array_sized_new (g_slist_length (list));
	for (iter = list; iter; iter = iter->next) {
		NMCompatDevice *compat = nm_device_get_compat (NM_DEVICE (iter->data));

		g_ptr_array_add (*devices, g_strdup (nm_compat_device_get_path (compat)));
	}
	return TRUE;
}

static void
impl_compat_manager_enable (NMCompatManager *self,
                            gboolean enable,
                            DBusGMethodInvocation *context)
{
	nm_manager_compat_enable (NM_COMPAT_MANAGER_GET_PRIVATE (self)->manager, enable, context);
}

static void
impl_compat_manager_sleep (NMCompatManager *self,
                            gboolean enable,
                            DBusGMethodInvocation *context)
{
	nm_manager_compat_enable (NM_COMPAT_MANAGER_GET_PRIVATE (self)->manager, enable, context);
}

#define NM_DBUS_SERVICE_USER_SETTINGS "org.freedesktop.NetworkManagerUserSettings"

static void
impl_compat_manager_activate_connection (NMCompatManager *self,
                                         const char *service_name,
                                         const char *connection_path,
                                         const char *device_path,
                                         const char *specific_object_path,
                                         DBusGMethodInvocation *context)
{
	nm_manager_compat_activate_connection (NM_COMPAT_MANAGER_GET_PRIVATE (self)->manager,
	                                       (g_strcmp0 (service_name, NM_DBUS_SERVICE_USER_SETTINGS) == 0) ? TRUE : FALSE,
                                           connection_path,
                                           device_path,
                                           specific_object_path,
                                           context);
}

static void
impl_compat_manager_deactivate_connection (NMCompatManager *self,
                                           const char *active_path,
                                           DBusGMethodInvocation *context)
{
	nm_manager_compat_deactivate_connection (NM_COMPAT_MANAGER_GET_PRIVATE (self)->manager,
	                                         active_path,
	                                         context);
}

#define NM_OLD_STATE_UNKNOWN      0
#define NM_OLD_STATE_ASLEEP       1
#define NM_OLD_STATE_CONNECTING   2
#define NM_OLD_STATE_CONNECTED    3
#define NM_OLD_STATE_DISCONNECTED 4

static guint32
new_state_to_old (NMState state)
{
	switch (state) {
	case NM_STATE_ASLEEP:
		return NM_OLD_STATE_ASLEEP;
	case NM_STATE_DISCONNECTED:
	case NM_STATE_DISCONNECTING:
		return NM_OLD_STATE_DISCONNECTED;
	case NM_STATE_CONNECTING:
		return NM_OLD_STATE_CONNECTING;
	case NM_STATE_CONNECTED_LOCAL:
	case NM_STATE_CONNECTED_SITE:
	case NM_STATE_CONNECTED_GLOBAL:
		return NM_OLD_STATE_CONNECTED;
	default:
		return NM_OLD_STATE_UNKNOWN;
	}
	g_assert_not_reached ();
}

static gboolean
impl_compat_manager_get_state (NMCompatManager *self,
                               guint32 *state,
                               GError **err)
{
	NMCompatManagerPrivate *priv = NM_COMPAT_MANAGER_GET_PRIVATE (self);

	*state = new_state_to_old (nm_manager_get_state (priv->manager));
	return TRUE;
}

static void
prop_reemit_cb (GObject *object, GParamSpec *pspec, NMCompatManager *self)
{
	g_object_notify (G_OBJECT (self), pspec->name);
}

static void
state_changed_cb (NMManager *parent, NMState state, NMCompatManager *self)
{
	g_signal_emit (self, signals[STATE_CHANGED], 0, new_state_to_old (state));
}

static void
device_added_cb (NMManager *parent, NMDevice *device, NMCompatManager *self)
{
	NMCompatDevice *compat = nm_device_get_compat (device);

	g_signal_emit (self, signals[DEVICE_ADDED], 0, compat);
}

static void
device_removed_cb (NMManager *parent, NMDevice *device, NMCompatManager *self)
{
	NMCompatDevice *compat = nm_device_get_compat (device);

	g_signal_emit (self, signals[DEVICE_REMOVED], 0, compat);
}

/*************************************************************************/

NMCompatManager *
nm_compat_manager_new (NMManager *parent, DBusGConnection *bus)
{
	NMCompatManager *self;

	self = (NMCompatManager *) g_object_new (NM_TYPE_COMPAT_MANAGER, NULL);
	if (self) {
		NM_COMPAT_MANAGER_GET_PRIVATE (self)->manager = parent;
		g_signal_connect (parent, "notify::" NM_MANAGER_STATE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_NETWORKING_ENABLED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_WIRELESS_ENABLED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_WIRELESS_HARDWARE_ENABLED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_WWAN_ENABLED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_WWAN_HARDWARE_ENABLED, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_MANAGER_ACTIVE_CONNECTIONS, G_CALLBACK (prop_reemit_cb), self);

		g_signal_connect (parent, "state-changed", G_CALLBACK (state_changed_cb), self);
		g_signal_connect (parent, "device-added", G_CALLBACK (device_added_cb), self);
		g_signal_connect (parent, "device-removed", G_CALLBACK (device_removed_cb), self);

		dbus_g_connection_register_g_object (bus,
			                                 "/org/freedesktop/NetworkManagerCompat",
			                                 G_OBJECT (self));
	}

	return self;
}

static void
nm_compat_manager_init (NMCompatManager *self)
{
}

static void
set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec)
{
	NMCompatManager *self = NM_COMPAT_MANAGER (object);
	NMCompatManagerPrivate *priv = NM_COMPAT_MANAGER_GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_WIRELESS_ENABLED:
		g_object_set (G_OBJECT (priv->manager),
		              NM_MANAGER_WIRELESS_ENABLED,
		              g_value_get_boolean (value),
		              NULL);
		break;
	case PROP_WWAN_ENABLED:
		g_object_set (G_OBJECT (priv->manager),
		              NM_MANAGER_WWAN_ENABLED,
		              g_value_get_boolean (value),
		              NULL);
		break;
	default:
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMCompatManager *self = NM_COMPAT_MANAGER (object);
	NMCompatManagerPrivate *priv = NM_COMPAT_MANAGER_GET_PRIVATE (self);
	NMState state = NM_STATE_UNKNOWN;
	gboolean b = FALSE;
	GSList *iter, *list;
	GPtrArray *active;

	switch (prop_id) {
	case PROP_VERSION:
		g_value_set_string (value, VERSION);
		break;
	case PROP_STATE:
		g_object_get (priv->manager, NM_MANAGER_STATE, &state, NULL);
		g_value_set_uint (value, new_state_to_old (state));
		break;
	case PROP_NETWORKING_ENABLED:
		g_object_get (priv->manager, NM_MANAGER_NETWORKING_ENABLED, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	case PROP_WIRELESS_ENABLED:
		g_object_get (priv->manager, NM_MANAGER_WIRELESS_ENABLED, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	case PROP_WIRELESS_HARDWARE_ENABLED:
		g_object_get (priv->manager, NM_MANAGER_WIRELESS_HARDWARE_ENABLED, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	case PROP_WWAN_ENABLED:
		g_object_get (priv->manager, NM_MANAGER_WWAN_ENABLED, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	case PROP_WWAN_HARDWARE_ENABLED:
		g_object_get (priv->manager, NM_MANAGER_WWAN_HARDWARE_ENABLED, &b, NULL);
		g_value_set_boolean (value, b);
		break;
	case PROP_ACTIVE_CONNECTIONS:
		list = nm_manager_compat_get_active_connections (priv->manager);
		active = g_ptr_array_sized_new (g_slist_length (list));
		for (iter = list; iter; iter = g_slist_next (iter)) {
			NMCompatActiveConnection *compat;

			compat = nm_act_request_get_compat (NM_ACT_REQUEST (iter->data));
			g_ptr_array_add (active, g_strdup (nm_compat_active_connection_get_path (compat)));
		}
		g_value_take_boxed (value, active);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_compat_manager_class_init (NMCompatManagerClass *manager_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

	g_type_class_add_private (manager_class, sizeof (NMCompatManagerPrivate));

	object_class->set_property = set_property;
	object_class->get_property = get_property;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_VERSION,
		 g_param_spec_string (NM_MANAGER_VERSION,
		                      "Version",
		                      "NetworkManager version",
		                      NULL,
		                      G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_STATE,
		 g_param_spec_uint (NM_MANAGER_STATE,
		                    "State",
		                    "Current state",
		                    0, G_MAXUINT, 0,
		                    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NETWORKING_ENABLED,
		 g_param_spec_boolean (NM_MANAGER_NETWORKING_ENABLED,
		                       "NetworkingEnabled",
		                       "Is networking enabled",
		                       TRUE,
		                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_WIRELESS_ENABLED,
		 g_param_spec_boolean (NM_MANAGER_WIRELESS_ENABLED,
		                       "WirelessEnabled",
		                       "Is wireless enabled",
		                       TRUE,
		                       G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_WIRELESS_HARDWARE_ENABLED,
		 g_param_spec_boolean (NM_MANAGER_WIRELESS_HARDWARE_ENABLED,
		                       "WirelessHardwareEnabled",
		                       "RF kill state",
		                       TRUE,
		                       G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_WWAN_ENABLED,
		 g_param_spec_boolean (NM_MANAGER_WWAN_ENABLED,
		                       "WwanEnabled",
		                       "Is mobile broadband enabled",
		                       TRUE,
		                       G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_WWAN_HARDWARE_ENABLED,
		 g_param_spec_boolean (NM_MANAGER_WWAN_HARDWARE_ENABLED,
		                       "WwanHardwareEnabled",
		                       "Whether WWAN is disabled by a hardware switch or not",
		                       TRUE,
		                       G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_ACTIVE_CONNECTIONS,
		 g_param_spec_boxed (NM_MANAGER_ACTIVE_CONNECTIONS,
		                     "Active connections",
		                     "Active connections",
		                     DBUS_TYPE_G_ARRAY_OF_OBJECT_PATH,
		                     G_PARAM_READABLE));

	/* signals */
	signals[DEVICE_ADDED] =
		g_signal_new ("device-added",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[DEVICE_REMOVED] =
		g_signal_new ("device-removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__UINT,
		              G_TYPE_NONE, 1, G_TYPE_UINT);

	signals[PROPERTIES_CHANGED] =
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatManagerClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (manager_class), &dbus_glib_nm_compat_manager_object_info);
}

