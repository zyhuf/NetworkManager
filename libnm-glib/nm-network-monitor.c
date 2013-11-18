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

#include "config.h"

#undef GLIB_VERSION_MAX_ALLOWED
#define GLIB_VERSION_MAX_ALLOWED GLIB_VERSION_2_42

#include "nm-network-monitor.h"
#include "nm-client.h"
#include "NetworkManager.h"

static void nm_network_monitor_iface_init (GNetworkMonitorInterface *iface);
static void nm_network_monitor_initable_iface_init (GInitableIface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (NMNetworkMonitor, nm_network_monitor, G_TYPE_NETWORK_MONITOR_BASE, 0,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (G_TYPE_NETWORK_MONITOR,
                                                               nm_network_monitor_iface_init)
                                G_IMPLEMENT_INTERFACE_DYNAMIC (G_TYPE_INITABLE,
                                                               nm_network_monitor_initable_iface_init)
                                )

typedef struct
{
	GMutex mutex;
	GCond cond;
	GMainContext *context;

	NMClient *client;
	GNetworkConnectivity connectivity;

} NMNetworkMonitorPrivate;

enum {
	PROP_0,
	PROP_CONNECTIVITY
};

#define NM_NETWORK_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_NETWORK_MONITOR, NMNetworkMonitorPrivate))

static void
nm_network_monitor_init (NMNetworkMonitor *monitor)
{
}

/* We have to make libnm-glib calls (and we receive libnm-glib signals) in the
 * default GMainContext. But we have to update the monitor's state and emit monitor
 * signals in its own priv->context (the thread-default context from when it was
 * created). So we need to be careful about what happens where...
 */

typedef struct {
	NMNetworkMonitor *monitor;
	NMConnectivityState connectivity;
} UpdateConnectivityData;

/* Called from the monitor's GMainContext */
static gboolean
update_connectivity (gpointer user_data)
{
	UpdateConnectivityData *data = user_data;
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (data->monitor);

	switch (data->connectivity) {
	case NM_CONNECTIVITY_NONE:
		priv->connectivity = G_NETWORK_CONNECTIVITY_LOCAL;
		break;
	case NM_CONNECTIVITY_LIMITED:
		priv->connectivity = G_NETWORK_CONNECTIVITY_LIMITED;
		break;
	case NM_CONNECTIVITY_PORTAL:
		priv->connectivity = G_NETWORK_CONNECTIVITY_PORTAL;
		break;
	case NM_CONNECTIVITY_FULL:
	default:
		priv->connectivity = G_NETWORK_CONNECTIVITY_FULL;
		break;
	}

	g_object_notify (G_OBJECT (data->monitor), "connectivity");

	g_object_unref (data->monitor);
	g_slice_free (UpdateConnectivityData, data);

	return FALSE;
}

/* Called from the default GMainContext */
static void
client_connectivity_changed (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    monitor)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (monitor);
	UpdateConnectivityData *data;

	data = g_slice_new (UpdateConnectivityData);
	data->monitor = g_object_ref (monitor);
	data->connectivity = nm_client_get_connectivity (NM_CLIENT (object));

	g_main_context_invoke (priv->context, update_connectivity, data);
}

/* Called from the monitor's GMainContext */
static void
rebuild_ip_config (NMNetworkMonitor *monitor,
                   GPtrArray        *networks)
{
	GNetworkMonitorBase *base = G_NETWORK_MONITOR_BASE (monitor);
	int i;

	g_object_freeze_notify (G_OBJECT (monitor));
	g_network_monitor_base_set_networks (base, NULL, 0);

	for (i = 0; i < networks->len; i++) {
		GInetAddressMask *network = networks->pdata[i];

		g_network_monitor_base_add_network (base, network);
	}

	g_object_thaw_notify (G_OBJECT (monitor));
}

typedef struct {
	NMNetworkMonitor *monitor;
	GPtrArray *networks;
} UpdateIPConfigData;

/* Called from the monitor's GMainContext */
static gboolean
update_ip_config (gpointer user_data)
{
	UpdateIPConfigData *data = user_data;

	rebuild_ip_config (data->monitor, data->networks);

	g_object_unref (data->monitor);
	g_ptr_array_unref (data->networks);
	g_slice_free (UpdateIPConfigData, data);

	return FALSE;
}

static void
do_add_network (GPtrArray     *networks,
                int            family,
                gconstpointer  address,
                guint          length)
{
	GInetAddress *addr;
	GInetAddressMask *mask;

	addr = g_inet_address_new_from_bytes (address, family);
	mask = g_inet_address_mask_new (addr, length, NULL);
	g_object_unref (addr);
	g_ptr_array_add (networks, mask);
}

