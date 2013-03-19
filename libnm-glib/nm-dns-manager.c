/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * libnm_glib -- Access network status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2013 Red Hat, Inc.
 */

#include <config.h>

#include <dbus/dbus-glib.h>

#include "nm-dns-manager.h"

static void nm_dns_manager_initable_iface_init (GInitableIface *iface);
static void nm_dns_manager_async_initable_iface_init (GAsyncInitableIface *iface);
static GInitableIface *nm_dns_manager_parent_initable_iface;
static GAsyncInitableIface *nm_dns_manager_parent_async_initable_iface;

G_DEFINE_TYPE_WITH_CODE (NMDnsManager, nm_dns_manager, NM_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, nm_dns_manager_initable_iface_init);
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, nm_dns_manager_async_initable_iface_init);
                         )

#define NM_DNS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DNS_MANAGER, NMDnsManagerPrivate))

typedef struct {
	DBusGProxy *manager_proxy;
	DBusGProxy *bus_proxy;
	gboolean manager_running;

	GPtrArray *nameservers;
	char *domain;
	GPtrArray *searches;
	char *nis_domain;
	GPtrArray *nis_servers;
	GPtrArray *wins_servers;

	NMSplitDnsConfig *split_dns;

} NMDnsManagerPrivate;

enum {
	PROP_0,

	PROP_NAMESERVERS,
	PROP_DOMAIN,
	PROP_SEARCHES,
	PROP_NIS_DOMAIN,
	PROP_NIS_SERVERS,
	PROP_WINS_SERVERS,
	PROP_SPLIT_DNS,

	LAST_PROP
};

static void proxy_name_owner_changed (DBusGProxy *proxy,
                                      const char *name,
                                      const char *old_owner,
                                      const char *new_owner,
                                      gpointer user_data);

static void
nm_dns_manager_init (NMDnsManager *client)
{
}

static void
register_properties (NMDnsManager *client)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (client);
	const NMPropertiesInfo property_info[] = {
		{ NM_DNS_MANAGER_NAMESERVERS,  &priv->nameservers },
		{ NM_DNS_MANAGER_DOMAIN,       &priv->domain },
		{ NM_DNS_MANAGER_SEARCHES,     &priv->searches },
		{ NM_DNS_MANAGER_NIS_DOMAIN,   &priv->nis_domain },
		{ NM_DNS_MANAGER_NIS_SERVERS,  &priv->nis_servers },
		{ NM_DNS_MANAGER_WINS_SERVERS, &priv->wins_servers },
		{ NM_DNS_MANAGER_SPLIT_DNS,    &priv->split_dns, demarshal_split_dns },
		{ NULL },
	};

	_nm_object_register_properties (NM_OBJECT (client),
	                                priv->manager_proxy,
	                                property_info);
}

/**
 * nm_dns_manager_get_nameservers:
 * @manager: a #NMDnsManager
 *
 * Gets the nameservers (as IP address strings).
 *
 * Returns: (transfer none) (element-type utf8): a #GPtrArray
 * containing the nameservers. The returned array is owned by the
 * #NMDnsManager object and should not be modified.
 **/
const GPtrArray *
nm_dns_manager_get_nameservers (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return handle_ptr_array_return (NM_DNS_MANAGER_GET_PRIVATE (manager)->nameservers);
}

/**
 * nm_dns_manager_get_domain:
 * @manager: a #NMDnsManager
 *
 * Gets the DNS domain
 *
 * Returns: the DNS domain
 **/
const char *
nm_dns_manager_get_domain (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return NM_DNS_MANAGER_GET_PRIVATE (manager)->domain;
}

/**
 * nm_dns_manager_get_searches:
 * @manager: a #NMDnsManager
 *
 * Gets the DNS search domains
 *
 * Returns: (transfer none) (element-type utf8): a #GPtrArray
 * containing the DNS search domains. The returned array is owned by the
 * #NMDnsManager object and should not be modified.
 **/
const GPtrArray *
nm_dns_manager_get_searches (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return handle_ptr_array_return (NM_DNS_MANAGER_GET_PRIVATE (manager)->searches);
}

