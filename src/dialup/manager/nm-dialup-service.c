/* NetworkManager -- Network link manager
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
 * (C) Copyright 2005 Red Hat, Inc.
 * (C) Copyright 2006 Tim Niemueller
 */

#include <glib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dbus/dbus.h>
#include "NetworkManagerDbus.h"
#include "NetworkManagerMain.h"
#include "nm-dbus-dialup.h"
#include "nm-dialup-service.h"
#include "nm-dialup-act-request.h"
#include "nm-utils.h"

/* define this for getting dialup debug messages */
#undef NM_DEBUG_DIALUP_CONFIG

struct NMDialupService
{
	int				refcount;
	NMDialupManager *		manager;
	NMData *			app_data;
	gboolean			watch_active;

	char *			name;
	char *			service;
	char *			program;
	NMDialupState		state;
};


static void nm_dialup_service_add_watch (NMDialupService *service);
static void nm_dialup_service_remove_watch (NMDialupService *service);
static void nm_dialup_service_stop_connection_internal (NMDialupService *service);
#ifdef NM_DEBUG_DIALUP_CONFIG
static void print_dialup_config (guint32 ip4_dialup_gateway,
				 const char *dev,
				 guint32 ip4_address,
				 guint32 ip4_ptp_address,
				 gint32 ip4_netmask,
				 guint32 *ip4_dns,
				 guint32 ip4_dns_len,
				 guint32 *ip4_nbns);
#endif

static void nm_dialup_service_schedule_stage1_daemon_exec (NMDialupService *service, NMDialupActRequest *req);
static void nm_dialup_service_schedule_stage3_connect (NMDialupService *service, NMDialupActRequest *req);
static void nm_dialup_service_schedule_stage2_daemon_wait (NMDialupService *service, NMDialupActRequest *req);
static void nm_dialup_service_schedule_stage4_ip_config_get_timeout (NMDialupService *service, NMDialupActRequest *req);
static void nm_dialup_service_cancel_callback (NMDialupService *service, NMDialupActRequest *req);


/*
 * nm_dialup_service_new
 *
 * Create a new DialupService object
 *
 */
NMDialupService *nm_dialup_service_new (NMDialupManager *manager, NMData *app_data)
{
	NMDialupService *service = g_malloc0 (sizeof (NMDialupService));

	service->refcount = 1;
	service->state = NM_DIALUP_STATE_SHUTDOWN;
	service->app_data = app_data;
	service->manager = manager;

	return service;
}

void nm_dialup_service_ref (NMDialupService *service)
{
	g_return_if_fail (service != NULL);

	service->refcount++;
}


void nm_dialup_service_unref (NMDialupService *service)
{
	g_return_if_fail (service != NULL);

	service->refcount--;
	if (service->refcount <= 0)
	{
		g_free (service->name);
		g_free (service->service);
		g_free (service->program);
		memset (service, 0, sizeof (NMDialupService));
		g_free (service);
	}
}


const char *nm_dialup_service_get_name (NMDialupService *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return service->name;
}


void nm_dialup_service_set_name (NMDialupService *service, const char *name)
{
	g_return_if_fail (service != NULL);

	if (service->name)
		g_free (service->name);
	service->name = g_strdup (name);
}


const char *nm_dialup_service_get_service_name (NMDialupService *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return service->service;
}


void nm_dialup_service_set_service_name (NMDialupService *service, const char *name)
{
	g_return_if_fail (service != NULL);

	if (service->service)
		g_free (service->service);
	service->service = g_strdup (name);

	/* If the Dialup daemon is currently running, tell it to stop */
	if (!dbus_bus_name_has_owner (service->app_data->dbus_connection, service->service, NULL))
		nm_dialup_service_stop_connection_internal (service);
}


const char *nm_dialup_service_get_program (NMDialupService *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return service->program;
}


void nm_dialup_service_set_program (NMDialupService *service, const char *program)
{
	g_return_if_fail (service != NULL);

	if (service->program)
		g_free (service->program);
	service->program = g_strdup (program);
}


NMDialupState nm_dialup_service_get_state (NMDialupService *service)
{
	g_return_val_if_fail (service != NULL, NM_DIALUP_STATE_UNKNOWN);

	return service->state;
}


