/* NetworkManager -- Network link manager
 *
 * Tim Niemueller [www.niemueller.de]
 * Dan Williams <dcbw@redhat.com>
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
 * (C) Copyright 2005 Red Hat, Inc.
 * (C) Copyright 2006 Tim Niemueller
 */

#include <glib.h>
#include <dbus/dbus.h>
#include "NetworkManagerMain.h"
#include "nm-device.h"
#include "NetworkManagerDbus.h"
#include "NetworkManagerUtils.h"
#include "NetworkManagerDialup.h"
#include "nm-dbus-dialup.h"
#include "nm-dialup-manager.h"
#include "nm-dialup-connection.h"
#include "nm-dialup-service.h"
#include "nm-dialup-act-request.h"
#include "nm-utils.h"


/*
 * nm_dbus_dialup_signal_dialup_connection_update
 *
 * Notifies the bus that a Dialup connection's properties have changed.
 *
 */
void nm_dbus_dialup_signal_dialup_connection_update (DBusConnection *con, NMDialupConnection *dialup, const char *signal)
{
	DBusMessage	*message;
	const char	*dialup_name;

	g_return_if_fail (con != NULL);
	g_return_if_fail (dialup != NULL);

	if (!(message = dbus_message_new_signal (NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, signal)))
	{
		nm_warning ("Not enough memory for new dbus message!");
		return;
	}

	dialup_name = nm_dialup_connection_get_name (dialup);

	dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup_name, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (con, message, NULL))
		nm_warning ("Could not raise the %s signal!", signal);

	dbus_message_unref (message);
}

/*
 * nm_dbus_dialup_signal_dialup_connection_state_change
 *
 * Notifies the bus that a dialup connection's state has changed.
 */
void nm_dbus_dialup_signal_dialup_connection_state_change (DBusConnection *con, NMDialupConnection *dialup, NMDialupActStage new_stage)
{
	DBusMessage *	message;
	const char *	dialup_name;
	dbus_uint32_t	int_stage = (dbus_uint32_t) new_stage;

	g_return_if_fail (con != NULL);
	g_return_if_fail (dialup != NULL);

	if (!(message = dbus_message_new_signal (NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, "DialupConnectionStateChange")))
	{
		nm_warning ("Not enough memory for new dbus message!");
		return;
	}

	dialup_name = nm_dialup_connection_get_name (dialup);
	dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup_name, DBUS_TYPE_UINT32, &int_stage, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (con, message, NULL))
		nm_warning ("Could not raise the DialupConnectionStateChange signal!");

	dbus_message_unref (message);
}


/*
 * nnm_dbus_dialup_signal_dialup_failure
 *
 * Proxy a Dialup Failure message from the dialup daemon to the bus.
 *
 */
void nm_dbus_dialup_signal_dialup_failed (DBusConnection *con, const char *signal, NMDialupConnection *dialup, const char *error_msg)
{
	DBusMessage	*message;
	const char	*dialup_name;

	g_return_if_fail (con != NULL);
	g_return_if_fail (signal != NULL);
	g_return_if_fail (dialup != NULL);
	g_return_if_fail (error_msg != NULL);

	if (!(message = dbus_message_new_signal (NM_DBUS_PATH_DIALUP, NM_DBUS_INTERFACE_DIALUP, signal)))
	{
		nm_warning ("Not enough memory for new dbus message!");
		return;
	}

	dialup_name = nm_dialup_connection_get_name (dialup);
	dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup_name, DBUS_TYPE_STRING, &error_msg, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (con, message, NULL))
		nm_warning ("Could not raise the %s signal!", signal);

	dbus_message_unref (message);
}


/*
 * nm_dbus_dialup_get_dialup_data
 *
 * Get dialup specific data from NMI for a dialup connection
 *
 * NOTE: caller MUST free returned value using g_strfreev()
 *
 */
