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
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-ovsdb.h"

#include <string.h>
#include <jansson.h>
#include <gmodule.h>
#include <gio/gunixsocketaddress.h>

#include "devices/nm-device.h"
//#include "nm-device-openvswitch.h"
#include "platform/nm-platform.h"
#include "nm-core-internal.h"

/*****************************************************************************/

typedef struct {
	char *name;
	char *connection_uuid;
	GPtrArray *interfaces;          /* interface uuids */
} OpenvswitchPort;

typedef struct {
	char *name;
	char *connection_uuid;
	GPtrArray *ports;               /* port uuids */
} OpenvswitchBridge;

typedef struct {
	char *name;
	char *type;
	char *connection_uuid;
} OpenvswitchInterface;

/*****************************************************************************/

enum {
	DEVICE_ADDED,
	DEVICE_REMOVED,
	DEVICE_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	GSocketClient *client;
	GSocketConnection *conn;
	GCancellable *cancellable;
	char buf[4096];                 /* Input buffer */
	size_t bufp;                    /* Last decoded byte in the input buffer. */
	GString *input;                 /* JSON stream waiting for decoding. */
	GString *output;                /* JSON stream to be sent. */
	gint64 seq;
	GArray *calls;                  /* Method calls waiting for a response. */
	GHashTable *interfaces;         /* interface uuid => OpenvswitchInterface */
	GHashTable *ports;              /* port uuid => OpenvswitchPort */
	GHashTable *bridges;            /* bridge uuid => OpenvswitchBridge */
	const char *db_uuid;
} NMOvsdbPrivate;

struct _NMOvsdb {
	GObject parent;
	NMOvsdbPrivate _priv;
};

struct _NMOvsdbClass {
	GObjectClass parent;
};

G_DEFINE_TYPE (NMOvsdb, nm_ovsdb, G_TYPE_OBJECT)

#define NM_OVSDB_GET_PRIVATE(self) _NM_GET_PRIVATE (self, NMOvsdb, NM_IS_OVSDB)

#define _NMLOG_DOMAIN      LOGD_DEVICE
#define _NMLOG(level, ...) __NMLOG_DEFAULT (level, _NMLOG_DOMAIN, "ovsdb", __VA_ARGS__)

// XXX ///////
#undef _LOGT
#define _LOGT _LOGW

NM_DEFINE_SINGLETON_GETTER (NMOvsdb, nm_ovsdb_get, NM_TYPE_OVSDB);

/*****************************************************************************/

static void ovsdb_try_connect (NMOvsdb *self);
static void ovsdb_disconnect (NMOvsdb *self);
static void ovsdb_read (NMOvsdb *self);
static void ovsdb_write (NMOvsdb *self);
static void ovsdb_next_command (NMOvsdb *self);

/*****************************************************************************/

/* ovsdb command abstraction. */

typedef void (*OvsdbMethodCallback) (NMOvsdb *self, json_t *response,
                                     GError *error, gpointer user_data);

typedef struct {
	gint64 id;
#define COMMAND_PENDING -1                      /* id not yet assigned */
	OvsdbMethodCallback callback;
	gpointer user_data;
	NMOvsdbCommand command;
	NMConnection *bridge;
	NMConnection *port;
	NMConnection *interface;
} OvsdbMethodCall;

static void
_call_trace (const char *comment, OvsdbMethodCall *call, json_t *msg)
{
#ifdef NM_MORE_LOGGING
	const char *op = NULL;
	char *str = NULL;

	switch (call->command) {
	case NM_OVSDB_MONITOR:
		op = "monitor";
		break;
	case NM_OVSDB_ADD_BR:
		op = "add-br";
		break;
	case NM_OVSDB_DEL_BR:
		op = "del-br";
		break;
	case NM_OVSDB_ADD_IFACE:
		op = "add-interface";
		break;
	case NM_OVSDB_DEL_IFACE:
		op = "del-interface";
		break;
	}

	if (msg)
		str = json_dumps (msg, 0);

	_LOGT ("%s: %s%s%s%s%s%s%s%s%s", comment, op,
	       call->bridge ? " bridge=" : "",
	       call->bridge ? nm_connection_get_interface_name (call->bridge) : "",
	       call->port ? " port=" : "",
	       call->port ? nm_connection_get_interface_name (call->port) : "",
	       call->interface ? " interface=" : "",
	       call->interface ? nm_connection_get_interface_name (call->interface) : "",
	       msg ? ": " : "",
	       msg ? str : "");

	if (msg)
		g_free (str);

	g_return_if_fail (op);
#endif
}

/**
 * ovsdb_call_method:
 *
 * Queues the ovsdb command. Eventually fires the command right away if
 * there's no command pending completion.
 */
static void
ovsdb_call_method (NMOvsdb *self, NMOvsdbCommand command,
                   NMConnection *bridge, NMConnection *port, NMConnection *interface,
                   OvsdbMethodCallback callback, gpointer user_data)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	OvsdbMethodCall *call;

	/* Ensure we're not unsynchronized before we queue the method call. */
	ovsdb_try_connect (self);

	g_array_set_size (priv->calls, priv->calls->len + 1);
	call = &g_array_index (priv->calls, OvsdbMethodCall, priv->calls->len - 1);
	call->id = COMMAND_PENDING;
	call->command = command;
	if (bridge)
		call->bridge = nm_simple_connection_new_clone (bridge);
	if (port)
		call->port = nm_simple_connection_new_clone (port);
	if (interface)
		call->interface = nm_simple_connection_new_clone (interface);
	call->callback = callback;
	call->user_data = user_data;

	_call_trace ("enqueue", call, NULL);

	ovsdb_next_command (self);
}

/*****************************************************************************/

/* Create and process the JSON-RPC messages from ovsdb. */

/**
 * _fill_bridges:
 *
 * Put set of all bridges into @items and all but @exclude_bridge into
 * @new_items. The array with the ommited element is useful for replacement
 * or deletion while the full array is good for ensuring the database is
 * in the state we expect it to be prior to the transaction.
 */
