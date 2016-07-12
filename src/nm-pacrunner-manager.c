/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager
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
 * Atul Anand <atulhjp@gmail.com>
 */

#include "nm-default.h"

#include "nm-utils.h"
#include "nm-platform.h"
#include "nm-pacrunner-manager.h"
#include "nm-proxy-config.h"
#include "nm-ip4-config.h"
#include "nm-ip6-config.h"

G_DEFINE_TYPE (NMPacRunnerManager, nm_pacrunner_manager, G_TYPE_OBJECT)

NM_DEFINE_SINGLETON_INSTANCE (NMPacRunnerManager);

#define NM_PACRUNNER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_PACRUNNER_MANAGER, NMPacRunnerManagerPrivate))

#define PACRUNNER_DBUS_SERVICE "org.pacrunner"
#define PACRUNNER_DBUS_INTERFACE "org.pacrunner.Manager"
#define PACRUNNER_DBUS_PATH "/org/pacrunner/manager"

struct remove_data {
	char *iface;
	char *path;
};

typedef struct {
	gboolean started;
	char *iface;
	GDBusProxy *pacrunner;
	GCancellable *pacrunner_cancellable;
	GVariant *pacrunner_manager_args;
	GPtrArray *domains;
	GList *remove;
} NMPacRunnerManagerPrivate;

/*****************************************************************************/

#define _NMLOG_DOMAIN         LOGD_PROXY
#define _NMLOG_PREFIX_NAME    "pacrunner"
#define _NMLOG(level, ...) \
    G_STMT_START { \
        nm_log ((level), _NMLOG_DOMAIN, \
                "%s[%p]: " _NM_UTILS_MACRO_FIRST(__VA_ARGS__), \
                _NMLOG_PREFIX_NAME, \
                (self) \
                _NM_UTILS_MACRO_REST(__VA_ARGS__)); \
    } G_STMT_END

/*****************************************************************************/

static void
remove_data_destroy (struct remove_data *data)
{
	g_return_if_fail (data != NULL);

	g_free (data->iface);
	g_free (data->path);
	memset (data, 0, sizeof (struct remove_data));
	g_free (data);
}

static void
add_proxy_config (NMPacRunnerManager *self, GVariantBuilder *proxy_data, const NMProxyConfig *proxy_config)
{
	const char *pac = NULL, *filename = NULL;
	char **servers = NULL, **excludes = NULL;
	char *contents = NULL;
	NMProxyConfigMethod method;

	method = nm_proxy_config_get_method (proxy_config);
	switch (method) {
	case NM_PROXY_CONFIG_METHOD_NONE:
		/* Do Nothing */

		break;
	case NM_PROXY_CONFIG_METHOD_AUTO:
		/* Extract Pac Url */
		pac = nm_proxy_config_get_pac_url (proxy_config);
		if (pac)
			g_variant_builder_add (proxy_data, "{sv}",
			                       "URL",
			                       g_variant_new_string (pac));

		/* Extract Pac Script */
		filename = nm_proxy_config_get_pac_script (proxy_config);
		if (filename)
			if (g_file_get_contents (filename, &contents, NULL, NULL))
				g_variant_builder_add (proxy_data, "{sv}",
				                       "Script",
				                       g_variant_new_string (contents));

		break;
	case NM_PROXY_CONFIG_METHOD_MANUAL:
		/* Extract Proxy servers */
		servers = nm_proxy_config_get_proxies (proxy_config);
		if (servers && g_strv_length (servers))
			g_variant_builder_add (proxy_data, "{sv}",
			                       "Servers",
			                       g_variant_new_strv ((const char *const *) servers, -1));

		/* Extract Excludes */
		excludes = nm_proxy_config_get_excludes (proxy_config);
		if (excludes && g_strv_length (excludes))
			g_variant_builder_add (proxy_data, "{sv}",
			                       "Excludes",
			                       g_variant_new_strv ((const char *const *) excludes, -1));

		if (servers)
			g_strfreev (servers);
		if (excludes)
			g_strfreev (excludes);
	}
}