static void nm_dialup_service_set_state (NMDialupService *service, const NMDialupState state)
{
	g_return_if_fail (service != NULL);

	service->state = state;
}


DBusConnection *nm_dialup_service_get_dbus_connection (NMDialupService *service)
{
	g_return_val_if_fail (service != NULL, NULL);

	return service->app_data->dbus_connection;
}


/*
 * construct_op_from_service_name
 *
 * Construct an object path from a dbus service name by replacing
 * all "." in the service with "/" and prepending a "/" to the
 * object path.
 *
 */
static char *construct_op_from_service_name (const char *service_name)
{
	char **split = NULL;
	char *temp1;
	char *temp2;

	g_return_val_if_fail (service_name != NULL, NULL);

	if (!(split = g_strsplit (service_name, ".", 0)))
		return NULL;

	temp1 = g_strjoinv ("/", split);
	g_strfreev (split);
	temp2 = g_strdup_printf ("/%s", temp1);
	g_free (temp1);

	return temp2;
}


/*
 * nm_dialup_service_act_request_failed
 *
 * Clean up after an activation request and tell the dialup manager that it
 * has failed.
 *
 */
static void nm_dialup_service_act_request_failed (NMDialupService *service,
								       NMDialupActRequest *req)
{
	NMDialupConnection *dialup;

	g_return_if_fail (service != NULL);
	g_return_if_fail (req != NULL);

	/* Sanity checks */
	if (nm_dialup_act_request_get_service (req) != service)
		return;

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_service_cancel_callback (service, req);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_FAILED);
	nm_info ("Dialup Activation (%s) failed.", nm_dialup_connection_get_name (dialup));

	nm_dialup_act_request_unref (req);
	nm_dialup_manager_schedule_dialup_activation_failed (service->manager, req);	

	// Signal that we are not connected
	nm_schedule_state_change_signal_broadcast (service->app_data);
}


static void nm_dialup_service_activation_success (NMDialupService *service, NMDialupActRequest *req)
{
	NMDialupConnection *	dialup = NULL;

	g_assert (service != NULL);
	g_assert (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_service_cancel_callback (service, req);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_ACTIVATED);
	nm_info ("Dialup Activation (%s) successful.", nm_dialup_connection_get_name (dialup));

	// Signal that we are connected
	nm_schedule_state_change_signal_broadcast (service->app_data);
}


/*
 * nm_dialup_service_start_connection
 *
 * Kick off the dialup connection process.
 *
 */
void nm_dialup_service_start_connection (NMDialupService *service, NMDialupActRequest *req)
{
	g_return_if_fail (service != NULL);
	g_return_if_fail (req != NULL);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_PREPARE);
	nm_dialup_service_add_watch (service);

	/* Start the daemon if it's not already running */
	nm_dialup_act_request_ref (req);

	// Signal that we are connecting
	nm_schedule_state_change_signal_broadcast (service->app_data);

	if (!dbus_bus_name_has_owner (service->app_data->dbus_connection, service->service, NULL))
		nm_dialup_service_schedule_stage1_daemon_exec (service, req);
	else
		nm_dialup_service_schedule_stage2_daemon_wait (service, req);
}


/*
 * nm_dialup_service_stage_1_daemon_exec
 *
 * Execute the dialup service daemon.
 *
 */
