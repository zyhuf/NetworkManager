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
 * Copyright (C) 2005 - 2011 Red Hat, Inc.
 * Copyright (C) 2007 - 2008 Novell, Inc.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>

#include "nm-activation-request.h"
#include "nm-marshal.h"
#include "nm-logging.h"
#include "nm-setting-wireless-security.h"
#include "nm-setting-8021x.h"
#include "nm-dbus-manager.h"
#include "nm-device.h"
#include "nm-properties-changed-signal.h"
#include "nm-active-connection.h"
#include "nm-dbus-glib-types.h"
#include "nm-active-connection-glue.h"
#include "nm-settings-connection.h"
#include "compat/nm-compat-act-request.h"


G_DEFINE_TYPE (NMActRequest, nm_act_request, G_TYPE_OBJECT)

#define NM_ACT_REQUEST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                       NM_TYPE_ACT_REQUEST, \
                                       NMActRequestPrivate))

enum {
	PROPERTIES_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	char *table;
	char *rule;
} ShareRule;

typedef struct {
	gboolean disposed;

	NMConnection *connection;

	GSList *secrets_calls;

	char *specific_object;
	NMDevice *device;
	gboolean user_requested;
	gulong user_uid;

	NMActiveConnectionState state;
	gboolean is_default;
	gboolean is_default6;
	gboolean shared;
	GSList *share_rules;

	char *ac_path;

	gboolean assumed;

	NMCompatActRequest *compat;
} NMActRequestPrivate;

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_UUID,
	PROP_SPECIFIC_OBJECT,
	PROP_DEVICES,
	PROP_STATE,
	PROP_DEFAULT,
	PROP_DEFAULT6,
	PROP_VPN,

	LAST_PROP
};

/*******************************************************************/

typedef struct {
	NMActRequest *self;
	guint32 call_id;
	NMActRequestSecretsFunc callback;
	gpointer callback_data;

	char *setting_name;
	DBusGProxy *user_proxy;
} GetSecretsInfo;

static void
free_get_secrets_info (GetSecretsInfo *info)
{
	g_free (info->setting_name);
	if (info->user_proxy)
		g_object_unref (info->user_proxy);
	memset (info, 0, sizeof (*info));
	g_free (info);
}

static void
user_get_secrets_cb (DBusGProxy *proxy,
                     DBusGProxyCall *call,
                     gpointer user_data)
{
	GetSecretsInfo *info = user_data;
	NMActRequestPrivate *priv;
	GHashTable *settings = NULL;
	GError *error = NULL;

	nm_log_dbg (LOGD_SETTINGS, "(%p) user secrets request reply", info);

	g_return_if_fail (info != NULL);
	g_return_if_fail (info->setting_name);

	priv = NM_ACT_REQUEST_GET_PRIVATE (info->self);

	if (dbus_g_proxy_end_call (proxy, call, &error,
	                           DBUS_TYPE_G_MAP_OF_MAP_OF_VARIANT, &settings,
	                           G_TYPE_INVALID)) {
		nm_log_dbg (LOGD_SETTINGS, "got user connection secrets size %d", g_hash_table_size (settings));
		nm_connection_update_secrets (priv->connection, info->setting_name, settings, &error);

#if 0
		if (g_hash_table_lookup (settings, info->setting_name)) {
			GHashTableIter iter;
			const char *setting_name = NULL;
			GHashTable *setting = NULL;
			g_hash_table_iter_init (&iter, settings);
			while (g_hash_table_iter_next (&iter, (gpointer) &setting_name, (gpointer) &setting)) {
				GHashTableIter setting_iter;
				const char *key = NULL;
				GValue *val = NULL;

				g_hash_table_iter_init (&setting_iter, setting);
				while (g_hash_table_iter_next (&setting_iter, (gpointer) &key, (gpointer) &val))
					g_message ("   %s => %s", key, g_strdup_value_contents (val));
			}
		} else {
			GHashTableIter iter;
			const char *key = NULL;
			GValue *val = NULL;

			g_hash_table_iter_init (&iter, settings);
			while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
				g_message ("   %s => %s", key, g_strdup_value_contents (val));
		}
#endif
		g_hash_table_destroy (settings);
	} else {
		nm_log_warn (LOGD_SETTINGS, "failed to get user connection secrets: %s", error->message);
	}

	priv->secrets_calls = g_slist_remove (priv->secrets_calls, info);
	info->callback (info->self, info->call_id, priv->connection, error, info->callback_data);
	g_clear_error (&error);

	free_get_secrets_info (info);
}