static char ** nm_dbus_dialup_get_dialup_data (DBusConnection *connection, NMDialupConnection *dialup, int *num_items)
{
	DBusMessage		*message;
	DBusError			 error;
	DBusMessage		*reply;
	char			    **data_items = NULL;
	const char		*dialup_name;

	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (dialup != NULL, NULL);
	g_return_val_if_fail (num_items != NULL, NULL);

	*num_items = -1;

	if (!(message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "getDialupConnectionDialupData")))
	{
		nm_warning ("nm_dbus_dialup_get_dialup_data(): Couldn't allocate the dbus message");
		return (NULL);
	}

	dialup_name = nm_dialup_connection_get_name (dialup);
	dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup_name, DBUS_TYPE_INVALID);

	dbus_error_init (&error);
	reply = dbus_connection_send_with_reply_and_block (connection, message, -1, &error);
	dbus_message_unref (message);
	if (dbus_error_is_set (&error))
		nm_warning ("nm_dbus_dialup_get_dialup_data(): %s raised %s", error.name, error.message);
	else if (!reply)
		nm_info ("nm_dbus_dialup_get_dialup_data(): reply was NULL.");
	else
	{
		DBusMessageIter iter, array_iter;
		GArray *buffer;

		dbus_message_iter_init (reply, &iter);
		dbus_message_iter_recurse (&iter, &array_iter);

		buffer = g_array_new (TRUE, TRUE, sizeof (gchar *));

		if (buffer == NULL)
			return NULL;

		while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRING)
		{
			const char *value;
			char *str;
		
			dbus_message_iter_get_basic (&array_iter, &value);
			str = g_strdup (value);
			
			if (str == NULL)
			{
				g_array_free (buffer, TRUE);
				return NULL;
			}

			g_array_append_val (buffer, str);

			dbus_message_iter_next (&array_iter);
		}
		data_items = (gchar **)(buffer->data);
		*num_items = buffer->len;
		g_array_free (buffer, FALSE);
	}
	
	if (reply)
		dbus_message_unref (reply);

	return (data_items);
}



typedef struct UpdateOneDialupCBData
{
	NMData *	data;
	char *	dialup;
} UpdateOneDialupCBData;


static void free_update_one_dialup_cb_data (UpdateOneDialupCBData *data)
{
	if (data)
		g_free (data->dialup);
	g_free (data);
}

/*
 * nm_dbus_dialup_update_one_connection_cb
 *
 * Retrieve and add to our dialup Manager one dialup connection from NMI.
 *
 */
static void nm_dbus_dialup_update_one_connection_cb (DBusPendingCall *pcall, void *user_data)
{
	UpdateOneDialupCBData *	cb_data = (UpdateOneDialupCBData *) user_data;
	DBusMessage *			reply;
	const char *			con_name = NULL;
	const char *			service_name = NULL;
	const char *			user_name = NULL;
	
	g_return_if_fail (pcall != NULL);
	g_return_if_fail (cb_data != NULL);
	g_return_if_fail (cb_data->data != NULL);
	g_return_if_fail (cb_data->data->dialup_manager != NULL);

	dbus_pending_call_ref (pcall);

	if (!dbus_pending_call_get_completed (pcall))
		goto out;

	if (!(reply = dbus_pending_call_steal_reply (pcall)))
		goto out;

	if (dbus_message_is_error (reply, "BadDialupConnectionData"))
	{
		NMDialupConnection *dialup;

		/* Bad dialup, remove it from our dialup connection list */
		if ((dialup = nm_dialup_manager_find_connection_by_name (cb_data->data->dialup_manager, cb_data->dialup)))
		{
			nm_dialup_connection_ref (dialup);
			nm_dialup_manager_remove_connection (cb_data->data->dialup_manager, dialup);
			nm_dbus_dialup_signal_dialup_connection_update (cb_data->data->dbus_connection, dialup, "DialupConnectionRemoved");
			nm_dialup_connection_unref (dialup);
		}
		goto out;
	}

	if (dbus_message_get_args (reply, NULL,
				   DBUS_TYPE_STRING, &con_name,
				   DBUS_TYPE_STRING, &service_name,
				   DBUS_TYPE_STRING, &user_name,
				   DBUS_TYPE_INVALID))
	{
		NMDialupConnection *	dialup;
		gboolean			new = TRUE;

		// nm_info ("Updating connectoin %s/%s/%s", con_name, service_name, user_name);

		if ((dialup = nm_dialup_manager_find_connection_by_name (cb_data->data->dialup_manager, con_name)))
		{
			const char *dialup_service_name = nm_dialup_connection_get_service_name (dialup);

			/* If all attributes of the existing connection are the same as the one we get from NMI,
			 * don't do anything.
			 */
			if (strcmp (dialup_service_name, service_name) || strcmp (nm_dialup_connection_get_user_name (dialup), user_name))
				nm_dialup_manager_remove_connection (cb_data->data->dialup_manager, dialup);
			else
				new = FALSE;
		}

		if (new)
			dialup = nm_dialup_manager_add_connection (cb_data->data->dialup_manager, con_name, service_name, user_name);

		if (dialup)
			nm_dbus_dialup_signal_dialup_connection_update (cb_data->data->dbus_connection, dialup, new ? "DialupConnectionAdded" : "DialupConnectionUpdate");

	}
	dbus_message_unref (reply);

out:
	dbus_pending_call_unref (pcall);
}