static void
_fill_bridges (NMOvsdb *self, NMConnection *exclude_bridge,
               json_t **items, json_t **new_items)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GHashTableIter iter;
	char *bridge_uuid;
	OpenvswitchBridge *ovs_bridge;

	*items = json_array ();
	*new_items = json_array ();

	g_hash_table_iter_init (&iter, priv->bridges);
	while (g_hash_table_iter_next (&iter, (gpointer) &bridge_uuid, (gpointer) &ovs_bridge)) {
		json_array_append_new (*items, json_pack ("[s,s]", "uuid", bridge_uuid));
		if (   g_strcmp0 (nm_connection_get_interface_name (exclude_bridge),ovs_bridge->name) == 0
		    && ovs_bridge->connection_uuid != NULL)
			continue;
		json_array_append_new (*new_items, json_pack ("[s,s]", "uuid", bridge_uuid));
	}
}

/**
 * _expect_bridges:
 *
 * Return a command that will fail the transaction if the actual set of
 * bridges doesn't match @bridges. This is a way of detecting race conditions
 * with other ovsdb clients that might be adding or removing bridges
 * at the same time.
 */
static json_t *
_expect_bridges (json_t *bridges, const char *db_uuid)
{
	return json_pack ("{s:s, s:s, s:i, s:[s], s:s, s:[{s:[s, o]}], s:[[s, s, [s, s]]]}",
	                  "op", "wait", "table", "Open_vSwitch",
	                  "timeout", 0, "columns", "bridges",
	                  "until", "==", "rows", "bridges", "set", bridges,
	                  "where", "_uuid", "==", "uuid", db_uuid);
}

/**
 * _set_bridges:
 *
 * Return a command that will update the list of bridges in @db_uuid
 * database to @bridges.
 */
static json_t *
_set_bridges (const json_t *bridges, const char *db_uuid)
{
	return json_pack ("{s:s, s:s, s:{s:[s, o]}, s:[[s, s, [s, s]]]}",
	                  "op", "update", "table", "Open_vSwitch",
	                  "row", "bridges", "set", bridges,
	                  "where", "_uuid", "==", "uuid", db_uuid);
}

/*
 * _fill_ports:
 *
 * Put set of all ports of @bridge into @items and all but @exclude_port into
 * @new_items.
 *
 * Returns: %TRUE if the specified port was actually seen, helping us to decide
 *          whether we need to put an itnerface into a new one or update the
 *          existing one.
 */
static gboolean
_fill_ports (NMOvsdb *self,
             NMConnection *bridge, NMConnection *exclude_port,
             json_t **items, json_t **new_items)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GHashTableIter iter;
	char *bridge_uuid;
	char *port_uuid;
	OpenvswitchBridge *ovs_bridge;
	OpenvswitchPort *ovs_port;
	gboolean found = FALSE;
	int i;

	*items = json_array ();
	*new_items = json_array ();

	g_hash_table_iter_init (&iter, priv->bridges);
	while (g_hash_table_iter_next (&iter, (gpointer) &bridge_uuid, (gpointer) &ovs_bridge)) {
		if (g_strcmp0 (ovs_bridge->name, nm_connection_get_interface_name (bridge)) != 0)
			continue;
		for (i = 0; i < ovs_bridge->ports->len; i++) {
			port_uuid = g_ptr_array_index (ovs_bridge->ports, i);
			json_array_append_new (*items, json_pack ("[s,s]", "uuid", port_uuid));

			ovs_port = g_hash_table_lookup (priv->ports, port_uuid);
			if (!ovs_port)
				continue;
			if (   g_strcmp0 (nm_connection_get_interface_name (exclude_port), ovs_port->name) == 0
			    && g_strcmp0 (nm_connection_get_uuid (exclude_port), ovs_port->connection_uuid) == 0) {
				found = TRUE;
				continue;
			}
			json_array_append_new (*new_items, json_pack ("[s,s]", "uuid", port_uuid));
		}
	}

	return found;
}

/**
 * _expect_ports:
 *
 * Return a command that will fail the transaction if the actual set of
 * ports in @bridge doesn't match @ports. This is a way of detecting
 * race conditions with other ovsdb clients that might be adding or removing
 * bridge ports at the same time.
 */
static json_t *
_expect_ports (NMConnection *bridge, const json_t *ports)
{
	return json_pack ("{s:s, s:s, s:i, s:[s], s:s, s:[{s:[s, o]}], s:[[s, s, s]]}",
	                  "op", "wait", "table", "Bridge",
	                  "timeout", 0, "columns", "ports",
	                  "until", "==", "rows", "ports", "set", ports,
	                  "where", "name", "==", nm_connection_get_interface_name (bridge));
}

/**
 * _set_ports:
 *
 * Return a command that will update the list of ports of @bridge
 * to @ports.
 */
static json_t *
_set_ports (NMConnection *bridge, const json_t *ports)
{
	return json_pack ("{s:s, s:s, s:{s:[s, o]}, s:[[s, s, s]]}",
	                  "op", "update", "table", "Bridge",
	                  "row", "ports", "set", ports,
	                  "where", "name", "==", nm_connection_get_interface_name (bridge));
}

/*
 * _fill_interfaces:
 *
 * Put set of all interfaces of @port into @items and all but
 * @exclude_interface into @new_items.
 */
static void
_fill_interfaces (NMOvsdb *self,
                  NMConnection *port, NMConnection *exclude_interface,
                  json_t **items, json_t **new_items)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GHashTableIter iter;
	char *port_uuid;
	char *interface_uuid;
	OpenvswitchPort *ovs_port;
	OpenvswitchInterface *ovs_interface;
	int i;

	*items = json_array ();
	*new_items = json_array ();

	g_hash_table_iter_init (&iter, priv->ports);
	while (g_hash_table_iter_next (&iter, (gpointer) &port_uuid, (gpointer) &ovs_port)) {
		if (g_strcmp0 (ovs_port->name, nm_connection_get_interface_name (port)) != 0)
			continue;
		for (i = 0; i < ovs_port->interfaces->len; i++) {
			interface_uuid = g_ptr_array_index (ovs_port->interfaces, i);
			json_array_append_new (*items, json_pack ("[s,s]", "uuid", interface_uuid));

			ovs_interface = g_hash_table_lookup (priv->interfaces, interface_uuid);
			if (!ovs_interface)
				continue;
			if (   g_strcmp0 (nm_connection_get_interface_name (exclude_interface), ovs_interface->name) == 0
			    && g_strcmp0 (nm_connection_get_uuid (exclude_interface), ovs_interface->connection_uuid) == 0)
				continue;
			json_array_append_new (*new_items, json_pack ("[s,s]", "uuid", interface_uuid));
		}
	}
}