/* Called from the default GMainContext */
static GPtrArray *
regenerate_network_list (NMNetworkMonitor *monitor)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (monitor);
	const GPtrArray *devices;
	gboolean have_ip4_default, have_ip6_default;
	GPtrArray *networks;
	int i;

	networks = g_ptr_array_new_with_free_func (g_object_unref);

	devices = nm_client_get_devices (priv->client);
	if (!devices)
		return networks;

	have_ip4_default = have_ip6_default = FALSE;
	for (i = 0; i < devices->len && (!have_ip4_default || !have_ip6_default); i++) {
		NMDevice *device = devices->pdata[i];
		NMIP4Config *ip4;
		NMIP6Config *ip6;

		if (!have_ip4_default) {
			ip4 = nm_device_get_ip4_config (device);
			if (ip4 && nm_ip4_config_get_gateway (ip4)) {
				in_addr_t in4addr_any = INADDR_ANY;

				do_add_network (networks, AF_INET, &in4addr_any, 0);
				have_ip4_default = TRUE;
			}
		}

		if (!have_ip6_default) {
			ip6 = nm_device_get_ip6_config (device);
			if (ip6 && nm_ip6_config_get_gateway (ip6)) {
				do_add_network (networks, AF_INET6, &in6addr_any, 0);
				have_ip6_default = TRUE;
			}
		}
	}

	/* If we have default routes for both IPv4 and IPv6, then we don't
	 * need to worry about more-specific routes.
	 */
	if (have_ip4_default && have_ip6_default)
		return networks;

	for (i = 0; i < devices->len; i++) {
		NMDevice *device = devices->pdata[i];
		NMIP4Config *ip4;
		NMIP6Config *ip6;
		GSList *routes, *iter;

		ip4 = nm_device_get_ip4_config (device);
		if (ip4) {
			routes = nm_ip4_config_get_routes (ip4);
			for (iter = routes; iter; iter = iter->next) {
				NMIP4Route *route = iter->data;
				guint32 dest = nm_ip4_route_get_dest (route);
				guint32 prefix = nm_ip4_route_get_prefix (route);

				do_add_network (networks, AF_INET, &dest, prefix);
			}
		}

		ip6 = nm_device_get_ip6_config (device);
		if (ip6) {
			routes = nm_ip4_config_get_routes (ip4);
			for (iter = routes; iter; iter = iter->next) {
				NMIP6Route *route = iter->data;
				const struct in6_addr *dest = nm_ip6_route_get_dest (route);
				guint32 prefix = nm_ip6_route_get_prefix (route);

				do_add_network (networks, AF_INET6, dest, prefix);
			}
		}
	}

	return networks;
}

/* Called from the default GMainContext */
static gboolean
idle_update_ip_config (gpointer monitor)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (monitor);
	GPtrArray *networks;
	UpdateIPConfigData *data;

	g_clear_pointer (&priv->idle_update, g_source_unref);

	data = g_slice_new (UpdateIPConfigData);
	data->monitor = g_object_ref (monitor);
	data->networks = regenerate_network_list (monitor);

	g_main_context_invoke (priv->context, update_ip_config, data);

	return FALSE;
}

/* Called from the default GMainContext */
static void
setup_idle_update_ip_config (NMNetworkMonitor *monitor)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (monitor);

	if (!priv->idle_update) {
		priv->idle_update = g_timeout_source_new (100);
		g_source_set_callback (priv->idle_update,
		                       (GSourceFunc) idle_update_ip_config, monitor, NULL);
		g_source_attach (priv->idle_update, NULL);
	} else
		g_source_set_ready_time (priv->idle_update, g_get_monotonic_time () + 100);
}

/* Called from the default GMainContext */
static void
device_ip_config_changed (GObject    *object,
                          GParamSpec *pspec,
                          gpointer    monitor)
{
	setup_idle_update_ip_config (monitor);
}

/* Called from the default GMainContext */
static void
client_device_added (NMClient *client,
                     NMDevice *device,
                     gpointer  monitor)
{
	g_signal_connect (device, "notify::" NM_DEVICE_IP4_CONFIG,
	                  G_CALLBACK (device_ip_config_changed), monitor);
	g_signal_connect (device, "notify::" NM_DEVICE_IP6_CONFIG,
	                  G_CALLBACK (device_ip_config_changed), monitor);
	setup_idle_update_ip_config (monitor);
}