static gboolean nm_dialup_service_stage1_daemon_exec (gpointer user_data)
{
	NMDialupActRequest *	req = (NMDialupActRequest *) user_data;
	NMDialupService *		service;
	NMDialupConnection *	dialup = NULL;
	GPtrArray *		dialup_argv;
	GError *			error = NULL;
	GPid				pid;

	g_assert (req != NULL);

	service = nm_dialup_act_request_get_service (req);
	g_assert (service != NULL);
	g_assert (service->program != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_callback_id (req, 0);

	dialup_argv = g_ptr_array_new ();
	g_ptr_array_add (dialup_argv, service->program);
	g_ptr_array_add (dialup_argv, NULL);

	if (!g_spawn_async (NULL, (char **) dialup_argv->pdata, NULL, 0, NULL, NULL, &pid, &error))
	{
		g_ptr_array_free (dialup_argv, TRUE);
		nm_warning ("(Dialup Service %s): could not launch the dialup service.  error: '%s'.", service->service, error->message);
		g_error_free (error);
		nm_dialup_service_act_request_failed (service, req);
		goto out;
	}
	g_ptr_array_free (dialup_argv, TRUE);
	nm_info ("Dialup Activation (%s) Stage 1 of 4 (Connection Prepare) ran dialup service daemon %s (PID %d)",
			nm_dialup_connection_get_name (dialup), service->service, pid);
	nm_info ("Dialup Activation (%s) Stage 1 of 4 (Connection Prepare) complete.",
			nm_dialup_connection_get_name (dialup));

	nm_dialup_service_schedule_stage2_daemon_wait (service, req);

out:
	return FALSE;
}


static void nm_dialup_service_schedule_stage1_daemon_exec (NMDialupService *service, NMDialupActRequest *req)
{
	GSource *			source = NULL;
	NMDialupConnection *	dialup = NULL;
	guint			id;

	g_assert (service != NULL);
	g_assert (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_PREPARE);
	nm_dialup_service_set_state (service, NM_DIALUP_STATE_SHUTDOWN);

	source = g_idle_source_new ();
	g_source_set_callback (source, (GSourceFunc) nm_dialup_service_stage1_daemon_exec, req, NULL);
	id = g_source_attach (source, service->app_data->main_context);
	nm_dialup_act_request_set_callback_id (req, id);
	g_source_unref (source);
	nm_info ("Dialup Activation (%s) Stage 1 of 4 (Connection Prepare) scheduled...", nm_dialup_connection_get_name (dialup));
}


/*
 * nm_dialup_service_stage2_daemon_wait
 *
 * Wait until the dialup daemon has become active.
 *
 */
static gboolean nm_dialup_service_stage2_daemon_wait (gpointer user_data)
{
	NMDialupActRequest *	req = (NMDialupActRequest *) user_data;
	NMDialupService *		service;
	NMDialupConnection *	dialup = NULL;
	gboolean			service_exists = FALSE;

	g_assert (req != NULL);

	service = nm_dialup_act_request_get_service (req);
	g_assert (service != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_callback_id (req, 0);

	nm_info ("Dialup Activation (%s) Stage 2 of 4 (Connection Prepare Wait) "
				"waiting...", nm_dialup_connection_get_name (dialup));

	service_exists = dbus_bus_name_has_owner (service->app_data->dbus_connection,
			service->service, NULL);
	if (service_exists && (service->state == NM_DIALUP_STATE_STOPPED))
	{
		nm_info ("Dialup Activation (%s) Stage 2 of 4 (Connection Prepare Wait) "
				"complete.", nm_dialup_connection_get_name (dialup));
		nm_dialup_service_schedule_stage3_connect (service, req);
	}
	else if (nm_dialup_act_request_get_daemon_wait_count (req) > 10)
	{
		/* We only wait 2s (10 * 200 milliseconds) for the service to
		 * become available.
		 */
		nm_dialup_service_act_request_failed (service, req);
	}
	else
		nm_dialup_service_schedule_stage2_daemon_wait (service, req);

	return FALSE;
}


static void nm_dialup_service_schedule_stage2_daemon_wait (NMDialupService *service, NMDialupActRequest *req)
{
	GSource *			source = NULL;
	NMDialupConnection *	dialup = NULL;
	guint			id;

	g_assert (service != NULL);
	g_assert (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_PREPARE);

	nm_dialup_act_request_set_daemon_wait_count (req, nm_dialup_act_request_get_daemon_wait_count (req) + 1);

	source = g_timeout_source_new (200);
	g_source_set_callback (source, (GSourceFunc) nm_dialup_service_stage2_daemon_wait, req, NULL);
	id = g_source_attach (source, service->app_data->main_context);
	nm_dialup_act_request_set_callback_id (req, id);
	g_source_unref (source);
	nm_info ("Dialup Activation (%s) Stage 2 of 4 (Connection Prepare Wait) scheduled...", nm_dialup_connection_get_name (dialup));
}


static void nm_dialup_service_stage3_connect_cb (DBusPendingCall *pcall, void *user_data)
{
	DBusMessage *		reply;
	NMDialupActRequest *	req = (NMDialupActRequest *) user_data;
	NMDialupService *		service;
	NMDialupConnection *	dialup;

	g_assert (pcall != NULL);
	g_assert (req != NULL);

	service = nm_dialup_act_request_get_service (req);
	g_assert (service != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_info ("Dialup Activation (%s) Stage 3 of 4 (Connect) reply received.",
			nm_dialup_connection_get_name (dialup));

	if (!(reply = dbus_pending_call_steal_reply (pcall)))
	{
		nm_warning ("(Dialup Service %s): could not obtain dialup service's reply.",
				service->service);
		nm_dialup_service_act_request_failed (service, req);
		goto out;
	}

	if (message_is_error (reply))
	{
		const char *member = dbus_message_get_member (reply);
		char *message;

		if (!dbus_message_get_args (reply, NULL, DBUS_TYPE_STRING, &message, NULL))
			message = (char *) "";

		nm_warning ("(Dialup Service %s): could not start the dialup '%s'.  dbus says: '%s'  '%s'.", 
					service->service, nm_dialup_connection_get_name (dialup), member, message);
		nm_dialup_service_act_request_failed (service, req);
	}
	else
	{
		nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_IP_CONFIG_GET);
		nm_dialup_service_schedule_stage4_ip_config_get_timeout (service, req);
		nm_info ("Dialup Activation (%s) Stage 3 of 4 (Connect) complete, "
				"waiting for IP configuration...", nm_dialup_connection_get_name (dialup));
	}

	dbus_message_unref (reply);

out:
	dbus_pending_call_unref (pcall);
}


static char **
sanitize_dbus_string_array (char **in_array, dbus_uint32_t *in_num)
{
	char ** out_array;

	g_return_val_if_fail (in_num != NULL, NULL);

	if (in_array)
		return in_array;

	out_array = g_malloc0 (sizeof (char *));
	out_array[0] = g_strdup ("");
	*in_num = 1;
	return out_array;
}


static gboolean nm_dialup_service_stage3_connect (gpointer user_data)
{
	NMDialupActRequest *	req = (NMDialupActRequest *) user_data;
	NMDialupService *		service;
	NMDialupConnection *	dialup;
	char *			op;
	const char *		name;
	const char *		user_name;
	char **			password_items;
	dbus_uint32_t		password_count;
	char **			data_items;
	dbus_uint32_t		data_count;
	DBusMessage *		message;
	DBusPendingCall *	pcall = NULL;

	g_assert (req != NULL);

	service = nm_dialup_act_request_get_service (req);
	g_assert (service != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup != NULL);

	nm_dialup_act_request_set_callback_id (req, 0);

	/* Send the start dialup request to the daemon */
	op = construct_op_from_service_name (service->service);
	message = dbus_message_new_method_call (service->service, op, service->service, "startConnection");
	g_free (op);
	if (!message)
	{
		nm_warning ("(Dialup Service %s): couldn't allocate dbus message.", service->service);
		nm_dialup_service_act_request_failed (service, req);
		return FALSE;
	}

	name = nm_dialup_connection_get_name (dialup);
	user_name = nm_dialup_connection_get_user_name (dialup);
	password_items = (char **) nm_dialup_act_request_get_password_items (req, &password_count);
	data_items = (char **) nm_dialup_act_request_get_data_items (req, &data_count);

	/* Ensure that data_items are safe to put through dbus */
	data_items = sanitize_dbus_string_array (data_items, &data_count);

	nm_info ("Dialup Activation (%s) Stage 3 of 4 (Connect) sending connect request.",
			nm_dialup_connection_get_name (dialup));
	dbus_message_append_args (message,
				  DBUS_TYPE_STRING, &name,
				  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &password_items, password_count,
				  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &data_items, data_count,
				  DBUS_TYPE_INVALID);

	dbus_connection_send_with_reply (service->app_data->dbus_connection, message, &pcall, -1);
	if (pcall)
	{
		dbus_pending_call_set_notify (pcall, nm_dialup_service_stage3_connect_cb, req, NULL);
		nm_info ("Dialup Activation (%s) Stage 3 of 4 (Connect) request sent,"
				" waiting for reply...", nm_dialup_connection_get_name (dialup));
	}
	else
		nm_dialup_service_act_request_failed (service, req);
	dbus_message_unref (message);

	return FALSE;
}


static void nm_dialup_service_schedule_stage3_connect (NMDialupService *service, NMDialupActRequest *req)
{
	GSource *			source = NULL;
	NMDialupConnection *	dialup = NULL;
	guint			id;

	g_assert (service != NULL);
	g_assert (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_CONNECT);

	source = g_idle_source_new ();
	g_source_set_callback (source, (GSourceFunc) nm_dialup_service_stage3_connect, req, NULL);
	id = g_source_attach (source, service->app_data->main_context);
	nm_dialup_act_request_set_callback_id (req, id);
	g_source_unref (source);
	nm_info ("Dialup Activation (%s) Stage 3 of 4 (Connect) scheduled...", nm_dialup_connection_get_name (dialup));
}


static gboolean nm_dialup_service_stage4_ip_config_get_timeout (gpointer *user_data)
{
	NMDialupActRequest *	req = (NMDialupActRequest *) user_data;
	NMDialupService *		service;
	NMDialupConnection *	dialup;

	g_assert (req != NULL);

	service = nm_dialup_act_request_get_service (req);
	g_assert (service != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup != NULL);

	nm_dialup_act_request_set_callback_id (req, 0);

	/* If the activation request's state is still IP_CONFIG_GET and we're
	 * in this timeout, cancel activation because it's taken too long.
	 */
	if (nm_dialup_act_request_get_stage (req) == NM_DIALUP_ACT_STAGE_IP_CONFIG_GET)
	{
		nm_info ("Dialup Activation (%s) Stage 4 of 4 (IP Config Get) timeout exceeded.", nm_dialup_connection_get_name (dialup));
		nm_dialup_service_act_request_failed (service, req);
	}

	return FALSE;
}


static void nm_dialup_service_schedule_stage4_ip_config_get_timeout (NMDialupService *service, NMDialupActRequest *req)
{
	GSource *			source = NULL;
	NMDialupConnection *	dialup = NULL;
	guint			id;

	g_assert (service != NULL);
	g_assert (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_IP_CONFIG_GET);

	/* 20 second timeout waiting for IP config signal from dialup service */
	source = g_timeout_source_new (20000);
	g_source_set_callback (source, (GSourceFunc) nm_dialup_service_stage4_ip_config_get_timeout, req, NULL);
	id = g_source_attach (source, service->app_data->main_context);
	nm_dialup_act_request_set_callback_id (req, id);
	g_source_unref (source);
	nm_info ("Dialup Activation (%s) Stage 4 of 4 (IP Config Get) timeout scheduled...", nm_dialup_connection_get_name (dialup));
}


static void nm_dialup_service_cancel_callback (NMDialupService *service, NMDialupActRequest *req)
{
	guint	id;

	g_return_if_fail (service != NULL);
	g_return_if_fail (req != NULL);

	if ((id = nm_dialup_act_request_get_callback_id (req)) != 0)
	{
		g_source_destroy (g_main_context_find_source_by_id (service->app_data->main_context, id));
		nm_dialup_act_request_set_callback_id (req, 0);
	}
}


static gboolean
get_dbus_guint32_helper (DBusMessageIter *iter,
                         guint32 *num,
					char *desc)
{
	if (!dbus_message_iter_next (iter)
	    || (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_UINT32))
	{
		nm_warning ("Error: couldn't get %s from dialup IP Config message.", desc);
		return FALSE;
	}
	dbus_message_iter_get_basic (iter, num);
	return TRUE;
}

/*
static gboolean
get_dbus_string_helper (DBusMessageIter *iter,
                        char **str,
                        char *desc)
{
	if (!dbus_message_iter_next (iter)
	    || (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_STRING))
	{
		nm_warning ("Error: couldn't get %s from dialup IP Config message.", desc);
		return FALSE;
	}
	dbus_message_iter_get_basic (iter, str);
	return TRUE;
}
*/

/*
 * nm_dialup_service_stage4_ip_config_get
 *
 * Configure a device with IPv4 config info in response the the dialup daemon.
 *
 */
static void
nm_dialup_service_stage4_ip_config_get (NMDialupService *service,
                                     NMDialupActRequest *req,
                                     DBusMessage *message)
{
	NMDialupConnection *	dialup;
	guint32			num;
	char *			dev;
	gboolean			success = FALSE;
	DBusMessageIter	iter;
	DBusMessageIter	subiter;
	NMIP4Config *		config;

	g_return_if_fail (service != NULL);
	g_return_if_fail (message != NULL);
	g_return_if_fail (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_info ("Dialup Activation (%s) Stage 4 of 4 (IP Config Get) reply received.",
		nm_dialup_connection_get_name (dialup));

	config = nm_ip4_config_new ();
	nm_ip4_config_set_secondary (config, TRUE);

	dbus_message_iter_init (message, &iter);

	/* First arg: device (STRING) */
	if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
	{
	  nm_warning ("Error: couldn't get dialup device from dialup IP config message.");
	  goto out;
	}
	dbus_message_iter_get_basic (&iter, &dev);

	/* Second arg: IP4 dialup gateway address (UINT32) */
	if (!get_dbus_guint32_helper (&iter, &num, "IP4 gateway"))
		goto out;
	nm_ip4_config_set_gateway (config, num);

	/* Third arg: IP4 dialup Local Address (UINT32) */
	if (!get_dbus_guint32_helper (&iter, &num, "IP4 dialup Local Address"))
		goto out;
	nm_ip4_config_set_address (config, num);

	/* Fourth arg: IP4 dialup Point-to-Point Address (UINT32) */
	if (!get_dbus_guint32_helper (&iter, &num, "IP4 dialup PtP Address"))
		goto out;
	nm_ip4_config_set_ptp_address (config, num);

	/* Fifth arg: IP4 dialup Local Netmask (UINT32) */
	if (!get_dbus_guint32_helper (&iter, &num, "IP4 dialup Local Netmask"))
		goto out;
	/* If no netmask, default to Class C address */
	nm_ip4_config_set_netmask (config, num ? num : 0x00FF);

	/* Sixth arg: IP4 DNS Server Addresses (ARRAY, UINT32) */
	if (   !dbus_message_iter_next (&iter)
	    || (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY))
	{
		nm_warning ("Error: couldn't get IP4 DNS Server Addresses"
		          " from dialup IP Config message.");
		goto out;
	}
	dbus_message_iter_recurse (&iter, &subiter);
	while (dbus_message_iter_get_arg_type (&subiter) == DBUS_TYPE_UINT32)
	{
		dbus_message_iter_get_basic (&subiter, &num);
		if (num)
			nm_ip4_config_add_nameserver (config, num);
		dbus_message_iter_next (&subiter);
	}

	/* Seventh arg: IP4 NBNS Server Addresses (ARRAY, UINT32) */
	if (   !dbus_message_iter_next (&iter)
	    || (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY))
	{
		nm_warning ("Error: couldn't get IP4 NBNS Server Addresses"
		          " from dialup IP Config message.");
		goto out;
	}
	dbus_message_iter_recurse (&iter, &subiter);
	while (dbus_message_iter_get_arg_type (&subiter) == DBUS_TYPE_UINT32)
	{
		dbus_message_iter_get_basic (&subiter, &num);
		/* We don't do anything with these yet */
		dbus_message_iter_next (&subiter);
	}

	/* Ninth arg: DNS Domain (STRING)
	if (!get_dbus_string_helper (&iter, &str, "DNS Domain"))
		goto out;
	if (strlen (str))
		nm_ip4_config_add_domain (config, str);
	*/


#ifdef NM_DEBUG_DIALUP_CONFIG
	print_dialup_config (ip4_dialup_gateway,
			     dev,
			     ip4_address,
			     ip4_ptp_address,
			     ip4_netmask,
			     ip4_dns,
			     ip4_dns_len,
			     ip4_nbns,
			     ip4_nbns_len);
#endif

	if (nm_dialup_connection_set_config (dialup, dev, config))
	{
		nm_info ("Dialup Activation (%s) Stage 4 of 4 (IP Config Get) complete.",
				nm_dialup_connection_get_name (dialup));
		success = TRUE;
		nm_dialup_service_activation_success (service, req);
	}

out:
	if (!success)
	{
		nm_ip4_config_unref (config);
		nm_warning ("(Dialup Service %s): did not receive valid IP config information.", service->service);
		nm_dialup_service_act_request_failed (service, req);
	}
}


static void nm_dialup_service_stop_connection_internal (NMDialupService *service)
{
	DBusMessage *		message;
	char *			op;

	g_return_if_fail (service != NULL);

	/* Construct a new method call with the correct service and object path */
	op = construct_op_from_service_name (service->service);
	if ((message = dbus_message_new_method_call (service->service, op, service->service, "stopConnection")))
	{
		dbus_connection_send (service->app_data->dbus_connection, message, NULL);
		dbus_message_unref (message);
	}
	else
		nm_warning ("(Dialup Service %s): couldn't allocate dbus message.", service->service);

	g_free (op);
}


void nm_dialup_service_stop_connection (NMDialupService *service, NMDialupActRequest *req)
{
	NMDialupConnection *dialup;

	g_return_if_fail (service != NULL);
	g_return_if_fail (req != NULL);

	dialup = nm_dialup_act_request_get_connection (req);
	g_assert (dialup);

	nm_dialup_service_cancel_callback (service, req);
	nm_dialup_act_request_set_stage (req, NM_DIALUP_ACT_STAGE_DISCONNECTED);

	/* Ensure we can stop the connection in this state */
	if ((service->state != NM_DIALUP_STATE_STARTED) && (service->state != NM_DIALUP_STATE_STARTING))
	{
		nm_warning ("(Dialup Service %s): could not stop connection '%s' because service was %d.", 
					service->service, nm_dialup_connection_get_name (dialup), service->state);
		return;
	}

	nm_dialup_service_stop_connection_internal (service);
	nm_dialup_service_set_state (service, NM_DIALUP_STATE_STOPPED);
}


static void nm_dialup_service_add_watch (NMDialupService *service)
{
	char *	match_string = NULL;

	g_return_if_fail (service != NULL);

	if (service->watch_active)
		return;

	/* Add a dbus filter for this connection's service name so its signals
	 * get delivered to us.
	 */
	match_string = g_strdup_printf ("type='signal',"
							  "interface='%s',"
							  "sender='%s'", service->service, service->service);
	dbus_bus_add_match (service->app_data->dbus_connection, match_string, NULL);
	g_free (match_string);
	service->watch_active = TRUE;
}


static void nm_dialup_service_remove_watch (NMDialupService *service)
{
	char *	match_string = NULL;

	g_return_if_fail (service != NULL);

	if (!service->watch_active)
		return;

	match_string = g_strdup_printf ("type='signal',"
							  "interface='%s',"
							  "sender='%s'", service->service, service->service);
	dbus_bus_remove_match (service->app_data->dbus_connection, match_string, NULL);
	g_free (match_string);
	service->watch_active = FALSE;
}


static inline gboolean same_service_name (NMDialupService *service, NMDialupConnection *dialup)
{
	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (dialup != NULL, FALSE);

	return (!strcmp (nm_dialup_service_get_service_name (service), nm_dialup_connection_get_service_name (dialup)));
}


gboolean nm_dialup_service_name_owner_changed (NMDialupService *service, NMDialupActRequest *req, const char *old, const char *new)
{
	NMDialupConnection *	dialup;
	gboolean			valid_dialup = FALSE;
	gboolean			old_owner_good = (old && strlen (old));
	gboolean			new_owner_good = (new && strlen (new));

	g_return_val_if_fail (service != NULL, FALSE);

	if (req && (dialup = nm_dialup_act_request_get_connection (req)))
		valid_dialup = same_service_name (service, dialup);

	if (!old_owner_good && new_owner_good)
	{
		/* dialup service started. */
		nm_dialup_service_add_watch (service);
		nm_dialup_service_set_state (service, NM_DIALUP_STATE_INIT);
	}
	else if (old_owner_good && !new_owner_good)
	{
		/* dialup service went away. */
		nm_dialup_service_set_state (service, NM_DIALUP_STATE_SHUTDOWN);
		nm_dialup_service_remove_watch (service);

		if (valid_dialup)
		{
			nm_dialup_act_request_unref (req);
			nm_dialup_manager_schedule_dialup_connection_died (service->manager, req);
		}
	}

	return TRUE;
}


gboolean nm_dialup_service_process_signal (NMDialupService *service, NMDialupActRequest *req, DBusMessage *message)
{
	NMDialupConnection *	dialup = NULL;
	gboolean			valid_dialup = FALSE;

	g_return_val_if_fail (service != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	if (req && (dialup = nm_dialup_act_request_get_connection (req)))
		valid_dialup = same_service_name (service, dialup);

	if (    dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_LOGIN_FAILED)
		|| dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_LAUNCH_FAILED)
		|| dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_CONNECT_FAILED)
		|| dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_DIALUP_CONFIG_BAD)
		|| dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_IP_CONFIG_BAD))
	{
		const char *	member = dbus_message_get_member (message);
		char *		error_msg;

		if (valid_dialup)
		{
			if (!dbus_message_get_args (message, NULL, DBUS_TYPE_STRING, &error_msg, DBUS_TYPE_INVALID))
				error_msg = (char *) "";
			nm_warning ("Dialup failed for service '%s', signal '%s', with message '%s'.", service->service, member, error_msg);
			nm_dbus_dialup_signal_dialup_failed (service->app_data->dbus_connection, member, dialup, error_msg);
			/* Don't deal with dialup Connection stopping here, we'll do that when we get the STOPPED or STOPPING signal below */
		}
	}
	else if (dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_STATE_CHANGE))
	{
		dbus_uint32_t old_state_int;
		dbus_uint32_t new_state_int;

		if (dbus_message_get_args (message, NULL, DBUS_TYPE_UINT32, &old_state_int, DBUS_TYPE_UINT32, &new_state_int, DBUS_TYPE_INVALID))
		{
			NMDialupState	old_state = (NMDialupState) old_state_int;
			NMDialupState	new_state = (NMDialupState) new_state_int;

			nm_info ("Dialup service '%s' signaled state change %d -> %d.", service->service, old_state, new_state);
			nm_dialup_service_set_state (service, new_state);

			/* If the dialup daemon state is now stopped and it was starting, clear the active connection */
			if (((new_state == NM_DIALUP_STATE_STOPPED) || (new_state == NM_DIALUP_STATE_SHUTDOWN) || (new_state == NM_DIALUP_STATE_STOPPING))
				&& ((old_state == NM_DIALUP_STATE_STARTED) || (old_state == NM_DIALUP_STATE_STARTING))
				&& valid_dialup)
			{
				nm_dialup_act_request_unref (req);
				nm_dialup_manager_schedule_dialup_connection_died (service->manager, req);
			}
		}
	}
	else if (valid_dialup && dbus_message_is_signal (message, service->service, NM_DBUS_DIALUP_SIGNAL_IP4_CONFIG))
		nm_dialup_service_stage4_ip_config_get (service, req, message);

	return TRUE;
}