/*
 * nm_dbus_dialup_connections_update_cb
 *
 * Async callback from nnm_dbus_dialup_connections_update
 *
 */
static void nm_dbus_dialup_connections_update_cb (DBusPendingCall *pcall, void *user_data)
{
	NMData *			data = (NMData *) user_data;
	DBusMessage *		reply;
	DBusMessageIter	iter, array_iter;
	GSList *			remove_list = NULL;
	GSList *			elt;

	g_return_if_fail (pcall);
	g_return_if_fail (data != NULL);

	dbus_pending_call_ref (pcall);

	if (!dbus_pending_call_get_completed (pcall))
		goto out;

	if (!(reply = dbus_pending_call_steal_reply (pcall)))
		goto out;

	if (message_is_error (reply))
		goto out;

	remove_list = nm_dialup_manager_dialup_connection_list_copy (data->dialup_manager);

	dbus_message_iter_init (reply, &iter);
	dbus_message_iter_recurse (&iter, &array_iter);
	while (dbus_message_iter_get_arg_type (&array_iter) == DBUS_TYPE_STRING)
	{
		DBusMessage *		message;
		const char *		con_name;
		NMDialupConnection *	dialup;

		dbus_message_iter_get_basic (&array_iter, &con_name);

		/* If the connection already exists, remove it from the remove list */
		if ((dialup = nm_dialup_manager_find_connection_by_name (data->dialup_manager, con_name)))
		{
			remove_list = g_slist_remove (remove_list, dialup);
		}

		if ((message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "getDialupConnectionProperties")))
		{
			DBusPendingCall *		dialup_pcall = NULL;

			dbus_message_append_args (message, DBUS_TYPE_STRING, &con_name, DBUS_TYPE_INVALID);
			dbus_connection_send_with_reply (data->dbus_connection, message, &dialup_pcall, -1);
			dbus_message_unref (message);
			if (dialup_pcall)
			{
				UpdateOneDialupCBData *	dialup_cb_data = g_malloc0 (sizeof (UpdateOneDialupCBData));

				dialup_cb_data->data = data;
				dialup_cb_data->dialup = g_strdup (con_name);
				dbus_pending_call_set_notify (dialup_pcall, nm_dbus_dialup_update_one_connection_cb, dialup_cb_data, (DBusFreeFunction) free_update_one_dialup_cb_data);
			}
		}
		dbus_message_iter_next (&array_iter);
	}
	dbus_message_unref (reply);

	/* dialup connections left in the remove list aren't known by NMI, therefore we delete them */
	for (elt = remove_list; elt; elt = g_slist_next (elt))
	{
		nm_dialup_manager_remove_connection (data->dialup_manager, elt->data);
		nm_dialup_connection_unref (elt->data);
	}

	g_slist_free (remove_list);

out:
	dbus_pending_call_unref (pcall);
}


/*
 * nm_dbus_dialup_update_one_dialup_connection
 *
 * Update one dialup connection
 *
 */
void nm_dbus_dialup_update_one_dialup_connection (DBusConnection *connection, const char *dialup, NMData *data)
{
	DBusMessage *			message;
	DBusPendingCall *		pcall = NULL;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (dialup != NULL);
	g_return_if_fail (data != NULL);

	if (!(message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "getDialupConnectionProperties")))
	{
		nm_warning ("nm_dbus_update_one_dialup_connection(): Couldn't allocate the dbus message");
		return;
	}

	dbus_message_append_args (message, DBUS_TYPE_STRING, &dialup, DBUS_TYPE_INVALID);
	dbus_connection_send_with_reply (connection, message, &pcall, -1);
	dbus_message_unref (message);
	if (pcall)
	{
		UpdateOneDialupCBData *	cb_data = g_malloc0 (sizeof (UpdateOneDialupCBData));

		cb_data->data = data;
		cb_data->dialup = g_strdup (dialup);
		dbus_pending_call_set_notify (pcall, nm_dbus_dialup_update_one_connection_cb, cb_data, (DBusFreeFunction) free_update_one_dialup_cb_data);
	}
}