/**
 * _expect_interfaces:
 *
 * Return a command that will fail the transaction if the actual set of
 * interfaces in @port doesn't match @interfaces. This is a way of detecting
 * race conditions with other ovsdb clients that might be adding or removing
 * port interfaces at the same time.
 */
static json_t *
_expect_interfaces (NMConnection *port, const json_t *interfaces)
{
	return json_pack ("{s:s, s:s, s:i, s:[s], s:s, s:[{s:[s, o]}], s:[[s, s, s]]}",
	                  "op", "wait", "table", "Port",
	                  "timeout", 0, "columns", "interfaces",
	                  "until", "==", "rows", "interfaces", "set", interfaces,
	                  "where", "name", "==", nm_connection_get_interface_name (port));
}

/**
 * _set_interfaces:
 *
 * Return a command that will update the list of interfaces of @port
 * to @interfaces.
 */
static json_t *
_set_interfaces (NMConnection *port, const json_t *interfaces)
{
	return json_pack ("{s:s, s:s, s:{s:[s, o]}, s:[[s, s, s]]}",
	                  "op", "update", "table", "Port",
	                  "row", "interfaces", "set", interfaces,
	                  "where", "name", "==", nm_connection_get_interface_name (port));
}

/**
 * _inc_next_cfg:
 *
 * Returns an mutate command that bumps next_cfg upon successful completion
 * of the transaction it is in.
 */
static json_t *
_inc_next_cfg (const char *db_uuid)
{
	return json_pack ("{s:s, s:s, s:[[s, s, i]], s:[[s, s, [s, s]]]}",
                          "op", "mutate", "table", "Open_vSwitch",
	                  "mutations", "next_cfg", "+=", 1,
	                  "where", "_uuid", "==", "uuid", db_uuid);
}

/**
 * _new_interface:
 *
 * Returns an commands that adds new interface from a given connection.
 * If the connection is of a bridge then an internal interface for the
 * bridge is added, otherwise it's a regular one.
 */
static json_t *
_new_interface (NMConnection *interface)
{
	const char *type;

	if (nm_connection_get_setting_ovs_bridge (interface))
		type = "internal";
	else
		type = "";

	return json_pack ("{s:s, s:s, s:{s:s, s:s, s:[s, [[s, s]]]}, s:s}",
	                  "op", "insert", "table", "Interface", "row",
	                  "name", nm_connection_get_interface_name (interface),
	                  "type", type,
	                  "external_ids", "map", "NM.connection.uuid", nm_connection_get_uuid (interface),
	                  "uuid-name", "rowIntf");
}

/**
 * _new_port:
 *
 * Returns an commands that adds new port from a given connection.
 */
static json_t *
_new_port (NMConnection *port)
{
	NMSettingOvsPort *s_ovs_port;
	const char *vlan_mode = NULL;
	guint tag = 0;
	const char *lacp = NULL;
	const char *bond_mode = NULL;
	guint bond_updelay = 0;
	guint bond_downdelay = 0;
	json_t *row;

	s_ovs_port = nm_connection_get_setting_ovs_port (port);

	row = json_object ();

	if (s_ovs_port) {
		vlan_mode = nm_setting_ovs_port_get_vlan_mode (s_ovs_port);
		tag = nm_setting_ovs_port_get_tag (s_ovs_port);
		lacp = nm_setting_ovs_port_get_lacp (s_ovs_port);
		bond_mode = nm_setting_ovs_port_get_bond_mode (s_ovs_port);
		bond_updelay = nm_setting_ovs_port_get_bond_updelay (s_ovs_port);
		bond_downdelay = nm_setting_ovs_port_get_bond_downdelay (s_ovs_port);
	}

	if (vlan_mode)
		json_object_set_new (row, "vlan_mode", json_string (vlan_mode));
	if (tag)
		json_object_set_new (row, "tag", json_integer (tag));
	if (lacp)
		json_object_set_new (row, "lacp", json_string (lacp));
	if (bond_mode)
		json_object_set_new (row, "bond_mode", json_string (bond_mode));
	if (bond_updelay)
		json_object_set_new (row, "bond_updelay", json_integer (bond_updelay));
	if (bond_downdelay)
		json_object_set_new (row, "bond_downdelay", json_integer (bond_downdelay));

	json_object_set_new (row, "name", json_string (nm_connection_get_interface_name (port)));
	json_object_set_new (row, "interfaces", json_pack ("[s, s]", "named-uuid", "rowIntf"));
	json_object_set_new (row, "external_ids",
		json_pack ("[s, [[s, s]]]", "map",
		           "NM.connection.uuid", nm_connection_get_uuid (port)));

	return json_pack ("{s:s, s:s, s:o, s:s}", "op", "insert", "table", "Port",
	                  "row", row, "uuid-name", "rowPort");
}

/**
 * _new_bridge:
 *
 * Returns an commands that adds new bridge from a given connection.
 */