#ifdef NM_DEBUG_DIALUP_CONFIG
/*
 *  Prints config returned from the service daemo
 */
static void print_dialup_config (guint32 ip4_dialup_gateway,
				 const char *dev,
				 guint32 ip4_address,
				 guint32 ip4_ptp_address,
				 guint32 ip4_netmask,
				 guint32 *ip4_dns,
				 guint32 ip4_dns_len,
				 guint32 *ip4_nbns,
				 guint32 ip4_nbns_len)
{
	struct in_addr	temp_addr;
	guint32 		i;

	temp_addr.s_addr = ip4_dialup_gateway;
	nm_info ("Dialup Gateway: %s", inet_ntoa (temp_addr));
	nm_info ("Device: %s", dev);
	temp_addr.s_addr = ip4_address;
	nm_info ("Internal IP4 Address: %s", inet_ntoa (temp_addr));
	temp_addr.s_addr = ip4_netmask;
	nm_info ("Internal IP4 Netmask: %s", inet_ntoa (temp_addr));
	temp_addr.s_addr = ip4_ptp_address;
	nm_info ("Internal IP4 Point-to-Point Address: %s", inet_ntoa (temp_addr));
	nm_info ("Maximum Segment Size (MSS): %d", mss);

	for (i = 0; i < ip4_dns_len; i++)
	{
		if (ip4_dns[i] != 0)
		{
			temp_addr.s_addr = ip4_dns[i];
			nm_info ("Internal IP4 DNS: %s", inet_ntoa (temp_addr));
		}
	}

	for (i = 0; i < ip4_nbns_len; i++)
	{
		if (ip4_nbns[i] != 0)
		{
			temp_addr.s_addr = ip4_nbns[i];
			nm_info ("Internal IP4 NBNS: %s", inet_ntoa (temp_addr));
		}
	}
}

#endif