static guint32
user_get_secrets (NMActRequest *self,
                  const char *setting_name,
                  guint32 flags,
                  const char *hint,
                  GetSecretsInfo *info)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (self);
	NMDBusManager *dbus_mgr;
	DBusGConnection *bus;
	GPtrArray *hints;
	static guint32 counter = 3000000000u;  /* start high to avoid collision with system connections */

	/* User connection */
	dbus_mgr = nm_dbus_manager_get ();
	bus = nm_dbus_manager_get_connection (dbus_mgr);
	info->user_proxy = dbus_g_proxy_new_for_name (bus,
	                                              "org.freedesktop.NetworkManagerUserSettings",
	                                              nm_connection_get_path (priv->connection),
	                                              NM_DBUS_IFACE_SETTINGS_CONNECTION_SECRETS);
	g_object_unref (dbus_mgr);

	if (!info->user_proxy) {
		nm_log_warn (LOGD_SETTINGS, "could not create user connection secrets proxy");
		return 0;
	}

	hints = g_ptr_array_sized_new (1);
	if (hint)
		g_ptr_array_add (hints, (gpointer) hint);

	dbus_g_proxy_begin_call_with_timeout (info->user_proxy, "GetSecrets",
	                                      user_get_secrets_cb,
	                                      info,
	                                      NULL,
	                                      120000,
	                                      G_TYPE_STRING, setting_name,
	                                      DBUS_TYPE_G_ARRAY_OF_STRING, hints,
	                                      G_TYPE_BOOLEAN, (flags & NM_SETTINGS_GET_SECRETS_FLAG_REQUEST_NEW),
	                                      G_TYPE_INVALID);
	nm_log_dbg (LOGD_SETTINGS, "(%p) new user secrets request", info);

	g_ptr_array_free (hints, TRUE);
	return counter++;
}

static void
get_secrets_cb (NMSettingsConnection *connection,
                guint32 call_id,
                const char *agent_username,
                const char *setting_name,
                GError *error,
                gpointer user_data)
{
	GetSecretsInfo *info = user_data;
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (info->self);

	g_return_if_fail (info->call_id == call_id);
	priv->secrets_calls = g_slist_remove (priv->secrets_calls, info);

	info->callback (info->self, call_id, NM_CONNECTION (connection), error, info->callback_data);
	free_get_secrets_info (info);
}

guint32
nm_act_request_get_secrets (NMActRequest *self,
                            const char *setting_name,
                            NMSettingsGetSecretsFlags flags,
                            const char *hint,
                            NMActRequestSecretsFunc callback,
                            gpointer callback_data)
{
	NMActRequestPrivate *priv;
	GetSecretsInfo *info;
	guint32 call_id;

	g_return_val_if_fail (self, 0);
	g_return_val_if_fail (NM_IS_ACT_REQUEST (self), 0);

	priv = NM_ACT_REQUEST_GET_PRIVATE (self);

	info = g_malloc0 (sizeof (GetSecretsInfo));
	info->self = self;
	info->callback = callback;
	info->callback_data = callback_data;
	info->setting_name = g_strdup (setting_name);

	if (NM_IS_SETTINGS_CONNECTION (priv->connection)) {
		call_id = nm_settings_connection_get_secrets (NM_SETTINGS_CONNECTION (priv->connection),
			                                          priv->user_requested,
			                                          priv->user_uid,
			                                          setting_name,
			                                          flags,
			                                          hint,
			                                          get_secrets_cb,
			                                          info,
			                                          NULL);
	} else
		call_id = user_get_secrets (self, setting_name, flags, hint, info);

	if (call_id > 0) {
		info->call_id = call_id;
		priv->secrets_calls = g_slist_append (priv->secrets_calls, info);
	} else
		free_get_secrets_info (info);

	return call_id;
}