/* Called from the default GMainContext */
static void
client_device_removed (NMClient *client,
                       NMDevice *device,
                       gpointer  monitor)
{
	g_signal_handlers_disconnect_by_func (client, G_CALLBACK (device_ip_config_changed), monitor);
	setup_idle_update_ip_config (monitor);
}

typedef struct {
	NMNetworkMonitor *monitor;
	GCancellable *cancellable;
	GError *error;
} NMNetworkMonitorInitData;

/* Called from the default GMainContext */
static gboolean
main_thread_initable_init (gpointer user_data)
{
	NMNetworkMonitorInitData *init_data = user_data;
	NMNetworkMonitor *monitor = init_data->monitor;
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (monitor);
	const GPtrArray *devices;
	int i;

	priv->client = nm_client_new ();
	if (!priv->client || !nm_client_get_manager_running (priv->client)) {
		g_set_error_literal (&init->data->error,
		                     NM_CLIENT_ERROR, NM_CLIENT_ERROR_MANAGER_NOT_RUNNING,
		                     /* Not seen by the user, so doesn't need l10n. */
		                     "NetworkManager is not running");
		return FALSE;
	}

	if (g_cancellable_set_error_if_cancelled (init_data->cancellable, &init_data->error))
		return FALSE;

	g_signal_connect (priv->client, "notify::" NM_CLIENT_CONNECTIVITY,
	                  G_CALLBACK (client_connectivity_changed), monitor);

	g_signal_connect (priv->client, "device-added",
	                  G_CALLBACK (client_device_added), monitor);
	g_signal_connect (priv->client, "device-removed",
	                  G_CALLBACK (client_device_removed), monitor);

	devices = nm_client_get_devices (priv->client);
	for (i = 0; devices && i < devices->len; i++)
		client_device_added (NULL, devices->pdata[i], monitor);

	return FALSE;
}

static gboolean
nm_network_monitor_initable_init (GInitable     *initable,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (initable);
	NMNetworkMonitorInitData init_data;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	priv->context = g_main_context_get_thread_default ();

	init_data.monitor = NM_NETWORK_MONITOR (initable);
	init_data.cancellable = cancellable;
	init_data.error = NULL;

	if (priv->context) {
		g_main_context_ref (priv->context);

		g_mutex_lock (&priv->mutex);
		g_idle_add (main_thread_initable_init, &init_data);
		g_cond_wait (&priv->cond, &priv->mutex);
		g_mutex_unlock (&priv->mutex);
	} else
		main_thread_initable_init (&init_data);

	if (init_data.error) {
		g_propagate_error (error, init_data.error);
		return FALSE;
	} else
		return TRUE;
}

static void
nm_network_monitor_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_CONNECTIVITY:
		g_value_set_enum (value, priv->connectivity);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
nm_network_monitor_finalize (GObject *object)
{
	NMNetworkMonitorPrivate *priv = NM_NETWORK_MONITOR_GET_PRIVATE (object);

	g_clear_object (&priv->client);
	g_clear_pointer (&priv->context, g_main_context_unref);

	G_OBJECT_CLASS (nm_network_monitor_parent_class)->finalize (object);
}

static void
nm_network_monitor_class_init (NMNetworkMonitorClass *nm_class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (nm_class);

	gobject_class->get_property = nm_network_monitor_get_property;
	gobject_class->finalize  = nm_network_monitor_finalize;

	g_object_class_override_property (gobject_class, PROP_CONNECTIVITY, "connectivity");
}

static void
nm_network_monitor_class_finalize (NMNetworkMonitorClass *nm_class)
{
}

static void
nm_network_monitor_iface_init (GNetworkMonitorInterface *monitor_iface)
{
}

static void
nm_network_monitor_initable_iface_init (GInitableIface *iface)
{
	iface->init = nm_network_monitor_initable_init;
}

void
nm_network_monitor_register (GIOModule *module)
{
	nm_network_monitor_register_type (G_TYPE_MODULE (module));
	g_io_extension_point_implement (G_NETWORK_MONITOR_EXTENSION_POINT_NAME,
	                                NM_TYPE_NETWORK_MONITOR,
	                                "NetworkManager",
	                                50);
}

void
g_io_module_load (GIOModule *module)
{
	nm_network_monitor_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}

gchar **
g_io_module_query (void)
{
	gchar *eps[] = {
		G_NETWORK_MONITOR_EXTENSION_POINT_NAME,
		NULL
	};
	return g_strdupv (eps);
}
