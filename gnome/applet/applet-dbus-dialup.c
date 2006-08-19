/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
 *
 * Tim Niemueller [www.niemueller.de]
 * based on work by Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004-2005 Red Hat, Inc.
 * (C) Copyright 2006      Tim Niemueller
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>
#include <dbus/dbus.h>
#include "applet-dbus-dialup.h"
#include "applet-dbus.h"
#include "applet.h"
#include "dialup-connection.h"
#include "nm-utils.h"


typedef struct DialupPropsCBData
{
	NMApplet *	applet;	
	char *			name;
} DialupPropsCBData;


static void nma_free_dialup_connections (NMApplet *applet);


void
nma_dbus_dialup_set_last_attempt_status (NMApplet *applet, const char *dialup_name, gboolean last_attempt_success)
{
	char *gconf_key;
	char *escaped_name;
	DialupConnection *dialup;
	
	if ((dialup = nma_dialup_connection_find_by_name (applet->dialup_connections, dialup_name)))
	{
		escaped_name = gconf_escape_key (dialup_name, strlen (dialup_name));

		gconf_key = g_strdup_printf ("%s/%s/last_attempt_success", GCONF_PATH_DIALUP_CONNECTIONS, escaped_name);
		gconf_client_set_bool (applet->gconf_client, gconf_key, last_attempt_success, NULL);

		g_free (gconf_key);
		g_free (escaped_name);
	}
}


/*
 * nma_dbus_dialup_update_dialup_connection_stage
 *
 * Sets the activation stage for a dbus dialup connection.
 */
void
nma_dbus_dialup_update_dialup_connection_stage (NMApplet *applet, const char *dialup_name, NMDialupActStage dialup_stage)
{
	DialupConnection	*dialup;

	g_return_if_fail (applet != NULL);

	if ((dialup = nma_dialup_connection_find_by_name (applet->dialup_connections, dialup_name)))
	{
		nma_dialup_connection_set_stage (dialup, dialup_stage);
		if (dialup_stage == NM_DIALUP_ACT_STAGE_ACTIVATED)
		{
			/* set the 'last_attempt_success' key in gconf so we DON'T prompt for password next time */
			nma_dbus_dialup_set_last_attempt_status (applet, dialup_name, TRUE);
		}
	}
}

static void
free_dialup_props_cb_data (DialupPropsCBData *data)
{
	if (data)
	{
		g_free (data->name);
		memset (data, 0, sizeof (DialupPropsCBData));
		g_free (data);
	}
}

/*
 * nma_dbus_dialup_properties_cb
 *
 * Callback for each dialup connection we called "getDialupConnectionProperties" on.
 *
 */
static void
nma_dbus_dialup_properties_cb (DBusPendingCall *pcall, void *user_data)
{
	DBusMessage *		reply;
	DialupPropsCBData *	cb_data = user_data;
	NMApplet *	applet;
	const char *		name;
	const char *        user_name;
	const char *        service;
	NMDialupActStage		stage;
	dbus_uint32_t		stage_int;
	
	g_return_if_fail (pcall != NULL);
	g_return_if_fail (cb_data != NULL);
	g_return_if_fail (cb_data->applet != NULL);
	g_return_if_fail (cb_data->name != NULL);

	nma_dbus_send_with_callback_replied (pcall, __func__);

	applet = cb_data->applet;

	if (!(reply = dbus_pending_call_steal_reply (pcall)))
		goto out;

	if (message_is_error (reply))
	{
		DBusError err;

		dbus_error_init (&err);
		dbus_set_error_from_message (&err, reply);
		nm_warning ("dbus returned an error.\n  (%s) %s\n", err.name, err.message);
		dbus_error_free (&err);
		dbus_message_unref (reply);
		goto out;
	}

	if (dbus_message_get_args (reply, NULL,
						  DBUS_TYPE_STRING, &name,
						  DBUS_TYPE_STRING, &user_name,
						  DBUS_TYPE_STRING, &service,
						  DBUS_TYPE_UINT32, &stage_int,
						  DBUS_TYPE_INVALID))
	{
		DialupConnection *	dialup;

		stage = (NMDialupActStage) stage_int;

		/* If its already there, update the service, otherwise add it to the list */
		if ((dialup = nma_dialup_connection_find_by_name (applet->dialup_connections, name)))
		{
			nma_dialup_connection_set_service (dialup, service);
			nma_dialup_connection_set_stage (dialup, stage);
		}
		else
		{
			dialup = nma_dialup_connection_new (name);
			nma_dialup_connection_set_service (dialup, service);
			nma_dialup_connection_set_stage (dialup, stage);
			applet->dialup_connections = g_slist_append (applet->dialup_connections, dialup);
		}
	}
	dbus_message_unref (reply);

out:
	dbus_pending_call_unref (pcall);
}


/*
 * nma_dbus_dialup_update_one_dialup_connection
 *
 * Get properties on one Dialup connection
 *
 */