void
nm_act_request_cancel_secrets (NMActRequest *self, guint32 call_id)
{
	NMActRequestPrivate *priv;
	GSList *iter;

	g_return_if_fail (self);
	g_return_if_fail (NM_IS_ACT_REQUEST (self));
	g_return_if_fail (call_id > 0);

	priv = NM_ACT_REQUEST_GET_PRIVATE (self);

	for (iter = priv->secrets_calls; iter; iter = g_slist_next (iter)) {
		GetSecretsInfo *info = iter->data;

		/* Remove the matching info */
		if (info->call_id == call_id) {
			priv->secrets_calls = g_slist_remove_link (priv->secrets_calls, iter);
			g_slist_free (iter);

			if (NM_IS_SETTINGS_CONNECTION (priv->connection))
				nm_settings_connection_cancel_secrets (NM_SETTINGS_CONNECTION (priv->connection), call_id);
			/* For user connections destroying the proxy cancels the request */
			free_get_secrets_info (info);
			break;
		}
	}
}

/*******************************************************************/

NMConnection *
nm_act_request_get_connection (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->connection;
}

const char *
nm_act_request_get_specific_object (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->specific_object;
}

void
nm_act_request_set_specific_object (NMActRequest *req,
                                    const char *specific_object)
{
	NMActRequestPrivate *priv;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));
	g_return_if_fail (specific_object != NULL);

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);

	g_free (priv->specific_object);
	priv->specific_object = g_strdup (specific_object);
}

gboolean
nm_act_request_get_user_requested (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->user_requested;
}

const char *
nm_act_request_get_active_connection_path (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->ac_path;
}

void
nm_act_request_set_default (NMActRequest *req, gboolean is_default)
{
	NMActRequestPrivate *priv;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	if (priv->is_default == is_default)
		return;

	priv->is_default = is_default;
	g_object_notify (G_OBJECT (req), NM_ACTIVE_CONNECTION_DEFAULT);
}

gboolean
nm_act_request_get_default (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->is_default;
}

void
nm_act_request_set_default6 (NMActRequest *req, gboolean is_default6)
{
	NMActRequestPrivate *priv;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	if (priv->is_default6 == is_default6)
		return;

	priv->is_default6 = is_default6;
	g_object_notify (G_OBJECT (req), NM_ACTIVE_CONNECTION_DEFAULT6);
}

gboolean
nm_act_request_get_default6 (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->is_default6;
}

GObject *
nm_act_request_get_device (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return G_OBJECT (NM_ACT_REQUEST_GET_PRIVATE (req)->device);
}

gboolean
nm_act_request_get_assumed (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->assumed;
}

/********************************************************************/

static void
clear_share_rules (NMActRequest *req)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	GSList *iter;

	for (iter = priv->share_rules; iter; iter = g_slist_next (iter)) {
		ShareRule *rule = (ShareRule *) iter->data;

		g_free (rule->table);
		g_free (rule->rule);
		g_free (rule);
	}

	g_slist_free (priv->share_rules);
	priv->share_rules = NULL;
}

static void
share_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