static void
add_ip4_config (NMPacRunnerManager *self, GVariantBuilder *proxy_data, NMIP4Config *ip4)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);
	int i;
	char *cidr = NULL;

	/* Extract Searches */
	for (i = 0; i < nm_ip4_config_get_num_searches (ip4); i++)
		g_ptr_array_add (priv->domains, g_strdup (nm_ip4_config_get_search (ip4, i)));

	/* Extract domains */
	for (i = 0; i < nm_ip4_config_get_num_domains (ip4); i++)
		g_ptr_array_add (priv->domains, g_strdup (nm_ip4_config_get_domain (ip4, i)));

	/* Add Addresses and routes in CIDR form */
	for (i = 0; i < nm_ip4_config_get_num_addresses (ip4); i++) {
		const NMPlatformIP4Address *address = nm_ip4_config_get_address (ip4, i);

		cidr = g_strdup_printf ("%s/%u",
		                        nm_utils_inet4_ntop (address->address, NULL),
		                        address->plen);
		g_ptr_array_add (priv->domains, g_strdup (cidr));
		g_free (cidr);
	}

	for (i = 0; i < nm_ip4_config_get_num_routes (ip4); i++) {
		const NMPlatformIP4Route *routes = nm_ip4_config_get_route (ip4, i);

		cidr = g_strdup_printf ("%s/%u",
		                        nm_utils_inet4_ntop (routes->network, NULL),
		                        routes->plen);
		g_ptr_array_add (priv->domains, g_strdup (cidr));
		g_free (cidr);
	}
}

static void
add_ip6_config (NMPacRunnerManager *self, GVariantBuilder *proxy_data, NMIP6Config *ip6)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);
	int i;
	char *cidr = NULL;

	/* Extract searches */
	for (i = 0; i < nm_ip6_config_get_num_searches (ip6); i++)
		g_ptr_array_add (priv->domains, g_strdup (nm_ip6_config_get_search (ip6, i)));

	/* Extract domains */
	for (i = 0; i < nm_ip6_config_get_num_domains (ip6); i++)
		g_ptr_array_add (priv->domains, g_strdup (nm_ip6_config_get_domain (ip6, i)));

	/* Add Addresses and routes in CIDR form */
	for (i = 0; i < nm_ip6_config_get_num_addresses (ip6); i++) {
		const NMPlatformIP6Address *address = nm_ip6_config_get_address (ip6, i);

		cidr = g_strdup_printf ("%s/%u",
		                        nm_utils_inet6_ntop (&address->address, NULL),
		                        address->plen);
		g_ptr_array_add (priv->domains, g_strdup (cidr));
		g_free (cidr);
	}

	for (i = 0; i < nm_ip6_config_get_num_routes (ip6); i++) {
		const NMPlatformIP6Route *routes = nm_ip6_config_get_route (ip6, i);

		cidr = g_strdup_printf ("%s/%u",
		                        nm_utils_inet6_ntop (&routes->network, NULL),
		                        routes->plen);
		g_ptr_array_add (priv->domains, g_strdup (cidr));
		g_free (cidr);
	}
}

static void
pacrunner_send_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
	NMPacRunnerManager *self = NM_PACRUNNER_MANAGER (user_data);
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);
	gs_free_error GError *error = NULL;
	gs_unref_variant GVariant *variant = NULL;
	const char *path = NULL;
	GList *iter = NULL;
	gboolean found = FALSE;

	variant = g_dbus_proxy_call_finish (priv->pacrunner, res, &error);
	if (!variant) {
		_LOGD ("sending proxy config to pacrunner failed: %s", error->message);
	} else {
		struct remove_data *data;
		g_variant_get (variant, "(&o)", &path);

		/* Replace the old path (if any) of proxy config with the new one returned
		 * from CreateProxyConfiguration() DBus method on PacRunner.
		 */
		for (iter = g_list_first (priv->remove); iter; iter = g_list_next (iter)) {
			struct remove_data *r = iter->data;
			if (g_strcmp0 (priv->iface, r->iface) == 0) {
				g_free (r->path);
				r->path = g_strdup (path);
				found = TRUE;
				break;
			}
		}

		if (!found) {
			data = g_malloc0 (sizeof (struct remove_data));
			data->iface = g_strdup (priv->iface);
			data->path = g_strdup (path);
			priv->remove = g_list_append (priv->remove, data);
			_LOGD ("proxy config sent to pacrunner");
		}
	}
}