void nma_dbus_dialup_update_one_dialup_connection (NMApplet *applet, const char *dialup_name)
{
	DBusMessage *		message;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (dialup_name != NULL);

	nma_get_first_active_dialup_connection (applet);

	if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE, NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, "getDialupConnectionProperties")))
	{
		DialupPropsCBData *	cb_data = g_malloc0 (sizeof (DialupPropsCBData));

		dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup_name, DBUS_TYPE_INVALID);

		cb_data->applet = applet;
		cb_data->name = g_strdup (dialup_name);

		nma_dbus_send_with_callback (applet->connection, message,
				nma_dbus_dialup_properties_cb, cb_data,
				(DBusFreeFunction) free_dialup_props_cb_data, __func__);
		dbus_message_unref (message);
	}
}


/*
 * nma_dbus_dialup_update_dialup_connections_cb
 *
 * nma_dbus_dialup_update_dialup_connections callback.
 *
 */
static void nma_dbus_dialup_update_dialup_connections_cb (DBusPendingCall *pcall, void *user_data)
{
	DBusMessage *		reply;
	NMApplet *	applet = (NMApplet *) user_data;
	char **			dialup_names;
	int				num_dialup_names;

	g_return_if_fail (pcall != NULL);
	g_return_if_fail (applet != NULL);

	nma_dbus_send_with_callback_replied (pcall, __func__);

	if (!(reply = dbus_pending_call_steal_reply (pcall)))
		goto out;

	if (dbus_message_is_error (reply, NM_DBUS_NO_DIALUP_CONNECTIONS))
	{
		dbus_message_unref (reply);
		goto out;
	}

	if (message_is_error (reply))
	{
		DBusError err;

		dbus_error_init (&err);
		dbus_set_error_from_message (&err, reply);
		nm_warning ("dbus returned an error.\n  (%s) %s\n", err.name, err.message);
		dbus_error_free (&err);
		dbus_message_unref (reply);
		goto out;
	}

	if (dbus_message_get_args (reply, NULL, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &dialup_names, &num_dialup_names, DBUS_TYPE_INVALID))
	{
		char ** item;

		/* For each connection, fire off a "getDialupConnectionProperties" call */
		for (item = dialup_names; *item; item++)
			nma_dbus_dialup_update_one_dialup_connection (applet, *item);

		dbus_free_string_array (dialup_names);
	}
	dbus_message_unref (reply);

out:
	dbus_pending_call_unref (pcall);
}


/*
 * nma_dbus_dialup_update_dialup_connections
 *
 * Do a full update of dialup connections from NetworkManager
 *
 */
void nma_dbus_dialup_update_dialup_connections (NMApplet *applet)
{
	DBusMessage *		message;

	nma_free_dialup_connections (applet);

	nma_get_first_active_dialup_connection (applet);

	if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE, NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, "getDialupConnections")))
	{
		nma_dbus_send_with_callback (applet->connection, message,
				nma_dbus_dialup_update_dialup_connections_cb, applet, NULL, __func__);
		dbus_message_unref (message);
	}
}


/*
 * nma_dbus_dialup_remove_one_dialup_connection
 *
 * Remove one dialup connection from the list
 *
 */
void nma_dbus_dialup_remove_one_dialup_connection (NMApplet *applet, const char *dialup_name)
{
	DialupConnection *	dialup;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (dialup_name != NULL);

	if ((dialup = nma_dialup_connection_find_by_name (applet->dialup_connections, dialup_name)))
	{
		applet->dialup_connections = g_slist_remove (applet->dialup_connections, dialup);
		nma_dialup_connection_unref (dialup);
	}
}

static void nma_free_dialup_connections (NMApplet *applet)
{
	g_return_if_fail (applet != NULL);

	if (applet->dialup_connections)
	{
		g_slist_foreach (applet->dialup_connections, (GFunc) nma_dialup_connection_unref, NULL);
		g_slist_free (applet->dialup_connections);
		applet->dialup_connections = NULL;
	}
}


/*
 * nma_dbus_dialup_activate_connection
 *
 * Tell NetworkManager to activate a particular Dialup connection.
 *
 */
void nma_dbus_dialup_activate_connection (DBusConnection *connection, const char *name, GSList *passwords)
{
	DBusMessage	*message;
	DBusMessageIter	 iter;
	DBusMessageIter	 iter_array;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (passwords != NULL);

	if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE, NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, "activateDialupConnection")))
	{
		GSList *i;

		nm_info ("Activating Dialup connection '%s'.", name);
		dbus_message_iter_init_append (message, &iter);
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &name);
		dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &iter_array);

		for (i = passwords; i != NULL; i = g_slist_next (i)) {
			dbus_message_iter_append_basic (&iter_array, DBUS_TYPE_STRING, &(i->data));
		}
		dbus_message_iter_close_container (&iter, &iter_array);
		dbus_connection_send (connection, message, NULL);
	}
	else
		nm_warning ("Couldn't allocate the dbus message");
}


/*
 * nma_dbus_deactivate_dialup_connection
 *
 * Tell NetworkManager to deactivate the currently active Dialup connection.
 *
 */
void nma_dbus_dialup_deactivate_connection (DBusConnection *connection)
{
	DBusMessage	*message;

	g_return_if_fail (connection != NULL);

	if ((message = dbus_message_new_method_call (NM_DBUS_SERVICE, NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, "deactivateDialupConnection")))
	{
		nm_info ("Deactivating the current Dialup connection.");
		dbus_connection_send (connection, message, NULL);
	}
	else
		nm_warning ("Couldn't allocate the dbus message");
}