void
nm_act_request_set_shared (NMActRequest *req, gboolean shared)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	GSList *list, *iter;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));

	NM_ACT_REQUEST_GET_PRIVATE (req)->shared = shared;

	/* Tear the rules down in reverse order when sharing is stopped */
	list = g_slist_copy (priv->share_rules);
	if (!shared)
		list = g_slist_reverse (list);

	/* Send the rules to iptables */
	for (iter = list; iter; iter = g_slist_next (iter)) {
		ShareRule *rule = (ShareRule *) iter->data;
		char *envp[1] = { NULL };
		char **argv;
		char *cmd;

		cmd = g_strdup_printf ("%s --table %s %s %s",
		                       IPTABLES_PATH,
		                       rule->table,
		                       shared ? "--insert" : "--delete",
		                       rule->rule);
		if (!cmd)
			continue;

		argv = g_strsplit (cmd, " ", 0);
		if (argv && argv[0]) {
			int status;
			GError *error = NULL;

			nm_log_info (LOGD_SHARING, "Executing: %s", cmd);
			if (!g_spawn_sync ("/", argv, envp, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			                   share_child_setup, NULL, NULL, NULL, &status, &error)) {
				nm_log_warn (LOGD_SHARING, "Error executing command: (%d) %s",
				             error ? error->code : -1,
				             (error && error->message) ? error->message : "(unknown)");
				g_clear_error (&error);
			} else if (WEXITSTATUS (status)) {
				nm_log_warn (LOGD_SHARING, "** Command returned exit status %d.",
				             WEXITSTATUS (status));
			}
		}
		g_free (cmd);
		if (argv)
			g_strfreev (argv);
	}

	g_slist_free (list);

	/* Clear the share rule list when sharing is stopped */
	if (!shared)
		clear_share_rules (req);
}

gboolean
nm_act_request_get_shared (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->shared;
}

void
nm_act_request_add_share_rule (NMActRequest *req,
                               const char *table,
                               const char *table_rule)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	ShareRule *rule;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));
	g_return_if_fail (table != NULL);
	g_return_if_fail (table_rule != NULL);

	rule = g_malloc0 (sizeof (ShareRule));
	rule->table = g_strdup (table);
	rule->rule = g_strdup (table_rule);
	priv->share_rules = g_slist_append (priv->share_rules, rule);
}

/********************************************************************/

static void
device_state_changed (NMDevice *device,
                      NMDeviceState new_state,
                      NMDeviceState old_state,
                      NMDeviceStateReason reason,
                      gpointer user_data)
{
	NMActRequest *self = NM_ACT_REQUEST (user_data);
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (self);
	NMActiveConnectionState new_ac_state;
	gboolean new_default = FALSE, new_default6 = FALSE;

	/* Set NMActiveConnection state based on the device's state */
	switch (new_state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
	case NM_DEVICE_STATE_IP_CHECK:
	case NM_DEVICE_STATE_SECONDARIES:
		new_ac_state = NM_ACTIVE_CONNECTION_STATE_ACTIVATING;
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		new_ac_state = NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
		new_default = priv->is_default;
		new_default6 = priv->is_default6;
		break;
	case NM_DEVICE_STATE_DEACTIVATING:
		new_ac_state = NM_ACTIVE_CONNECTION_STATE_DEACTIVATING;
		break;
	default:
		new_ac_state = NM_ACTIVE_CONNECTION_STATE_UNKNOWN;
		new_default = new_default6 = FALSE;
		break;
	}

	if (new_ac_state != priv->state) {
		priv->state = new_ac_state;
		g_object_notify (G_OBJECT (self), NM_ACTIVE_CONNECTION_STATE);
	}

	if (new_default != priv->is_default) {
		priv->is_default = new_default;
		g_object_notify (G_OBJECT (self), NM_ACTIVE_CONNECTION_DEFAULT);
	}

	if (new_default6 != priv->is_default6) {
		priv->is_default6 = new_default6;
		g_object_notify (G_OBJECT (self), NM_ACTIVE_CONNECTION_DEFAULT6);
	}
}

gpointer
nm_act_request_get_compat (NMActRequest *req)
{
	return NM_ACT_REQUEST_GET_PRIVATE (req)->compat;
}

/********************************************************************/

NMActRequest *
nm_act_request_new (NMConnection *connection,
                    const char *specific_object,
                    gboolean user_requested,
                    gulong user_uid,
                    gboolean assumed,
                    gpointer *device)
{
	GObject *object;
	NMActRequestPrivate *priv;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);
	g_return_val_if_fail (NM_DEVICE (device), NULL);

	object = g_object_new (NM_TYPE_ACT_REQUEST, NULL);
	if (!object)
		return NULL;

	priv = NM_ACT_REQUEST_GET_PRIVATE (object);

	priv->connection = g_object_ref (connection);
	if (specific_object)
		priv->specific_object = g_strdup (specific_object);

	priv->device = NM_DEVICE (device);
	g_signal_connect (device, "state-changed",
	                  G_CALLBACK (device_state_changed),
	                  NM_ACT_REQUEST (object));

	priv->user_uid = user_uid;
	priv->user_requested = user_requested;
	priv->assumed = assumed;

	return NM_ACT_REQUEST (object);
}