/*
 * nm_dbus_dialup_connections_update_from_nmi
 *
 * Update dialup connections from NetworkManagerInfo.
 *
 */
static gboolean nm_dbus_dialup_connections_update_from_nmi (NMData *data)
{
	DBusMessage *		message;
	DBusPendingCall *	pcall;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data->dbus_connection != NULL, FALSE);
	g_return_val_if_fail (data->dialup_manager != NULL, FALSE);

	if (!(message = dbus_message_new_method_call (NMI_DBUS_SERVICE, NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "getDialupConnections")))
	{
		nm_warning ("nm_dbus_dialup_connections_update (): Couldn't allocate the dbus message");
		return FALSE;
	}

	dbus_connection_send_with_reply (data->dbus_connection, message, &pcall, -1);
	dbus_message_unref (message);
	if (pcall)
	{
		dbus_pending_call_set_notify (pcall, nm_dbus_dialup_connections_update_cb, data, NULL);
		dbus_pending_call_block (pcall);
	}

	return FALSE;
}


/*
 * nm_dbus_dialup_schedule_dialup_connections_update
 *
 * Schedule an update of dialup connections in the main thread
 *
 */
void nm_dbus_dialup_schedule_dialup_connections_update (NMData *app_data)
{
	GSource	*source = NULL;

	g_return_if_fail (app_data != NULL);
	g_return_if_fail (app_data->main_context != NULL);

	source = g_idle_source_new ();
	/* We want this idle source to run before any other idle source */
	g_source_set_priority (source, G_PRIORITY_HIGH_IDLE);
	g_source_set_callback (source, (GSourceFunc) nm_dbus_dialup_connections_update_from_nmi, app_data, NULL);
	g_source_attach (source, app_data->main_context);
	g_source_unref (source);
}


/*
 * nm_dbus_dialup_get_dialup_connections
 *
 * Returns a string array of dialup connection names.
 *
 */
static DBusMessage *nm_dbus_dialup_get_dialup_connections (DBusConnection *connection, DBusMessage *message, NMDbusCBData *data)
{
	DBusMessage		*reply = NULL;
	char				**dialup_names = NULL;
	int				 num_names;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->data != NULL, NULL);
	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (!data->data->dialup_manager)
	{
		reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_DIALUP, "NoDialupConnections", "There are no available dialup connections.");
		goto out;
	}

	dialup_names = nm_dialup_manager_get_connection_names (data->data->dialup_manager);
	num_names = dialup_names ? g_strv_length (dialup_names) : 0;
	if (num_names == 0)
	{
		reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_DIALUP, "NoDialupConnections", "There are no available dialup connections.");
		goto out;
	}

	if (!(reply = dbus_message_new_method_return (message)))
		goto out;

	dbus_message_append_args (reply, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &dialup_names, num_names, DBUS_TYPE_INVALID);

out:
	if (dialup_names)
		g_strfreev (dialup_names); 

	return (reply);
}


/*
 * nm_dbus_dialup_get_dialup_connection_properties
 *
 * Grab properties of a dialup connection
 *
 */