/**
 * nm_dns_manager_get_nis_domain:
 * @manager: a #NMDnsManager
 *
 * Gets the NIS domain
 *
 * Returns: the NIS domain
 **/
const char *
nm_dns_manager_get_nis_domain (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return NM_DNS_MANAGER_GET_PRIVATE (manager)->nis_domain;
}

/**
 * nm_dns_manager_get_nis_servers:
 * @manager: a #NMDnsManager
 *
 * Gets the NIS servers (as IP address strings).
 *
 * Returns: (transfer none) (element-type utf8): a #GPtrArray
 * containing the NIS servers. The returned array is owned by the
 * #NMDnsManager object and should not be modified.
 **/
const GPtrArray *
nm_dns_manager_get_nis_servers (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return handle_ptr_array_return (NM_DNS_MANAGER_GET_PRIVATE (manager)->nis_servers);
}

/**
 * nm_dns_manager_get_wins_nameservers:
 * @manager: a #NMDnsManager
 *
 * Gets the WINS servers (as IP address strings).
 *
 * Returns: (transfer none) (element-type utf8): a #GPtrArray
 * containing the WINS servers. The returned array is owned by the
 * #NMDnsManager object and should not be modified.
 **/
const GPtrArray *
nm_dns_manager_get_wins_servers (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return handle_ptr_array_return (NM_DNS_MANAGER_GET_PRIVATE (manager)->wins_servers);
}

/**
 * nm_dns_manager_get_split_dns:
 * @manager: a #NMDnsManager
 *
 * Gets the "split DNS" configuration; see the documentation for
 * #NMSplitDnsConfig.
 *
 * Returns: (transfer none): the #NMSplitDnsConfig. The returned
 * object is owned by the #NMDnsManager object and should not be
 * modified.
 **/
const NMSplitDnsConfig *
nm_dns_manager_get_wins_servers (NMDnsManager *manager)
{
	g_return_val_if_fail (NM_IS_DNS_MANAGER (client), NULL);

	_nm_object_ensure_inited (NM_OBJECT (manager));

	return handle_ptr_array_return (NM_DNS_MANAGER_GET_PRIVATE (manager)->split_dns);
}

/****************************************************************/

/**
 * NMSplitDnsConfig:
 * @networks: (element-type NMSplitDnsNetwork): an array of
 *   #NMSplitDnsNetworks defining the networks that should be
 *   handled separately for DNS purposes.
 * @fallback_servers: (element-type utf8): an array of DNS
 *   servers (as IP address strings) to use for lookups not
 *   matched by any element of @networks.
 *
 * Configuration information for doing "split DNS"
 */

/**
 * NMSplitDnsNetwork:
 * @interface: the interface that this applies to
 * @nameservers: (element-type utf8): an array of DNS servers
 *   (as IP address strings)
 * @domains: (element-type utf8): an array of domains that
 *   @nameservers handles
 * @ip_ranges: (element-type utf8): an array of IP address ranges
 *   (in "address/prefix" form) that @nameservers handles
 *   reverse-resolution for.
 *
 * The configuration information for a single "split DNS" network.
 */

static GPtrArray 

