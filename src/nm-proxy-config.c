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

#include <stdlib.h>

#include "nm-proxy-config.h"
#include "nm-core-internal.h"

#define NM_PROXY_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_PROXY_CONFIG, NMProxyConfigPrivate))

G_DEFINE_TYPE (NMProxyConfig, nm_proxy_config, G_TYPE_OBJECT)

typedef struct {
	NMProxyConfigMethod method;
	GPtrArray *proxies;
	GPtrArray *excludes;
	char *pac_url;
	char *pac_script;
} NMProxyConfigPrivate;

NMProxyConfig *
nm_proxy_config_new (void)
{
	return NM_PROXY_CONFIG (g_object_new (NM_TYPE_PROXY_CONFIG, NULL));
}

void
nm_proxy_config_set_method (NMProxyConfig *config, NMProxyConfigMethod method)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	priv->method = method;
}

NMProxyConfigMethod
nm_proxy_config_get_method (const NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	return priv->method;
}

void
nm_proxy_config_merge_setting (NMProxyConfig *config, NMSettingProxy *setting)
{
	const char *tmp = NULL;
	char **strv = NULL;
	guint32 port = 0;
	NMProxyConfigPrivate *priv;
	NMSettingProxyMethod method;

	if (!setting)
		return;

	g_return_if_fail (NM_IS_SETTING_PROXY (setting));

	priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	g_ptr_array_free (priv->proxies, TRUE);
	g_ptr_array_free (priv->excludes, TRUE);
	g_free (priv->pac_url);
	g_free (priv->pac_script);

	priv->proxies = NULL;
	priv->excludes = NULL;
	priv->pac_url = NULL;
	priv->pac_script = NULL;

	method = nm_setting_proxy_get_method (setting);
	switch (method) {
	case NM_SETTING_PROXY_METHOD_NONE:
		priv->method = NM_PROXY_CONFIG_METHOD_NONE;

		break;
	case NM_SETTING_PROXY_METHOD_AUTO:
		priv->method = NM_PROXY_CONFIG_METHOD_AUTO;

		tmp = nm_setting_proxy_get_pac_url (setting);
		priv->pac_url = g_strdup (tmp);

		tmp = nm_setting_proxy_get_pac_script (setting);
		priv->pac_script = g_strdup (tmp);

		break;
	case NM_SETTING_PROXY_METHOD_MANUAL:
		priv->method = NM_PROXY_CONFIG_METHOD_MANUAL;

		strv = nm_setting_proxy_get_no_proxy_for (setting);
		priv->excludes = _nm_utils_strv_to_ptrarray (strv);
		g_free (strv);

		priv->proxies = g_ptr_array_new_with_free_func (g_free);

		tmp = nm_setting_proxy_get_http_proxy (setting);
		port = nm_setting_proxy_get_http_port (setting);

		if (nm_setting_proxy_get_http_default (setting)) {
			if (tmp && port)
				g_ptr_array_add (priv->proxies, g_strdup_printf ("%s:%u/", tmp, port));
			break;
		}

		if (tmp && port)
			g_ptr_array_add (priv->proxies, g_strdup_printf ("http://%s:%u/", tmp, port));

		/* If HTTP Proxy has been selected for all Protocols
		 * set up a generic proxy in PacRunner.
		 */
		port = 0;
		tmp = nm_setting_proxy_get_ssl_proxy (setting);
		port = nm_setting_proxy_get_ssl_port (setting);
		if (tmp && port)
			g_ptr_array_add (priv->proxies, g_strdup_printf ("https://%s:%u/", tmp, port));

		port = 0;
		tmp = nm_setting_proxy_get_ftp_proxy (setting);
		port = nm_setting_proxy_get_ftp_port (setting);
		if (tmp && port)
			g_ptr_array_add (priv->proxies, g_strdup_printf ("ftp://%s:%u/", tmp, port));

		port = 0;
		tmp = nm_setting_proxy_get_socks_proxy (setting);
		port = nm_setting_proxy_get_socks_port (setting);
		if (tmp && port)
			g_ptr_array_add (priv->proxies, g_strdup_printf (nm_setting_proxy_get_socks_version_5 (setting) ?
			                                                 "socks5://%s:%u/" : "socks4://%s:%u/", tmp, port));
	}
}