static DBusMessage *nm_dbus_dialup_get_dialup_connection_properties (DBusConnection *connection, DBusMessage *message, NMDbusCBData *data)
{
	DBusMessage *		reply = NULL;
	DBusError			error;
	const char *		name;
	gboolean			good = FALSE;
	NMDialupManager *		manager;
	NMDialupConnection *	dialup;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->data != NULL, NULL);	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	/* Check for no dialup Manager */
	if (!(manager = data->data->dialup_manager))
		return nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_DIALUP, "NoDialupConnections", "There are no available dialup connections.");

	if (!(reply = dbus_message_new_method_return (message)))
		return NULL;

	dbus_error_init (&error);
	if (dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
	{
		if ((dialup = nm_dialup_manager_find_connection_by_name (manager, name)))
		{
			const char *	user_name;
			const char *	service_name;
			NMDialupService *	service;

			user_name = nm_dialup_connection_get_user_name (dialup);
			service_name = nm_dialup_connection_get_service_name (dialup);
			if ((service = nm_dialup_manager_find_service_by_name (data->data->dialup_manager, service_name)))
			{
				NMDialupActRequest *	req = nm_dialup_manager_get_dialup_act_request (manager);
				dbus_uint32_t		stage = (dbus_uint32_t) NM_DIALUP_ACT_STAGE_DISCONNECTED;

				if (req && (nm_dialup_act_request_get_connection (req) == dialup))
					stage = nm_dialup_act_request_get_stage (req);

				dbus_message_append_args (reply, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &user_name,
								DBUS_TYPE_STRING, &service_name, DBUS_TYPE_UINT32, &stage, DBUS_TYPE_INVALID);
				good = TRUE;
			}
		}
	}

	if (!good)
		reply = nm_dbus_create_error_message (message, NM_DBUS_INTERFACE_DIALUP, "InvalidDialupConnection", "No dialup connection with that name was found.");

	return reply;
}


/*
 * nm_dbus_dialup_activate_connection
 *
 * Activate a specific dialup connection.
 *
 */
static DBusMessage *nm_dbus_dialup_activate_connection (DBusConnection *connection, DBusMessage *message, NMDbusCBData *data)
{
	DBusError			error;
	const char *		name;
	char **			passwords;
	int				num_passwords;
	NMDialupConnection *	dialup;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->data != NULL, NULL);
	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);

	if (dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &name, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &passwords, &num_passwords, DBUS_TYPE_INVALID))
	{
		if ((dialup = nm_dialup_manager_find_connection_by_name (data->data->dialup_manager, name)))
		{
			int	item_count = -1;
			char **items;
			if ((items = nm_dbus_dialup_get_dialup_data (connection, dialup, &item_count)))
			{
				char *	joined_string = g_strjoinv (" :: ", items);
				nm_info ("Will activate dialup connection '%s', service '%s', user_name '%s', dialup_data '%s'.",
				name, nm_dialup_connection_get_service_name (dialup), nm_dialup_connection_get_user_name (dialup), joined_string);
				nm_dialup_manager_activate_dialup_connection (data->data->dialup_manager, dialup, passwords, num_passwords, items, item_count);

				g_free (joined_string);
				g_strfreev (items);
			}
		} else {
			nm_warning ("nm_dbus_dialup_activate_connection(): cannot find dialup connection '%s'", name);
		}
	} else {
		nm_warning ("nm_dbus_dialup_activate_connection(): syntax error in method arguments");
	}

	return NULL;
}


/*
 * nm_dbus_dialup_deactivate_connection
 *
 * Deactivate the active dialup connection, if any.
 *
 */
static DBusMessage *nm_dbus_dialup_deactivate_connection (DBusConnection *connection, DBusMessage *message, NMDbusCBData *data)
{
	NMDialupActRequest *req;
	NMDialupConnection *dialup;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data->data != NULL, NULL);
	g_return_val_if_fail (connection != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	if (!(req = nm_dialup_manager_get_dialup_act_request (data->data->dialup_manager)))
		return NULL;

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_info ("Will deactivate the dialup connection '%s', service '%s'.", nm_dialup_connection_get_name (dialup),
						nm_dialup_connection_get_service_name (dialup));
	nm_dialup_manager_deactivate_dialup_connection (data->data->dialup_manager);

	return NULL;
}


/*
 * nm_dbus_dialup_methods_setup
 *
 * Register handlers for dbus methods on the
 * org.freedesktop.NetworkManager.DialupConnections object.
 *
 */
NMDbusMethodList *nm_dbus_dialup_methods_setup (void)
{
	NMDbusMethodList	*list = nm_dbus_method_list_new (NULL);

	nm_dbus_method_list_add_method (list, "getDialupConnections",			nm_dbus_dialup_get_dialup_connections);
	nm_dbus_method_list_add_method (list, "getDialupConnectionProperties",	nm_dbus_dialup_get_dialup_connection_properties);
	nm_dbus_method_list_add_method (list, "activateDialupConnection",		nm_dbus_dialup_activate_connection);
	nm_dbus_method_list_add_method (list, "deactivateDialupConnection",		nm_dbus_dialup_deactivate_connection);

	return (list);
}