static gboolean
demarshal_split_dns (NMObject *object, GParamSpec *pspec, GValue *value, gpointer field)
{
	NMDevicePrivate *priv = NM_DEVICE_GET_PRIVATE (object);
	GPtrArray *networks;
	GValueArray *vnetwork;
	char **fallback, **strings;
	NMSplitDnsNetwork *network;
	int i;

	g_clear_pointer (&priv->split_dns, free_split_dns);

	if (!G_VALUE_HOLDS (value, DBUS_TYPE_NM_SPLIT_DNS_CONFIG))
		return FALSE;

	dbus_g_type_struct_get (value,
	                        0, &networks,
	                        1, &fallback,
	                        G_MAXUINT);

	priv->split_dns = g_slice_new (NMSplitDnsConfig);
	priv->split_dns->networks = g_ptr_array_new ();
	priv->split_dns->fallback = g_ptr_array_new ();

	for (i = 0; i < networks->len; i++) {
		vnetwork = networks->pdata[i];
		network = g_slice_new (NMSplitDnsNetwork);
		network->interface = g_value_dup_string (g_value_array_get_nth (vnetwork, 0));
		network->nameservers = g_value_dup_boxed (g_value_array_get_nth (vnetwork, 0));


	_nm_object_queue_notify (object, NM_DNS_MANAGER_SPLIT_DNS);
	return TRUE;
}

/****************************************************************/

static void
proxy_name_owner_changed (DBusGProxy *proxy,
                          const char *name,
                          const char *old_owner,
                          const char *new_owner,
                          gpointer user_data)
{
	NMDnsManager *manager = NM_DNS_MANAGER (user_data);
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (manager);
	gboolean old_good = (old_owner && strlen (old_owner));
	gboolean new_good = (new_owner && strlen (new_owner));
	gboolean new_running = FALSE;

	if (!name || strcmp (name, NM_DBUS_SERVICE))
		return;

	if (!old_good && new_good)
		new_running = TRUE;
	else if (old_good && !new_good)
		new_running = FALSE;

	if (new_running == priv->manager_running)
		return;

	priv->manager_running = new_running;
	if (!priv->manager_running) {
		_nm_object_suppress_property_updates (NM_OBJECT (manager), TRUE);
		g_clear_pointer (&priv->nameservers, g_ptr_array_unref);
		g_clear_pointer (&priv->domain, g_free);
		g_clear_pointer (&priv->searches, g_ptr_array_unref);
		g_clear_pointer (&priv->nis_domain, g_free);
		g_clear_pointer (&priv->nis_servers, g_ptr_array_unref);
		g_clear_pointer (&priv->wins_servers, g_ptr_array_unref);
		g_clear_pointer (&priv->split_dns, free_split_dns);
	} else {
		_nm_object_suppress_property_updates (NM_OBJECT (manager), FALSE);
		_nm_object_reload_properties_async (NM_OBJECT (manager), NULL, NULL);
	}
}

/****************************************************************/

/**
 * nm_dns_manager_new:
 * @cancellable: a #GCancellable, or %NULL
 * @error: location for a #GError, or %NULL
 *
 * Creates a new #NMDnsManager.
 *
 * Note that this will do blocking D-Bus calls to initialize the
 * client. You can use nm_dns_manager_new_async() if you want to avoid
 * that.
 *
 * Returns: a new #NMDnsManager
 **/
NMDnsManager *
nm_dns_manager_new (GCancellable *cancellable, GError *error)
{
	return g_initable_new (NM_TYPE_DNS_MANAGER, cancellable, error,
	                       NM_OBJECT_DBUS_PATH, NM_DBUS_PATH_DNS_MANAGER,
	                       NULL);
}

static void
dns_manager_inited (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GSimpleAsyncResult *simple = user_data;
	GObject *object;
	GError *error = NULL;

	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source), result, &error))
	if (object)
		g_simple_async_result_set_op_res_gpointer (simple, object, g_object_unref);
	else
		g_simple_async_result_take_error (simple, error);
	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

/**
 * nm_dns_manager_new_async:
 * @cancellable: a #GCancellable, or %NULL
 * @callback: callback to call when the client is created
 * @user_data: data for @callback
 *
 * Creates a new #NMDnsManager and begins asynchronously initializing it.
 * @callback will be called when it is done; use
 * nm_dns_manager_new_finish() to get the result.
 **/
void
nm_dns_manager_new_async (GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (NULL, callback, user_data, nm_dns_manager_new_async);
	g_async_initable_new_async (NM_TYPE_DNS_MANAGER,
	                            G_PRIORITY_DEFAULT,
	                            cancellable, dns_manager_inited, simple,
	                            NM_OBJECT_DBUS_PATH, NM_DBUS_PATH_DNS_MANAGER,
	                            NULL);
}

