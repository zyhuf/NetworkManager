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

#include "NetworkManager.h"
#include "nm-dbus-glib-types.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"
#include "nm-compat-vpn-connection.h"

G_DEFINE_TYPE (NMCompatVpnConnection, nm_compat_vpn_connection, NM_TYPE_COMPAT_ACTIVE_CONNECTION)

enum {
	PROP_0,
	PROP_VPN_STATE,
	PROP_BANNER,
	LAST_PROP
};

enum {
	PROPERTIES_CHANGED,
	VPN_STATE_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

#include "nm-compat-vpn-connection-glue.h"

/*************************************************************************/

static void
prop_reemit_cb (GObject *object, GParamSpec *pspec, GObject *self)
{
	g_object_notify (self, pspec->name);
}

static void
vpn_state_changed_cb (NMVPNConnection *vpn,
                      NMVPNConnectionState state,
	                  NMVPNConnectionStateReason reason,
	                  GObject *self)
{
	g_signal_emit (self, signals[VPN_STATE_CHANGED], 0, state, reason);
}

/*************************************************************************/

NMCompatVpnConnection *
nm_compat_vpn_connection_new (NMVPNConnection *parent, DBusGConnection *bus)
{
	NMCompatVpnConnection *self;

	self = (NMCompatVpnConnection *) g_object_new (NM_TYPE_COMPAT_VPN_CONNECTION,
	                                               "parent", parent,
	                                               NULL);
	if (self) {
		g_signal_connect (parent, "notify::" NM_VPN_CONNECTION_VPN_STATE, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "notify::" NM_VPN_CONNECTION_BANNER, G_CALLBACK (prop_reemit_cb), self);
		g_signal_connect (parent, "vpn-state-changed", G_CALLBACK (vpn_state_changed_cb), self);
		nm_compat_active_connection_export (NM_COMPAT_ACTIVE_CONNECTION (self), bus);
	}

	return self;
}

static void
nm_compat_vpn_connection_init (NMCompatVpnConnection *self)
{
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	GObject *parent = nm_compat_active_connection_get_parent (NM_COMPAT_ACTIVE_CONNECTION (object));

	if (parent)
		g_object_get_property (parent, pspec->name, value);
}

static NMConnection *
get_connection (NMCompatActiveConnection *compat, GObject *parent)
{
	/* Parent should be an NMVPNConnection */
	return nm_vpn_connection_get_connection (NM_VPN_CONNECTION (parent));
}

static NMDevice *
get_device (NMCompatActiveConnection *compat, GObject *parent)
{
	/* Parent should be an NMVPNConnection */
	return nm_vpn_connection_get_parent_device (NM_VPN_CONNECTION (parent));
}

static void
nm_compat_vpn_connection_class_init (NMCompatVpnConnectionClass *compat_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (compat_class);
	NMCompatActiveConnectionClass *ac_class = NM_COMPAT_ACTIVE_CONNECTION_CLASS (compat_class);

	ac_class->get_connection = get_connection;
	ac_class->get_device = get_device;
	object_class->get_property = get_property;

	/* properties */
	g_object_class_install_property (object_class, PROP_VPN_STATE,
		g_param_spec_uint (NM_VPN_CONNECTION_VPN_STATE,
		                   "VpnState",
		                   "Current VPN state",
		                   NM_VPN_CONNECTION_STATE_UNKNOWN,
		                   NM_VPN_CONNECTION_STATE_DISCONNECTED,
		                   NM_VPN_CONNECTION_STATE_UNKNOWN,
		                   G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_BANNER,
		g_param_spec_string (NM_VPN_CONNECTION_BANNER,
		                     "Banner",
		                     "Login Banner",
		                     NULL,
		                     G_PARAM_READABLE));

	/* signals */
	signals[VPN_STATE_CHANGED] =
		g_signal_new ("vpn-state-changed",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    0, NULL, NULL,
				    _nm_marshal_VOID__UINT_UINT,
				    G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	signals[PROPERTIES_CHANGED] = 
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMCompatVpnConnectionClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (object_class),
									 &dbus_glib_nm_compat_vpn_connection_object_info);
}