static json_t *
_new_bridge (NMConnection *bridge)
{
	NMSettingOvsBridge *s_ovs_bridge;
	const char *fail_mode = NULL;
	gboolean mcast_snooping_enable = FALSE;
	gboolean rstp_enable = FALSE;
	gboolean stp_enable = FALSE;
	json_t *row;

	s_ovs_bridge = nm_connection_get_setting_ovs_bridge (bridge);

	row = json_object ();

	if (s_ovs_bridge) {
		fail_mode = nm_setting_ovs_bridge_get_fail_mode (s_ovs_bridge);
		mcast_snooping_enable = nm_setting_ovs_bridge_get_mcast_snooping_enable (s_ovs_bridge);
		rstp_enable = nm_setting_ovs_bridge_get_rstp_enable (s_ovs_bridge);
		stp_enable = nm_setting_ovs_bridge_get_stp_enable (s_ovs_bridge);
	}

	if (fail_mode)
		json_object_set_new (row, "fail_mode", json_string (fail_mode));
	if (mcast_snooping_enable)
		json_object_set_new (row, "mcast_snooping_enable", json_boolean (mcast_snooping_enable));
	if (rstp_enable)
		json_object_set_new (row, "rstp_enable", json_boolean (rstp_enable));
	if (stp_enable)
		json_object_set_new (row, "stp_enable", json_boolean (stp_enable));

	json_object_set_new (row, "name", json_string (nm_connection_get_interface_name (bridge)));
	json_object_set_new (row, "ports", json_pack ("[s, s]", "named-uuid", "rowPort"));
	json_object_set_new (row, "external_ids",
		json_pack ("[s, [[s, s]]]", "map",
		           "NM.connection.uuid", nm_connection_get_uuid (bridge)));

	return json_pack ("{s:s, s:s, s:o, s:s}", "op", "insert", "table", "Bridge",
	                  "row", row, "uuid-name", "rowBridge");
}

/**
 * ovsdb_next_command:
 *
 * Translates a higher level operation (add/remove bridge/port) to a RFC 7047
 * command serialized into JSON ands sends it over to the database.

 * Only called when no command is waiting for a response, since the serialized
 * command might depend on result of a previous one (add and remove need to
 * include an up to date bridge list in their transactions to rule out races).
 */
static void
ovsdb_next_command (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	OvsdbMethodCall *call = NULL;
	char *cmd;
	json_t *msg = NULL;
	json_t *items, *new_items;
	json_t *params;

	if (!priv->conn)
		return;
	if (!priv->calls->len)
		return;
	call = &g_array_index (priv->calls, OvsdbMethodCall, 0);
	if (call->id != COMMAND_PENDING)
		return;
	call->id = priv->seq++;

	switch (call->command) {
	case NM_OVSDB_MONITOR:
		msg = json_pack ("{s:i, s:s, s:[s, n, {"
		                 "  s:[{s:[s, s, s]}],"
		                 "  s:[{s:[s, s, s]}],"
		                 "  s:[{s:[s, s, s]}],"
		                 "  s:[{s:[]}]"
		                 "}]}",
		                 "id", call->id,
		                 "method", "monitor", "params", "Open_vSwitch",
		                 "Bridge", "columns", "name", "ports", "external_ids",
		                 "Port", "columns", "name", "interfaces", "external_ids",
		                 "Interface", "columns", "name", "type", "external_ids",
		                 "Open_vSwitch", "columns");
		break;
	case NM_OVSDB_ADD_BR:
		_fill_bridges (self, call->bridge, &items, &new_items);
		json_array_append_new (new_items, json_pack ("[s,s]", "named-uuid", "rowBridge"));

		msg = json_pack ("{s:i, s:s, s:[s, o, o, o, o, o, o]}",
		                 "id", call->id,
		                 "method", "transact", "params", "Open_vSwitch",
		                 _expect_bridges (items, priv->db_uuid),
		                 _set_bridges (new_items, priv->db_uuid),
		                 _inc_next_cfg (priv->db_uuid),
		                 _new_interface (call->interface),
		                 _new_port (call->port),
		                 _new_bridge (call->bridge));
		break;
	case NM_OVSDB_DEL_BR:
		_fill_bridges (self, call->bridge, &items, &new_items);

		msg = json_pack ("{s:i, s:s, s:[s,o,o,o]}",
		                 "id", call->id,
		                 "method", "transact", "params", "Open_vSwitch",
		                 _expect_bridges (items, priv->db_uuid),
		                 _set_bridges (new_items, priv->db_uuid),
		                 _inc_next_cfg (priv->db_uuid));
		break;
	case NM_OVSDB_ADD_IFACE:
		params = json_array ();
		json_array_append_new (params, json_string ("Open_vSwitch"));

		/* Insert the new interface. */
		json_array_append_new (params, _new_interface (call->interface));

		if (_fill_ports (self, call->bridge, call->port, &items, &new_items)) {
			/* The port exists, update it with the new interface. */
			json_decref (items);
			json_decref (new_items);
			_fill_interfaces (self, call->port, call->interface, &items, &new_items);
			json_array_append_new (new_items, json_pack ("[s,s]", "named-uuid", "rowIntf"));
			json_array_append_new (params, _expect_interfaces (call->port, items));
			json_array_append_new (params, _set_interfaces (call->port, new_items));
		} else {
		        /* Create a new port along with the interface. */
			json_array_append_new (params, _new_port (call->port));
			json_array_append_new (new_items, json_pack ("[s,s]", "named-uuid", "rowPort"));
			json_array_append_new (params, _expect_ports (call->bridge, items));
			json_array_append_new (params, _set_ports (call->bridge, new_items));
		}

		msg = json_pack ("{s:i, s:s, s:o}",
		                 "id", call->id,
		                 "method", "transact", "params", params);

		break;
	case NM_OVSDB_DEL_IFACE:
		params = json_array ();
		json_array_append_new (params, json_string ("Open_vSwitch"));

		_fill_interfaces (self, call->port, call->interface, &items, &new_items);
		if (json_array_size (new_items) == 0) {
			/* A port can't exist without interfaces, drop it altogether. */
			json_decref (items);
			json_decref (new_items);
			_fill_ports (self, call->bridge, call->port, &items, &new_items);
			json_array_append_new (params, _expect_ports (call->bridge, items));
			json_array_append_new (params, _set_ports (call->bridge, new_items));
		} else {
			/* Drop just the interface from the port. */
			json_array_append_new (params, _expect_interfaces (call->port, items));
			json_array_append_new (params, _set_interfaces (call->port, new_items));
		}

		msg = json_pack ("{s:i, s:s, s:o}",
		                 "id", call->id,
		                 "method", "transact", "params", params);
		break;
	}

	g_return_if_fail (msg);
	_call_trace ("send", call, msg);
	cmd = json_dumps (msg, 0);

	g_string_append (priv->output, cmd);
	json_decref (msg);
	free (cmd);

	ovsdb_write (self);
}

