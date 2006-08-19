/* nm-dialup-connection.c - handle a single dialup connection within NetworkManager's framework 
 *
 * Copyright (C) 2006 Tim Niemueller
 * based on work Copyright (C) 2005 Dan Williams
 *
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include "config.h"
#include <glib.h>
#include <string.h>
#include "nm-dialup-connection.h"
#include "nm-dbus-dialup.h"
#include "NetworkManagerSystem.h"


struct NMDialupConnection
{
	int			refcount;

	/* Won't change over life of object */
	char *		name;
	char *		user_name;
	char *		service_name;

	NMNamedManager *named_manager;
	DBusConnection *dbus_connection;

	/* Change when connection is activated/deactivated */
	NMIP4Config *	ip4_config;
	char *		dialup_iface;
};


static void	nm_dialup_connection_set_dialup_iface	(NMDialupConnection *con, const char *dialup_iface);
static void	nm_dialup_connection_set_ip4_config	(NMDialupConnection *con, NMIP4Config *ip4_config);

NMDialupConnection *
nm_dialup_connection_new (const char *name, const char *user_name, const char *service_name,
			  NMNamedManager *named_manager, DBusConnection *dbus_connection)
{
	NMDialupConnection	*connection;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (user_name != NULL, NULL);
	g_return_val_if_fail (service_name != NULL, NULL);
	g_return_val_if_fail (named_manager != NULL, NULL);
	g_return_val_if_fail (dbus_connection != NULL, NULL);

	connection = g_malloc0 (sizeof (NMDialupConnection));
	connection->refcount = 1;

	connection->name = g_strdup (name);
	connection->user_name = g_strdup (user_name);
	connection->service_name = g_strdup (service_name);

	g_object_ref (named_manager);
	connection->named_manager = named_manager;

	connection->dbus_connection = dbus_connection;

	return connection;
}


void
nm_dialup_connection_ref (NMDialupConnection *connection)
{
	g_return_if_fail (connection != NULL);

	connection->refcount++;
}


void
nm_dialup_connection_unref (NMDialupConnection *connection)
{
	g_return_if_fail (connection != NULL);

	connection->refcount--;
	if (connection->refcount <= 0)
	{
		g_free (connection->name);
		g_free (connection->user_name);
		g_free (connection->service_name);

		if (connection->ip4_config)
			nm_ip4_config_unref (connection->ip4_config);
		g_free (connection->dialup_iface);

		g_object_unref (connection->named_manager);

		memset (connection, 0, sizeof (NMDialupConnection));
		g_free (connection);
	}
}


void
nm_dialup_connection_activate (NMDialupConnection *connection)
{
	g_return_if_fail (connection != NULL);

	/* Nothing done here yet */
}


gboolean
nm_dialup_connection_set_config (NMDialupConnection *connection, const char *dialup_iface, NMIP4Config *ip4_config)
{
	gboolean	success = FALSE;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (ip4_config != NULL, FALSE);

	if (dialup_iface != NULL && strlen (dialup_iface))
		nm_dialup_connection_set_dialup_iface (connection, dialup_iface);
	nm_dialup_connection_set_ip4_config (connection, ip4_config);

	// m_system_dialup_device_set_from_ip4_config (connection->named_manager,
	//			connection->dialup_iface, connection->ip4_config);
	success = TRUE;

	return success;
}


void
nm_dialup_connection_deactivate (NMDialupConnection *connection)
{
	g_return_if_fail (connection != NULL);

	if (connection->dialup_iface)
	{
		nm_system_device_set_up_down_with_iface (connection->dialup_iface, FALSE);
		nm_system_device_flush_routes_with_iface (connection->dialup_iface);
		nm_system_device_flush_addresses_with_iface (connection->dialup_iface);
	}

	if (connection->ip4_config)
	{
		/* Remove attributes of the dialup's IP4 Config */
		//nm_system_dialup_device_unset_from_ip4_config (connection->named_manager, connection->parent_dev,
		//		connection->dialup_iface, connection->ip4_config);

		/* Reset routes, nameservers, and domains of the currently active device */
		//nm_system_device_set_from_ip4_config (connection->parent_dev);
	}

	nm_dialup_connection_set_ip4_config (connection, NULL);
	nm_dialup_connection_set_dialup_iface (connection, NULL);
}


const char *
nm_dialup_connection_get_name (NMDialupConnection *connection)
{
	g_return_val_if_fail (connection != NULL, NULL);

	return connection->name;
}


const char *
nm_dialup_connection_get_user_name (NMDialupConnection *connection)
{
	g_return_val_if_fail (connection != NULL, NULL);

	return connection->user_name;
}


const char *
nm_dialup_connection_get_service_name (NMDialupConnection *connection)
{
	g_return_val_if_fail (connection != NULL, NULL);

	return connection->service_name;
}


static void
nm_dialup_connection_set_dialup_iface (NMDialupConnection *con, const char *dialup_iface)
{
	g_return_if_fail (con != NULL);

	if (con->dialup_iface)
	{
		g_free (con->dialup_iface);
		con->dialup_iface = NULL;
	}

	if (dialup_iface)
		con->dialup_iface = g_strdup (dialup_iface);
}


static void
nm_dialup_connection_set_ip4_config (NMDialupConnection *con, NMIP4Config *ip4_config)
{
	g_return_if_fail (con != NULL);

	if (con->ip4_config)
	{
		nm_ip4_config_unref (con->ip4_config);
		con->ip4_config = NULL;
	}

	if (ip4_config)
	{
		nm_ip4_config_ref (ip4_config);
		con->ip4_config = ip4_config;
	}
}