static void
nm_act_request_init (NMActRequest *req)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	NMDBusManager *dbus_mgr;

	priv->ac_path = nm_active_connection_get_next_object_path ();
	priv->state = NM_ACTIVE_CONNECTION_STATE_UNKNOWN;

	dbus_mgr = nm_dbus_manager_get ();
	dbus_g_connection_register_g_object (nm_dbus_manager_get_connection (dbus_mgr),
	                                     priv->ac_path,
	                                     G_OBJECT (req));

	priv->compat = nm_compat_act_request_new (req, nm_dbus_manager_get_connection (dbus_mgr));
	g_object_unref (dbus_mgr);
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);
	GPtrArray *devices;

	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_boxed (value, nm_connection_get_path (priv->connection));
		break;
	case PROP_UUID:
		g_value_set_string (value, nm_connection_get_uuid (priv->connection));
		break;
	case PROP_SPECIFIC_OBJECT:
		if (priv->specific_object)
			g_value_set_boxed (value, priv->specific_object);
		else
			g_value_set_boxed (value, "/");
		break;
	case PROP_DEVICES:
		devices = g_ptr_array_sized_new (1);
		g_ptr_array_add (devices, g_strdup (nm_device_get_path (priv->device)));
		g_value_take_boxed (value, devices);
		break;
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
		break;
	case PROP_DEFAULT:
		g_value_set_boolean (value, priv->is_default);
		break;
	case PROP_DEFAULT6:
		g_value_set_boolean (value, priv->is_default6);
		break;
	case PROP_VPN:
		g_value_set_boolean (value, FALSE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
dispose (GObject *object)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);
	GSList *iter;

	if (priv->disposed) {
		G_OBJECT_CLASS (nm_act_request_parent_class)->dispose (object);
		return;
	}
	priv->disposed = TRUE;

	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->device),
	                                      G_CALLBACK (device_state_changed),
	                                      NM_ACT_REQUEST (object));

	/* Clear any share rules */
	nm_act_request_set_shared (NM_ACT_REQUEST (object), FALSE);

	/* Kill any in-progress secrets requests */
	g_assert (priv->connection);
	for (iter = priv->secrets_calls; iter; iter = g_slist_next (iter)) {
		GetSecretsInfo *info = iter->data;

		if (NM_IS_SETTINGS_CONNECTION (priv->connection))
			nm_settings_connection_cancel_secrets (NM_SETTINGS_CONNECTION (priv->connection), info->call_id);
		free_get_secrets_info (info);
	}
	g_slist_free (priv->secrets_calls);

	g_object_unref (priv->connection);
	g_object_unref (priv->compat);

	G_OBJECT_CLASS (nm_act_request_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);

	g_free (priv->specific_object);
	g_free (priv->ac_path);

	clear_share_rules (NM_ACT_REQUEST (object));

	G_OBJECT_CLASS (nm_act_request_parent_class)->finalize (object);
}

static void
nm_act_request_class_init (NMActRequestClass *req_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (req_class);

	g_type_class_add_private (req_class, sizeof (NMActRequestPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* properties */
    nm_active_connection_install_properties (object_class,
                                             PROP_CONNECTION,
                                             PROP_UUID,
                                             PROP_SPECIFIC_OBJECT,
                                             PROP_DEVICES,
                                             PROP_STATE,
                                             PROP_DEFAULT,
                                             PROP_DEFAULT6,
                                             PROP_VPN);

	/* Signals */
	signals[PROPERTIES_CHANGED] = 
		nm_properties_changed_signal_new (object_class,
		                                  G_STRUCT_OFFSET (NMActRequestClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (req_class),
	                                 &dbus_glib_nm_active_connection_object_info);
}