/**
 * _uuids_to_array:
 *
 * This tidies up the somewhat non-straightforward way ovsdb represents an array
 * of UUID elements. The single element is a tuple (called <atom> in RFC7047),
 *
 *   [ "uuid", "aa095ffb-e1f1-0fc4-8038-82c1ea7e4797" ]
 *
 * while the list of multiple UUIDs are turned into a set of such tuples ("atoms"):
 *
 *   [ "set", [ [ "uuid", "aa095ffb-e1f1-0fc4-8038-82c1ea7e4797" ],
 *              [ "uuid", "185c93f6-0b39-424e-8587-77d074aa7ce0" ], ... ] ]
 */
static void
_uuids_to_array (GPtrArray *array, const json_t *items)
{
	const char *key;
	json_t *value;
	size_t index = 0;
	json_t *set_value;
	size_t set_index;

	while (index < json_array_size (items)) {
		key = json_string_value (json_array_get (items, index));
		index++;
		value = json_array_get (items, index);
		index++;

		if (!value)
			return;

		if (g_strcmp0 (key, "uuid") == 0 && json_is_string (value)) {
			g_ptr_array_add (array, g_strdup (json_string_value (value)));
		} else if (g_strcmp0 (key, "set") == 0 && json_is_array (value)) {
			json_array_foreach (value, set_index, set_value) {
				_uuids_to_array (array, set_value);
			}
		}
	}
}

static char *
_connection_uuid_from_external_ids (json_t *external_ids)
{
	json_t *value;
	size_t index;

	if (g_strcmp0 ("map", json_string_value (json_array_get (external_ids, 0))) != 0)
		return NULL;

	json_array_foreach (json_array_get (external_ids, 1), index, value) {
		if (g_strcmp0 ("NM.connection.uuid", json_string_value (json_array_get (value, 0))) == 0)
			return g_strdup (json_string_value (json_array_get (value, 1)));
	}

	return NULL;
}

/**
 * ovsdb_got_update:
 *
 * Called when we've got an "update" method call (we asked for it with the monitor
 * command). We use it to maintain a consistent view of bridge list regardless of
 * whether the changes are done by us or externally.
 */