static void
send_pacrunner_proxy_data (NMPacRunnerManager *self)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);

	if (!priv->pacrunner_manager_args)
		return;

	if (priv->pacrunner)
		g_dbus_proxy_call (priv->pacrunner,
		                   "CreateProxyConfiguration",
		                   priv->pacrunner_manager_args,
		                   G_DBUS_CALL_FLAGS_NONE,
		                   -1,
		                   NULL,
		                  (GAsyncReadyCallback) pacrunner_send_done,
		                   self);
}

static void
name_owner_changed (GObject *object,
                    GParamSpec *pspec,
                    gpointer user_data)
{
	NMPacRunnerManager *self = NM_PACRUNNER_MANAGER (user_data);
	gs_free char *owner = NULL;

	owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (object));
	if (owner) {
		_LOGD ("PacRunner appeared as %s", owner);
		send_pacrunner_proxy_data (self);
	} else {
		_LOGD ("PacRunner disappeared");
	}
}

static void
pacrunner_proxy_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	NMPacRunnerManager *self = NULL;
	NMPacRunnerManagerPrivate *priv;
	gs_free_error GError *error = NULL;
	gs_free char *owner = NULL;
	GDBusProxy *proxy;

	self = NM_PACRUNNER_MANAGER (user_data);
	priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);

	proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (!proxy && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		/* Mark PacRunner unavailable on DBus */
		priv->started = FALSE;
		_LOGD ("failed to connect to pacrunner via DBus: %s", error->message);
		return;
	}

	priv->pacrunner = proxy;
	nm_clear_g_cancellable (&priv->pacrunner_cancellable);

	g_signal_connect (priv->pacrunner, "notify::g-name-owner",
	                  G_CALLBACK (name_owner_changed), self);
}

static void
start_pacrunner (NMPacRunnerManager *self)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);

	if (priv->started)
		return;

	priv->pacrunner_cancellable = g_cancellable_new ();

	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
	                          G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
	                          NULL,
	                          PACRUNNER_DBUS_SERVICE,
	                          PACRUNNER_DBUS_PATH,
	                          PACRUNNER_DBUS_INTERFACE,
	                          priv->pacrunner_cancellable,
	                         (GAsyncReadyCallback) pacrunner_proxy_cb,
	                          self);
	priv->started = TRUE;
}

/**
 * nm_pacrunner_manager_send():
 * @self: the #NMPacRunnerManager
 * @iface: the iface for the connection or %NULL
 * @proxy_config: Proxy config of the connection
 * @ip4_conifg: IP4 Cofig of the connection
 * @ip6_config: IP6 Config of the connection
 *
 * Returns: %TRUE if configs were sucessfully sent
 * to PacRunner, %FALSE on error
 */