/**
 * nm_dns_manager_new_finish:
 * @result: a #GAsyncResult
 * @error: location for a #GError, or %NULL
 *
 * Gets the result of an nm_dns_manager_new_async() call.
 *
 * Returns: a new #NMDnsManager, or %NULL on error
 **/
NMDnsManager *
nm_dns_manager_new_finish (GAsyncResult *result, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL, nm_dns_manager_new_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	else
		return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

static void
constructed (GObject *object)
{
	DBusGConnection *connection;
	NMDnsManagerPrivate *priv;

	priv = NM_DNS_MANAGER_GET_PRIVATE (object);
	connection = nm_object_get_connection (NM_OBJECT (object));

	priv->manager_proxy = dbus_g_proxy_new_for_name (connection,
	                                                 NM_DBUS_SERVICE,
	                                                 nm_object_get_path (NM_OBJECT (object)),
	                                                 NM_DBUS_INTERFACE_DNS_MANAGER);

	register_properties (NM_DNS_MANAGER (object));

	priv->bus_proxy = dbus_g_proxy_new_for_name (connection,
	                                             DBUS_SERVICE_DBUS,
	                                             DBUS_PATH_DBUS,
	                                             DBUS_INTERFACE_DBUS);

	dbus_g_proxy_add_signal (priv->bus_proxy, "NameOwnerChanged",
	                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                         G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (priv->bus_proxy,
	                             "NameOwnerChanged",
	                             G_CALLBACK (proxy_name_owner_changed),
	                             object, NULL);

	g_signal_connect (object, "notify::" NM_DNS_MANAGER_WIRELESS_ENABLED,
	                  G_CALLBACK (wireless_enabled_cb), NULL);

	g_signal_connect (object, "notify::" NM_DNS_MANAGER_ACTIVE_CONNECTIONS,
	                  G_CALLBACK (active_connections_changed_cb), NULL);

	g_signal_connect (object, "object-creation-failed",
	                  G_CALLBACK (object_creation_failed_cb), NULL);
}

static gboolean
init_sync (GInitable *initable, GCancellable *cancellable, GError **error)
{
	NMDnsManager *manager = NM_DNS_MANAGER (initable);
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (manager);

	if (!nm_dns_manager_parent_initable_iface->init (initable, cancellable, error))
		return FALSE;

	if (!dbus_g_proxy_call (priv->bus_proxy,
	                        "NameHasOwner", error,
	                        G_TYPE_STRING, NM_DBUS_SERVICE,
	                        G_TYPE_INVALID,
	                        G_TYPE_BOOLEAN, &priv->manager_running,
	                        G_TYPE_INVALID))
		return FALSE;

	if (priv->manager_running && !get_permissions_sync (manager, error))
		return FALSE;

	return TRUE;
}

typedef struct {
	NMDnsManager *manager;
	GSimpleAsyncResult *result;
	gboolean properties_pending;
	gboolean permissions_pending;
} NMDnsManagerInitData;

static void
init_async_complete (NMDnsManagerInitData *init_data)
{
	if (init_data->properties_pending || init_data->permissions_pending)
		return;

	g_simple_async_result_complete (init_data->result);
	g_object_unref (init_data->result);
	g_slice_free (NMDnsManagerInitData, init_data);
}

static void
init_async_got_permissions (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	NMDnsManagerInitData *init_data = user_data;
	GHashTable *permissions;
	GError *error = NULL;

	dbus_g_proxy_end_call (proxy, call, &error,
	                       DBUS_TYPE_G_MAP_OF_STRING, &permissions,
	                       G_TYPE_INVALID);
	update_permissions (init_data->client, error ? NULL : permissions);
	g_clear_error (&error);

	init_data->permissions_pending = FALSE;
	init_async_complete (init_data);
}

static void
init_async_got_properties (GObject *source, GAsyncResult *result, gpointer user_data)
{
	NMDnsManagerInitData *init_data = user_data;
	GError *error = NULL;

	if (!nm_dns_manager_parent_async_initable_iface->init_finish (G_ASYNC_INITABLE (source), result, &error))
		g_simple_async_result_take_error (init_data->result, error);

	init_data->properties_pending = FALSE;
	init_async_complete (init_data);
}

static void
init_async_got_manager_running (DBusGProxy *proxy, DBusGProxyCall *call,
                                gpointer user_data)
{
	NMDnsManagerInitData *init_data = user_data;
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (init_data->client);
	GError *error = NULL;

	if (!dbus_g_proxy_end_call (proxy, call, &error,
	                            G_TYPE_BOOLEAN, &priv->manager_running,
	                            G_TYPE_INVALID)) {
		g_simple_async_result_take_error (init_data->result, error);
		init_async_complete (init_data);
		return;
	}

	if (!priv->manager_running) {
		init_async_complete (init_data);
		return;
	}

	nm_dns_manager_parent_async_initable_iface->init_async (G_ASYNC_INITABLE (init_data->client),
	                                                   G_PRIORITY_DEFAULT, NULL, /* FIXME cancellable */
	                                                   init_async_got_properties, init_data);
	init_data->properties_pending = TRUE;

	dbus_g_proxy_begin_call (priv->client_proxy, "GetPermissions",
	                         init_async_got_permissions, init_data, NULL,
	                         G_TYPE_INVALID);
	init_data->permissions_pending = TRUE;
}

static void
init_async (GAsyncInitable *initable, int io_priority,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data)
{
	NMDnsManagerInitData *init_data;
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (initable);

	init_data = g_slice_new0 (NMDnsManagerInitData);
	init_data->client = NM_DNS_MANAGER (initable);
	init_data->result = g_simple_async_result_new (G_OBJECT (initable), callback,
	                                               user_data, init_async);
	g_simple_async_result_set_op_res_gboolean (init_data->result, TRUE);

	/* Check if NM is running */
	dbus_g_proxy_begin_call (priv->bus_proxy, "NameHasOwner",
	                         init_async_got_manager_running,
	                         init_data, NULL,
	                         G_TYPE_STRING, NM_DBUS_SERVICE,
	                         G_TYPE_INVALID);
}

static gboolean
init_finish (GAsyncInitable *initable, GAsyncResult *result, GError **error)
{
	GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;
	else
		return TRUE;
}

static void
dispose (GObject *object)
{
	NMDnsManager *manager = NM_DNS_MANAGER (object);
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (object);

	if (priv->perm_call) {
		dbus_g_proxy_cancel_call (priv->client_proxy, priv->perm_call);
		priv->perm_call = NULL;
	}

	g_clear_object (&priv->client_proxy);
	g_clear_object (&priv->bus_proxy);

	free_devices (manager, FALSE);
	free_active_connections (manager, FALSE);

	g_slist_foreach (priv->pending_activations, (GFunc) activate_info_free, NULL);
	g_slist_free (priv->pending_activations);
	priv->pending_activations = NULL;

	g_hash_table_destroy (priv->permissions);
	priv->permissions = NULL;

	G_OBJECT_CLASS (nm_dns_manager_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (object);

	g_free (priv->version);

	G_OBJECT_CLASS (nm_dns_manager_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (object);
	gboolean b;

	switch (prop_id) {
	case PROP_NETWORKING_ENABLED:
		b = g_value_get_boolean (value);
		if (priv->networking_enabled != b) {
			nm_dns_manager_networking_set_enabled (NM_DNS_MANAGER (object), b);
			/* Let the property value flip when we get the change signal from NM */
		}
		break;
	case PROP_WIRELESS_ENABLED:
		b = g_value_get_boolean (value);
		if (priv->wireless_enabled != b) {
			nm_dns_manager_wireless_set_enabled (NM_DNS_MANAGER (object), b);
			/* Let the property value flip when we get the change signal from NM */
		}
		break;
	case PROP_WWAN_ENABLED:
		b = g_value_get_boolean (value);
		if (priv->wwan_enabled != b) {
			nm_dns_manager_wwan_set_enabled (NM_DNS_MANAGER (object), b);
			/* Let the property value flip when we get the change signal from NM */
		}
		break;
	case PROP_WIMAX_ENABLED:
		b = g_value_get_boolean (value);
		if (priv->wimax_enabled != b) {
			nm_dns_manager_wimax_set_enabled (NM_DNS_MANAGER (object), b);
			/* Let the property value flip when we get the change signal from NM */
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMDnsManager *self = NM_DNS_MANAGER (object);
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	_nm_object_ensure_inited (NM_OBJECT (object));

	switch (prop_id) {
	case PROP_VERSION:
		g_value_set_string (value, nm_dns_manager_get_version (self));
		break;
	case PROP_STATE:
		g_value_set_uint (value, nm_dns_manager_get_state (self));
		break;
	case PROP_MANAGER_RUNNING:
		g_value_set_boolean (value, priv->manager_running);
		break;
	case PROP_NETWORKING_ENABLED:
		g_value_set_boolean (value, nm_dns_manager_networking_get_enabled (self));
		break;
	case PROP_WIRELESS_ENABLED:
		g_value_set_boolean (value, priv->wireless_enabled);
		break;
	case PROP_WIRELESS_HARDWARE_ENABLED:
		g_value_set_boolean (value, priv->wireless_hw_enabled);
		break;
	case PROP_WWAN_ENABLED:
		g_value_set_boolean (value, priv->wwan_enabled);
		break;
	case PROP_WWAN_HARDWARE_ENABLED:
		g_value_set_boolean (value, priv->wwan_hw_enabled);
		break;
	case PROP_WIMAX_ENABLED:
		g_value_set_boolean (value, priv->wimax_enabled);
		break;
	case PROP_WIMAX_HARDWARE_ENABLED:
		g_value_set_boolean (value, priv->wimax_hw_enabled);
		break;
	case PROP_ACTIVE_CONNECTIONS:
		g_value_set_boxed (value, nm_dns_manager_get_active_connections (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_dns_manager_class_init (NMDnsManagerClass *manager_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

	g_type_class_add_private (manager_class, sizeof (NMDnsManagerPrivate));

	/* virtual methods */
	object_class->constructor = constructor;
	object_class->constructed = constructed;
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* properties */

	/**
	 * NMDnsManager:version:
	 *
	 * The NetworkManager version.
	 **/
	g_object_class_install_property (object_class, PROP_VERSION,
	                                 g_param_spec_string (NM_DNS_MANAGER_VERSION,
	                                                      "Version",
	                                                      "NetworkManager version",
	                                                      NULL,
	                                                      G_PARAM_READABLE));

	/**
	 * NMDnsManager:state:
	 *
	 * The current daemon state.
	 **/
	g_object_class_install_property
		(object_class, PROP_STATE,
		 g_param_spec_uint (NM_DNS_MANAGER_STATE,
		                    "State",
		                    "NetworkManager state",
		                    NM_STATE_UNKNOWN, NM_STATE_CONNECTED_GLOBAL, NM_STATE_UNKNOWN,
		                    G_PARAM_READABLE));

	/**
	 * NMDnsManager::manager-running:
	 *
	 * Whether the daemon is running.
	 **/
	g_object_class_install_property
		(object_class, PROP_MANAGER_RUNNING,
		 g_param_spec_boolean (NM_DNS_MANAGER_MANAGER_RUNNING,
		                       "ManagerRunning",
		                       "Whether NetworkManager is running",
		                       FALSE,
		                       G_PARAM_READABLE));

	/**
	 * NMDnsManager::networking-enabled:
	 *
	 * Whether networking is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_NETWORKING_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_NETWORKING_ENABLED,
		                       "NetworkingEnabled",
		                       "Is networking enabled",
		                       TRUE,
		                       G_PARAM_READWRITE));

	/**
	 * NMDnsManager::wireless-enabled:
	 *
	 * Whether wireless is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WIRELESS_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WIRELESS_ENABLED,
		                       "WirelessEnabled",
		                       "Is wireless enabled",
		                       FALSE,
		                       G_PARAM_READWRITE));

	/**
	 * NMDnsManager::wireless-hardware-enabled:
	 *
	 * Whether the wireless hardware is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WIRELESS_HARDWARE_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WIRELESS_HARDWARE_ENABLED,
		                       "WirelessHardwareEnabled",
		                       "Is wireless hardware enabled",
		                       TRUE,
		                       G_PARAM_READABLE));

	/**
	 * NMDnsManager::wwan-enabled:
	 *
	 * Whether WWAN functionality is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WWAN_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WWAN_ENABLED,
		                       "WwanEnabled",
		                       "Is WWAN enabled",
		                       FALSE,
		                       G_PARAM_READWRITE));

	/**
	 * NMDnsManager::wwan-hardware-enabled:
	 *
	 * Whether the WWAN hardware is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WWAN_HARDWARE_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WWAN_HARDWARE_ENABLED,
		                       "WwanHardwareEnabled",
		                       "Is WWAN hardware enabled",
		                       FALSE,
		                       G_PARAM_READABLE));

	/**
	 * NMDnsManager::wimax-enabled:
	 *
	 * Whether WiMAX functionality is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WIMAX_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WIMAX_ENABLED,
		                       "WimaxEnabled",
		                       "Is WiMAX enabled",
		                       FALSE,
		                       G_PARAM_READWRITE));

	/**
	 * NMDnsManager::wimax-hardware-enabled:
	 *
	 * Whether the WiMAX hardware is enabled.
	 **/
	g_object_class_install_property
		(object_class, PROP_WIMAX_HARDWARE_ENABLED,
		 g_param_spec_boolean (NM_DNS_MANAGER_WIMAX_HARDWARE_ENABLED,
		                       "WimaxHardwareEnabled",
		                       "Is WiMAX hardware enabled",
		                       FALSE,
		                       G_PARAM_READABLE));

	/**
	 * NMDnsManager::active-connections:
	 *
	 * The active connections.
	 * Type: GPtrArray<NMDnsManager.ActiveConnection>
	 **/
	g_object_class_install_property
		(object_class, PROP_ACTIVE_CONNECTIONS,
		 g_param_spec_boxed (NM_DNS_MANAGER_ACTIVE_CONNECTIONS,
		                     "Active connections",
		                     "Active connections",
		                     NM_TYPE_OBJECT_ARRAY,
		                     G_PARAM_READABLE));

	/* signals */

	/**
	 * NMDnsManager::device-added:
	 * @manager: the client that received the signal
	 * @device: (type NMDnsManager.Device): the new device
	 *
	 * Notifies that a #NMDevice is added.
	 **/
	signals[DEVICE_ADDED] =
		g_signal_new ("device-added",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMDnsManagerClass, device_added),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1,
		              G_TYPE_OBJECT);

	/**
	 * NMDnsManager::device-removed:
	 * @manager: the client that received the signal
	 * @device: (type NMDnsManager.Device): the removed device
	 *
	 * Notifies that a #NMDevice is removed.
	 **/
	signals[DEVICE_REMOVED] =
		g_signal_new ("device-removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMDnsManagerClass, device_removed),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1,
		              G_TYPE_OBJECT);

	/**
	 * NMDnsManager::permission-changed:
	 * @manager: the client that received the signal
	 * @permission: a permission from #NMDnsManagerPermission
	 * @result: the permission's result, one of #NMDnsManagerPermissionResult
	 *
	 * Notifies that a permission has changed
	 **/
	signals[PERMISSION_CHANGED] =
		g_signal_new ("permission-changed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              _nm_glib_marshal_VOID__UINT_UINT,
		              G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
nm_dns_manager_initable_iface_init (GInitableIface *iface)
{
	nm_dns_manager_parent_initable_iface = g_type_interface_peek_parent (iface);

	iface->init = init_sync;
}

static void
nm_dns_manager_async_initable_iface_init (GAsyncInitableIface *iface)
{
	nm_dns_manager_parent_async_initable_iface = g_type_interface_peek_parent (iface);

	iface->init_async = init_async;
	iface->init_finish = init_finish;
}