static void
ovsdb_got_update (NMOvsdb *self, json_t *msg)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	json_t *ovs = NULL;
	json_t *bridge = NULL;
	json_t *port = NULL;
	json_t *interface = NULL;
	json_t *items;
	json_t *external_ids;
	json_error_t json_error = { 0, };
	void *iter;
	const char *name;
	const char *key;
	const char *type;
	json_t *value;
	OpenvswitchBridge *ovs_bridge;
	OpenvswitchPort *ovs_port;
	OpenvswitchInterface *ovs_interface;

	if (json_unpack_ex (msg, &json_error, 0, "{s?:o, s?:o, s?:o, s?:o}",
	                    "Open_vSwitch", &ovs,
	                    "Bridge", &bridge,
	                    "Port", &port,
	                    "Interface", &interface) == -1) {
		/* This doesn't really have to be an error; the key might
		 * be missing if there really are no bridges present. */
		_LOGD ("Bad update: %s", json_error.text);
	}

	if (ovs) {
		iter = json_object_iter (ovs);
		priv->db_uuid = g_strdup (iter ? json_object_iter_key (iter) : NULL);
	}

	/* Interfaces */
	json_object_foreach (interface, key, value) {
		gboolean old = FALSE;
		gboolean new = FALSE;

		if (json_unpack (value, "{s:{}}", "old") == 0)
			old = TRUE;

		if (json_unpack (value, "{s:{s:s, s:s, s:o}}", "new",
		                 "name", &name,
		                 "type", &type,
		                 "external_ids", &external_ids) == 0)
			new = TRUE;

		if (old) {
			ovs_interface = g_hash_table_lookup (priv->interfaces, key);
			if (g_strcmp0 (ovs_interface->name, name) != 0) {
				old = FALSE;
				_LOGT ("removed an '%s' interface: %s%s%s",
				       ovs_interface->type, ovs_interface->name,
				       ovs_interface->connection_uuid ? ", " : "",
				       ovs_interface->connection_uuid ? ovs_interface->connection_uuid : "");
				if (g_strcmp0 (type, "internal") == 0) {
					/* Currently the factory only creates NMDevices for
					 * internal interfaces. Ignore the rest. */
					g_signal_emit (self, signals[DEVICE_REMOVED], 0,
					               "ovs-interface", ovs_interface->name);
				}
			}
			g_hash_table_remove (priv->interfaces, key);
		}

		if (new) {
			ovs_interface = g_slice_new (OpenvswitchInterface);
			ovs_interface->name = g_strdup (name);
			ovs_interface->type = g_strdup (type);
			ovs_interface->connection_uuid = _connection_uuid_from_external_ids (external_ids);
			if (old) {
				_LOGT ("changed an '%s' interface: %s%s%s", type, ovs_interface->name,
				       ovs_interface->connection_uuid ? ", " : "",
				       ovs_interface->connection_uuid ? ovs_interface->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_CHANGED], 0,
				               "ovs-interface", ovs_interface->name);
			} else {
				_LOGT ("added an '%s' interface: %s%s%s",
				       ovs_interface->type, ovs_interface->name,
				       ovs_interface->connection_uuid ? ", " : "",
				       ovs_interface->connection_uuid ? ovs_interface->connection_uuid : "");
				if (g_strcmp0 (ovs_interface->type, "internal") == 0) {
					/* Currently the factory only creates NMDevices for
					 * internal interfaces. Ignore the rest. */
					g_signal_emit (self, signals[DEVICE_ADDED], 0,
					               "ovs-interface", ovs_interface->name);
				}
			}
			g_hash_table_insert (priv->interfaces, g_strdup (key), ovs_interface);
		}
	}

	/* Ports */
	json_object_foreach (port, key, value) {
		gboolean old = FALSE;
		gboolean new = FALSE;

		if (json_unpack (value, "{s:{}}", "old") == 0)
			old = TRUE;

		if (json_unpack (value, "{s:{s:s, s:o, s:o}}", "new",
		                 "name", &name,
		                 "external_ids", &external_ids,
		                 "interfaces", &items) == 0)
			new = TRUE;

		if (old) {
			ovs_port = g_hash_table_lookup (priv->ports, key);
			if (g_strcmp0 (ovs_port->name, name) != 0) {
				old = FALSE;
				_LOGT ("removed a port: %s%s%s", ovs_port->name,
				       ovs_port->connection_uuid ? ", " : "",
				       ovs_port->connection_uuid ? ovs_port->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_REMOVED], 0,
				               NM_SETTING_OVS_PORT_SETTING_NAME, ovs_port->name);
			}
			g_hash_table_remove (priv->ports, key);
		}

		if (new) {
			ovs_port = g_slice_new (OpenvswitchPort);
			ovs_port->name = g_strdup (name);
			ovs_port->connection_uuid = _connection_uuid_from_external_ids (external_ids);
			ovs_port->interfaces = g_ptr_array_new_with_free_func (g_free);
			_uuids_to_array (ovs_port->interfaces, items);
			if (old) {
				_LOGT ("changed a port: %s%s%s", ovs_port->name,
				       ovs_port->connection_uuid ? ", " : "",
				       ovs_port->connection_uuid ? ovs_port->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_CHANGED], 0,
				               NM_SETTING_OVS_PORT_SETTING_NAME, ovs_port->name);
			} else {
				_LOGT ("added a port: %s%s%s", ovs_port->name,
				       ovs_port->connection_uuid ? ", " : "",
				       ovs_port->connection_uuid ? ovs_port->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_ADDED], 0,
				               NM_SETTING_OVS_PORT_SETTING_NAME, ovs_port->name);
			}
			g_hash_table_insert (priv->ports, g_strdup (key), ovs_port);
		}
	}

	/* Bridges */
	json_object_foreach (bridge, key, value) {
		gboolean old = FALSE;
		gboolean new = FALSE;

		if (json_unpack (value, "{s:{}}", "old") == 0)
			old = TRUE;

		if (json_unpack (value, "{s:{s:s, s:o, s:o}}", "new",
		                 "name", &name,
		                 "external_ids", &external_ids,
		                 "ports", &items) == 0)
			new = TRUE;

		if (old) {
			ovs_bridge = g_hash_table_lookup (priv->bridges, key);
			if (g_strcmp0 (ovs_bridge->name, name) != 0) {
				old = FALSE;
				_LOGT ("removed a bridge: %s%s%s", ovs_bridge->name,
				       ovs_bridge->connection_uuid ? ", " : "",
				       ovs_bridge->connection_uuid ? ovs_bridge->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_REMOVED], 0,
				               NM_SETTING_OVS_BRIDGE_SETTING_NAME, ovs_bridge->name);
			}
			g_hash_table_remove (priv->bridges, key);
		}

		if (new) {
			ovs_bridge = g_slice_new (OpenvswitchBridge);
			ovs_bridge->name = g_strdup (name);
			ovs_bridge->connection_uuid = _connection_uuid_from_external_ids (external_ids);
			ovs_bridge->ports = g_ptr_array_new_with_free_func (g_free);
			_uuids_to_array (ovs_bridge->ports, items);
			if (old) {
				_LOGT ("changed a bridge: %s%s%s", ovs_bridge->name,
				       ovs_bridge->connection_uuid ? ", " : "",
				       ovs_bridge->connection_uuid ? ovs_bridge->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_CHANGED], 0,
				               NM_SETTING_OVS_BRIDGE_SETTING_NAME, ovs_bridge->name);
			} else {
				_LOGT ("added a bridge: %s%s%s", ovs_bridge->name,
				       ovs_bridge->connection_uuid ? ", " : "",
				       ovs_bridge->connection_uuid ? ovs_bridge->connection_uuid : "");
				g_signal_emit (self, signals[DEVICE_ADDED], 0,
				               NM_SETTING_OVS_BRIDGE_SETTING_NAME, ovs_bridge->name);
			}
			g_hash_table_insert (priv->bridges, g_strdup (key), ovs_bridge);
		}
	}

}

/**
 * ovsdb_got_echo:
 *
 * Only implemented because the specification mandates it. Actual ovsdb hasn't been
 * seen doing this.
 */
static void
ovsdb_got_echo (NMOvsdb *self, json_int_t id, json_t *data)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	json_t *msg;
	char *reply;
	gboolean output_was_empty;

	output_was_empty = priv->output->len == 0;

	msg = json_pack ("{s:I, s:O}", "id", id, "result", data);
	reply = json_dumps (msg, 0);
	g_string_append (priv->output, reply);
	json_decref (msg);
	free (reply);

	if (output_was_empty)
		ovsdb_write (self);
}

/**
 * ovsdb_got_msg::
 *
 * Called when when a complete JSON object was seen and unmarshalled.
 * Either finishes a method call or processes a method call.
 */