gboolean
nm_pacrunner_manager_send (NMPacRunnerManager *self,
                           const char *iface,
                           NMProxyConfig *proxy_config,
                           NMIP4Config *ip4_config,
                           NMIP6Config *ip6_config)
{
	char **strv = NULL;
	NMProxyConfigMethod method;
	NMPacRunnerManagerPrivate *priv;
	GVariantBuilder proxy_data;

	g_return_val_if_fail (NM_IS_PACRUNNER_MANAGER (self), FALSE);
	priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);

	/* DBus Proxy hasn't been created */
	if (!priv->started) {
		_LOGD ("Can't send config to pacrunner (not available on bus)");
		return FALSE;
	}

	g_free (priv->iface);
	priv->iface = g_strdup (iface);

	g_variant_builder_init (&proxy_data, G_VARIANT_TYPE_VARDICT);

	g_variant_builder_add (&proxy_data, "{sv}",
	                       "Interface",
	                       g_variant_new_string (iface));

	method = nm_proxy_config_get_method (proxy_config);
	switch (method) {
	case NM_PROXY_CONFIG_METHOD_NONE:
		g_variant_builder_add (&proxy_data, "{sv}",
		                       "Method",
		                       g_variant_new_string ("direct"));

	break;
	case NM_PROXY_CONFIG_METHOD_AUTO:
		g_variant_builder_add (&proxy_data, "{sv}",
		                       "Method",
		                       g_variant_new_string ("auto"));

	break;
	case NM_PROXY_CONFIG_METHOD_MANUAL:
		g_variant_builder_add (&proxy_data, "{sv}",
		                       "Method",
		                       g_variant_new_string ("manual"));
	}

	priv->domains = g_ptr_array_new_with_free_func (g_free);

	/* Extract stuff from Configs */
	if (proxy_config)
		add_proxy_config (self, &proxy_data, proxy_config);
	if (ip4_config)
		add_ip4_config (self, &proxy_data, ip4_config);
	if (ip6_config)
		add_ip6_config (self, &proxy_data, ip6_config);

	g_ptr_array_add (priv->domains, NULL);
	strv = (char **) g_ptr_array_free (priv->domains, (priv->domains->len == 1));

	if (strv) {
		g_variant_builder_add (&proxy_data, "{sv}",
		                       "Domains",
		                       g_variant_new_strv ((const char *const *) strv, -1));
		g_strfreev (strv);
	}

	g_clear_pointer (&priv->pacrunner_manager_args, g_variant_unref);
	priv->pacrunner_manager_args = g_variant_ref_sink (g_variant_new ("(a{sv})", &proxy_data));

	send_pacrunner_proxy_data (self);

	return TRUE;
}

static void
pacrunner_remove_done (GObject *source, GAsyncResult *res, gpointer user_data)
{
	NMPacRunnerManager *self = NM_PACRUNNER_MANAGER (user_data);
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);
	gs_free_error GError *error = NULL;
	gs_unref_variant GVariant *ret = NULL;

	ret = g_dbus_proxy_call_finish (priv->pacrunner, res, &error);
	if (!ret || g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		_LOGD ("Couldn't remove proxy config from pacrunner");
	else
		_LOGD ("Sucessfully removed proxy config from pacrunner");
}

/**
 * nm_pacrunner_manager_remove():
 * @self: the #NMPacRunnerManager
 * @iface: the iface for the connection to be removed
 * from PacRunner
 */
void
nm_pacrunner_manager_remove (NMPacRunnerManager *self, const char *iface)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);
	GList *list;

	for (list = g_list_first(priv->remove); list; list = g_list_next(list)) {
		struct remove_data *data = list->data;
		if (g_strcmp0 (data->iface, iface) == 0) {
			if ((priv->pacrunner) && (data->path))
				g_dbus_proxy_call (priv->pacrunner,
				                   "DestroyProxyConfiguration",
				                   g_variant_new ("(o)", data->path),
				                   G_DBUS_CALL_FLAGS_NONE,
				                   -1,
				                   NULL,
				                  (GAsyncReadyCallback) pacrunner_remove_done,
				                   self);
			break;
		}
	}
}

NM_DEFINE_SINGLETON_GETTER (NMPacRunnerManager, nm_pacrunner_manager_get, NM_TYPE_PACRUNNER_MANAGER);

static void
nm_pacrunner_manager_init (NMPacRunnerManager *self)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (self);

	priv->started = FALSE;

	/* Create DBus Proxy */
	start_pacrunner (self);
}

static void
dispose (GObject *object)
{
	NMPacRunnerManagerPrivate *priv = NM_PACRUNNER_MANAGER_GET_PRIVATE (object);

	g_free (priv->iface);

	nm_clear_g_cancellable (&priv->pacrunner_cancellable);

	g_clear_object (&priv->pacrunner);

	g_clear_pointer (&priv->pacrunner_manager_args, g_variant_unref);

	g_list_free_full (priv->remove, (GDestroyNotify) remove_data_destroy);

	G_OBJECT_CLASS (nm_pacrunner_manager_parent_class)->dispose (object);
}

static void
nm_pacrunner_manager_class_init (NMPacRunnerManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMPacRunnerManagerPrivate));

	object_class->dispose = dispose;
}