NMSetting *
nm_proxy_config_create_setting (const NMProxyConfig *config)
{
	NMSettingProxy *s_proxy;
	NMProxyConfigPrivate *priv;
	const char *tmp = NULL;
	char **excludes = NULL;
	char *str = NULL;
	guint32 i, port = 0;
	NMProxyConfigMethod method;

	g_return_val_if_fail (config != NULL, NULL);
	s_proxy = NM_SETTING_PROXY (nm_setting_proxy_new ());

	priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	method = nm_proxy_config_get_method (config);
	switch (method) {
	case NM_PROXY_CONFIG_METHOD_NONE:
		g_object_set (s_proxy,
		              NM_SETTING_PROXY_METHOD, NM_SETTING_PROXY_METHOD_NONE,
		              NULL);

		break;
	case NM_PROXY_CONFIG_METHOD_AUTO:
		g_object_set (s_proxy,
		              NM_SETTING_PROXY_METHOD, NM_SETTING_PROXY_METHOD_AUTO,
		              NM_SETTING_PROXY_PAC_URL, nm_proxy_config_get_pac_url (config),
		              NM_SETTING_PROXY_PAC_SCRIPT, nm_proxy_config_get_pac_script (config),
		              NULL);

		break;
	case NM_PROXY_CONFIG_METHOD_MANUAL:
		g_object_set (s_proxy,
		              NM_SETTING_PROXY_METHOD, NM_SETTING_PROXY_METHOD_MANUAL,
		              NULL);

		for (i = 0; priv->proxies->len; i++) {
			tmp = g_ptr_array_index (priv->proxies, i);

			if (strstr (tmp, "http://")) {
				tmp = tmp + 7;
				str = g_strndup (tmp, strchr (tmp, ':') - tmp);
				g_object_set (s_proxy, NM_SETTING_PROXY_HTTP_PROXY, str, NULL);
				g_free (str);

				tmp = strchr (tmp, ':') + 1;
				str = g_strndup (tmp, strchr (tmp, '/') - tmp);
				port = (guint32) atoi (str);
				if (port > 0)
					g_object_set (s_proxy, NM_SETTING_PROXY_HTTP_PORT, port, NULL);
				g_free (str);

			} else if (strstr (tmp, "https://")) {
				tmp = tmp + 8;
				str = g_strndup (tmp, strchr (tmp, ':') - tmp);
				g_object_set (s_proxy, NM_SETTING_PROXY_SSL_PROXY, str, NULL);
				g_free (str);


				tmp = strchr (tmp, ':') + 1;
				str = g_strndup (tmp, strchr (tmp, '/') - tmp);
				port = (guint32) atoi (str);
				if (port > 0)
					g_object_set (s_proxy, NM_SETTING_PROXY_SSL_PORT, port, NULL);
				g_free (str);

			} else if (strstr (tmp, "ftp://")) {
				tmp = tmp + 6;
				str = g_strndup (tmp, strchr (tmp, ':') - tmp);
				g_object_set (s_proxy, NM_SETTING_PROXY_FTP_PROXY, str, NULL);
				g_free (str);

				tmp = strchr (tmp, ':') + 1;
				str = g_strndup (tmp, strchr (tmp, '/') - tmp);
				port = (guint32) atoi (str);
				if (port > 0)
					g_object_set (s_proxy, NM_SETTING_PROXY_FTP_PORT, port, NULL);
				g_free (str);

			} else if (strstr (tmp, "socks4://") || strstr (tmp, "socks5://")) {
				if (strstr (tmp, "socks5://"))
					g_object_set (s_proxy, NM_SETTING_PROXY_SOCKS_VERSION_5, TRUE, NULL);
				else
					g_object_set (s_proxy, NM_SETTING_PROXY_SOCKS_VERSION_5, FALSE, NULL);

				tmp = tmp + 9;
				str = g_strndup (tmp, strchr (tmp, ':') - tmp);
				g_object_set (s_proxy, NM_SETTING_PROXY_SOCKS_PROXY, str, NULL);
				g_free (str);

				tmp = strchr (tmp, ':') + 1;
				str = g_strndup (tmp, strchr (tmp, '/') - tmp);
				port = (guint32) atoi (str);
				if (port > 0)
					g_object_set (s_proxy, NM_SETTING_PROXY_SOCKS_PORT, port, NULL);
				g_free (str);
			}
		}

		if (priv->excludes->len) {
			excludes = _nm_utils_ptrarray_to_strv (priv->excludes);
			g_object_set (s_proxy,
			              NM_SETTING_PROXY_NO_PROXY_FOR, excludes,
			              NULL);
			g_free (excludes);
		}
	}

	return NM_SETTING (s_proxy);
}

char **
nm_proxy_config_get_proxies (const NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	return _nm_utils_ptrarray_to_strv (priv->proxies);
}

char **
nm_proxy_config_get_excludes (const NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	return _nm_utils_ptrarray_to_strv (priv->excludes);
}

void
nm_proxy_config_set_pac_url (NMProxyConfig *config, const char *url)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	g_free (priv->pac_url);
	priv->pac_url = g_strdup (url);
}

const char *
nm_proxy_config_get_pac_url (const NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	return priv->pac_url;
}

void
nm_proxy_config_set_pac_script (NMProxyConfig *config, const char *script)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	g_free (priv->pac_script);
	priv->pac_script = g_strdup (script);
}

const char *
nm_proxy_config_get_pac_script (const NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	return priv->pac_script;
}

static void
nm_proxy_config_init (NMProxyConfig *config)
{
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (config);

	priv->method = NM_PROXY_CONFIG_METHOD_NONE;
	priv->proxies = g_ptr_array_new_with_free_func (g_free);
	priv->excludes = g_ptr_array_new_with_free_func (g_free);
}

static void
finalize (GObject *object)
{
	NMProxyConfig *self = NM_PROXY_CONFIG (object);
	NMProxyConfigPrivate *priv = NM_PROXY_CONFIG_GET_PRIVATE (self);

	if (priv->proxies)
		g_ptr_array_free (priv->proxies, TRUE);
	if (priv->excludes)
		g_ptr_array_free (priv->excludes, TRUE);
	g_free (priv->pac_url);
	g_free (priv->pac_script);

	G_OBJECT_CLASS (nm_proxy_config_parent_class)->finalize (object);
}

static void
nm_proxy_config_class_init (NMProxyConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMProxyConfigPrivate));

	object_class->finalize = finalize;
}