static void
ovsdb_got_msg (NMOvsdb *self, json_t *msg)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	json_error_t json_error = { 0, };
	json_t *json_id = NULL;
	gint64 id = -1;
	const char *method = NULL;
	json_t *params = NULL;
	json_t *result = NULL;
	json_t *error = NULL;
	OvsdbMethodCall *call = NULL;
	OvsdbMethodCallback callback;
	gpointer user_data;
	GError *local = NULL;

	if (json_unpack_ex (msg, &json_error, 0, "{s?:o, s?:s, s?:o, s?:o, s?:o}",
	                    "id", &json_id,
	                    "method", &method,
	                    "params", &params,
	                    "result", &result,
	                    "error", &error) == -1) {
		_LOGW ("couldn't grok the message: %s", json_error.text);
		ovsdb_disconnect (self);
		return;
	}

	if (json_is_number (json_id))
		id = json_integer_value (json_id);

	if (method) {
		/* It's a method call! */
		if (!params) {
			_LOGW ("a method call with no params: '%s'", method);
			ovsdb_disconnect (self);
			return;
		}

		if (g_strcmp0 (method, "update") == 0) {
			/* This is a update method call. */
			ovsdb_got_update (self, json_array_get (params, 1));
		} else if (g_strcmp0 (method, "echo") == 0) {
			/* This is an echo request. */
			ovsdb_got_echo (self, id, params);
		} else {
			_LOGW ("got an unknown method call: '%s'", method);
		}
		return;
	}

	if (id > -1) {
		/* This is a response to a method call. */
		if (!priv->calls->len) {
			_LOGE ("there are no queued calls expecting response %ld", id);
			ovsdb_disconnect (self);
			return;
		}
		call = &g_array_index (priv->calls, OvsdbMethodCall, 0);
		if (call->id != id) {
			_LOGE ("expected a response to call %ld, not %ld", call->id, id);
			ovsdb_disconnect (self);
			return;
		}
		/* Cool, we found a corresponsing call. Finish it. */

		_call_trace ("response", call, msg);

		if (!json_is_null (error)) {
			/* The response contains an error. */
			g_set_error (&local, G_IO_ERROR, G_IO_ERROR_FAILED,
			             "Error call to OVSDB returned an error: %s",
			              json_string_value (error));
		}

		callback = call->callback;
		user_data = call->user_data;
		g_array_remove_index (priv->calls, 0);
		callback (self, result, local, user_data);

		/* Don't progress further commands in case the callback hit an error
		 * and disconnected us. */
		if (!priv->conn)
			return;

		/* Now we're free to serialize and send the next command, if any. */
		ovsdb_next_command (self);

		return;
	}


	/* This is a message we are not interested in. */
	_LOGW ("got an unknown message, ignoring");
}

/*****************************************************************************/

/* Lower level marshalling and demarshalling of the JSON-RPC traffic on the
 * ovsdb socket. */

static size_t
_json_callback (void *buffer, size_t buflen, void *user_data)
{
	NMOvsdb *self = NM_OVSDB (user_data);
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);

	if (priv->bufp == priv->input->len) {
		/* No more bytes buffered for decoding. */
		return 0;
	}

	/* Pass one more byte to the JSON decoder. */
	*(char *)buffer = priv->input->str[priv->bufp];
	priv->bufp++;

	return (size_t)1;
}

/**
 * ovsdb_read_cb:
 *
 * Read out the data available from the ovsdb socket and try to deserialize
 * the JSON. If we see a complete object, pass it upwards to ovsdb_got_msg().
 */
static void
ovsdb_read_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	NMOvsdb *self = NM_OVSDB (user_data);
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GInputStream *stream = G_INPUT_STREAM (source_object);
	GError *error = NULL;
	gssize size;
	json_t *msg;
	json_error_t json_error = { 0, };

	size = g_input_stream_read_finish (stream, res, &error);
	if (size == -1) {
		_LOGW ("short read from ovsdb: %s", error->message);
		g_clear_error (&error);
		ovsdb_disconnect (self);
		return;
	}

	g_string_append_len (priv->input, priv->buf, size);
	do {
		priv->bufp = 0;
		/* The callback always eats up only up to a single byte. This makes
		 * it possible for us to identify complete JSON objects in spite of
		 * us not knowing the length in advance. */
		msg = json_load_callback (_json_callback, self, JSON_DISABLE_EOF_CHECK, &json_error);
		if (msg) {
			ovsdb_got_msg (self, msg);
			g_string_erase (priv->input, 0, priv->bufp);
		}
		json_decref (msg);
	} while (msg);

	if (!priv->conn)
		return;

	if (size)
		ovsdb_read (self);
}

static void
ovsdb_read (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);

	g_input_stream_read_async (g_io_stream_get_input_stream (G_IO_STREAM (priv->conn)),
		priv->buf, sizeof(priv->buf),
		G_PRIORITY_DEFAULT, NULL, ovsdb_read_cb, self);
}

static void
ovsdb_write_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GOutputStream *stream = G_OUTPUT_STREAM (source_object);
	NMOvsdb *self = NM_OVSDB (user_data);
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GError *error = NULL;
	gssize size;

	size = g_output_stream_write_finish (stream, res, &error);
	if (size == -1) {
		_LOGW ("short write to ovsdb: %s", error->message);
		g_clear_error (&error);
		ovsdb_disconnect (self);
		return;
	}

	if (!priv->conn)
		return;

	g_string_erase (priv->output, 0, size);

	ovsdb_write (self);
}

static void
ovsdb_write (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GOutputStream *stream;

	if (!priv->output->len)
		return;

	stream = g_io_stream_get_output_stream (G_IO_STREAM (priv->conn));
	if (g_output_stream_has_pending (stream))
		return;

	g_output_stream_write_async (stream,
	                             priv->output->str, priv->output->len,
	                             G_PRIORITY_DEFAULT, NULL, ovsdb_write_cb, self);
}
/*****************************************************************************/

/* Routines to maintain the ovsdb connection. */

/**
 * ovsdb_disconnect:
 *
 * Clean up the internal state to the point equivalent to before connecting.
 * Apart from clean shutdown this is a good response to unexpected trouble,
 * since the next method call attempt a will trigger reconnect which hopefully
 * puts us back in sync.
 */
static void
ovsdb_disconnect (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	OvsdbMethodCall *call;
	OvsdbMethodCallback callback;
	gpointer user_data;
	GError *error;

	_LOGD ("disconnecting from ovsdb");

	while (priv->calls->len) {
		error = NULL;
		call = &g_array_index (priv->calls, OvsdbMethodCall, priv->calls->len - 1);
		g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");

		callback = call->callback;
		user_data = call->user_data;
		g_array_remove_index (priv->calls, priv->calls->len - 1);
		callback (self, NULL, error, user_data);
	}

	priv->bufp = 0;
	g_string_truncate (priv->input, 0);
	g_string_truncate (priv->output, 0);
	g_clear_object (&priv->client);
	g_clear_object (&priv->conn);
	g_clear_pointer (&priv->db_uuid, g_free);
}

static void
_monitor_bridges_cb (NMOvsdb *self, json_t *result, GError *error, gpointer user_data)
{
	if (error) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			_LOGI ("%s", error->message);
			ovsdb_disconnect (self);
		}

		g_clear_error (&error);
		return;
	}

	/* Treat the first response the same as the subsequent "update"
	 * messages we eventually get. */
	ovsdb_got_update (self, result);
}

static void
_client_connect_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GSocketClient *client = G_SOCKET_CLIENT (source_object);
	NMOvsdb *self = NM_OVSDB (user_data);
	NMOvsdbPrivate *priv;
	GError *error = NULL;
	GSocketConnection *conn;

	conn = g_socket_client_connect_finish (client, res, &error);
	if (conn == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			_LOGI ("%s", error->message);

		ovsdb_disconnect (self);
		g_clear_error (&error);
		return;
	}

	priv = NM_OVSDB_GET_PRIVATE (self);
	priv->conn = conn;
	g_clear_object (&priv->cancellable);

	ovsdb_read (self);
	ovsdb_next_command (self);
}

/**
 * ovsdb_try_connect:
 *
 * Establish a connection to ovsdb unless it's already established or being
 * established. Queues a monitor command as a very first one so that we're in
 * sync when other commands are issued.
 */
static void
ovsdb_try_connect (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);
	GSocketAddress *addr;

	if (priv->client)
		return;

	/* XXX: This should probably be made configurable via NetworkManager.conf */
	addr = g_unix_socket_address_new (RUNSTATEDIR "/openvswitch/db.sock");

	priv->client = g_socket_client_new ();
	priv->cancellable = g_cancellable_new ();
	g_socket_client_connect_async (priv->client, G_SOCKET_CONNECTABLE (addr),
	                               priv->cancellable, _client_connect_cb, self);
	g_object_unref (addr);

	/* Queue a monitor call before any other command, ensuring that we have an up
	 * to date view of existing bridged that we need for add and remove ops. */
	ovsdb_call_method (self, NM_OVSDB_MONITOR, NULL, NULL, NULL, _monitor_bridges_cb, NULL);
}

/*****************************************************************************/

/* Public functions useful for NMDeviceOpenvswitch to maintain the life cycle of
 * their ovsdb entries without having to deal with ovsdb complexities themselves. */

typedef struct {
	NMOvsdbCallback callback;
	gpointer user_data;
} OvsdbCall;

static void
_transact_cb (NMOvsdb *self, json_t *result, GError *error, gpointer user_data)
{
	OvsdbCall *call = user_data;
	const char *err;
	const char *err_details;
	size_t index;
	json_t *value;

	if (error)
		goto out;

	json_array_foreach (result, index, value) {
		if (json_unpack (value, "{s:s, s:s}", "error", &err, "details", &err_details) == 0) {
			g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			             "Error running the transaction: %s: %s", err, err_details);
			goto out;
		}
	}

out:
	call->callback (error, call->user_data);
	g_slice_free (OvsdbCall, call);
}

void
nm_ovsdb_transact (NMOvsdb *self, NMOvsdbCommand command,
                   NMConnection *bridge, NMConnection *port, NMConnection *interface,
                   NMOvsdbCallback callback, gpointer user_data)
{
	OvsdbCall *call;

	call = g_slice_new (OvsdbCall);
	call->callback = callback;
	call->user_data = user_data;

	ovsdb_call_method (self, command, bridge, port, interface, _transact_cb, call);
}

/*****************************************************************************/

static void
_clear_call (gpointer data)
{
	OvsdbMethodCall *call = data;

	g_clear_object (&call->bridge);
	g_clear_object (&call->port);
	g_clear_object (&call->interface);
}

static void
_free_bridge (gpointer data)
{
	OpenvswitchBridge *ovs_bridge = data;

	g_free (ovs_bridge->name);
	g_free (ovs_bridge->connection_uuid);
	g_ptr_array_free (ovs_bridge->ports, TRUE);
	g_slice_free (OpenvswitchBridge, ovs_bridge);
}

static void
_free_port (gpointer data)
{
	OpenvswitchPort *ovs_port = data;

	g_free (ovs_port->name);
	g_free (ovs_port->connection_uuid);
	g_ptr_array_free (ovs_port->interfaces, TRUE);
	g_slice_free (OpenvswitchPort, ovs_port);
}

static void
_free_interface (gpointer data)
{
	OpenvswitchInterface *ovs_interface = data;

	g_free (ovs_interface->name);
	g_free (ovs_interface->connection_uuid);
	g_free (ovs_interface->type);
	g_slice_free (OpenvswitchInterface, ovs_interface);
}

static void
nm_ovsdb_init (NMOvsdb *self)
{
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);

	priv->calls = g_array_new (FALSE, TRUE, sizeof (OvsdbMethodCall));
	g_array_set_clear_func (priv->calls, _clear_call);
	priv->input = g_string_new (NULL);
	priv->output = g_string_new (NULL);
	priv->bridges = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, _free_bridge);
	priv->ports = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, _free_port);
	priv->interfaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, _free_interface);

	ovsdb_try_connect (self);
}

static void
dispose (GObject *object)
{
	NMOvsdb *self = NM_OVSDB (object);
	NMOvsdbPrivate *priv = NM_OVSDB_GET_PRIVATE (self);

	ovsdb_disconnect (self);

	g_string_free (priv->input, TRUE);
	priv->input = NULL;
	g_string_free (priv->output, TRUE);
	priv->output = NULL;

	if (priv->calls) {
		g_array_free (priv->calls, TRUE);
		priv->calls = NULL;
	}

	g_clear_pointer (&priv->bridges, g_hash_table_destroy);
	g_clear_pointer (&priv->ports, g_hash_table_destroy);
	g_clear_pointer (&priv->interfaces, g_hash_table_destroy);

	g_cancellable_cancel (priv->cancellable);
	g_clear_object (&priv->cancellable);

	G_OBJECT_CLASS (nm_ovsdb_parent_class)->dispose (object);
}

static void
nm_ovsdb_class_init (NMOvsdbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = dispose;

	signals[DEVICE_ADDED] =
		g_signal_new (NM_OVSDB_DEVICE_ADDED,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[DEVICE_REMOVED] =
		g_signal_new (NM_OVSDB_DEVICE_REMOVED,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[DEVICE_CHANGED] =
		g_signal_new (NM_OVSDB_DEVICE_CHANGED,
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}
