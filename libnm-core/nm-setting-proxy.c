/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
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
 * Atul Anand <atulhjp@gmail.com>
 */

#include "nm-default.h"

#include "nm-setting-proxy.h"
#include "nm-utils.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-proxy
 * @short_description: Describes Proxy Url, Script and other related properties
 *
 * The #NMSettingProxy object is a #NMSetting subclass that describes properties
 * related to Proxy settings like Pac Url, Pac Script etc.
 *
 * NetworkManager support 3 values for the #NMSettingProxy:method property for
 * proxy. If "auto" is specified then WPAD takes places and the appropriate details
 * are pushed into PacRunner or user can override this URL with a new PAC url or a
 * PAC Script. If "manual" is selected then users can specify different proxy servers
 * for different protocols. If "none" is selected then no proxy configuration is given
 * to PacRunner to fulfill client queries.
 **/

G_DEFINE_TYPE_WITH_CODE (NMSettingProxy, nm_setting_proxy, NM_TYPE_SETTING,
                         _nm_register_setting (PROXY, 4))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_PROXY)

#define NM_SETTING_PROXY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_PROXY, NMSettingProxyPrivate))

typedef struct {
	NMSettingProxyMethod method;
	char *http_proxy;
	guint32 http_port;
	gboolean http_default;
	char *ssl_proxy;
	guint32 ssl_port;
	char *ftp_proxy;
	guint32 ftp_port;
	char *socks_proxy;
	guint32 socks_port;
	gboolean socks_version_5;
	GPtrArray *no_proxy_for;
	char *pac_url;
	char *pac_script;
} NMSettingProxyPrivate;

enum {
	PROP_0,
	PROP_METHOD,
	PROP_HTTP_PROXY,
	PROP_HTTP_PORT,
	PROP_HTTP_DEFAULT,
	PROP_SSL_PROXY,
	PROP_SSL_PORT,
	PROP_FTP_PROXY,
	PROP_FTP_PORT,
	PROP_SOCKS_PROXY,
	PROP_SOCKS_PORT,
	PROP_SOCKS_VERSION_5,
	PROP_NO_PROXY_FOR,
	PROP_PAC_URL,
	PROP_PAC_SCRIPT,

	LAST_PROP
};

/**
 * nm_setting_proxy_new:
 *
 * Creates a new #NMSettingProxy object.
 *
 * Returns: the new empty #NMSettingProxy object
 **/
NMSetting *
nm_setting_proxy_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_PROXY, NULL);
}

/**
 * nm_setting_proxy_get_method:
 * @setting: the #NMSettingProxy
 *
 * Returns the proxy configuration method. By default the value is "NONE".
 * "NONE" should be selected for a connection intended for direct network
 * access.
 *
 * Returns: the proxy configuration method
 **/
NMSettingProxyMethod
nm_setting_proxy_get_method (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NM_SETTING_PROXY_METHOD_NONE);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->method;
}

/**
 * nm_setting_proxy_get_http_proxy:
 * @setting: the #NMSettingProxy
 *
 * Returns: the HTTP proxy
 **/
const char *
nm_setting_proxy_get_http_proxy (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->http_proxy;
}

/**
 * nm_setting_proxy_get_http_port:
 * @setting: the #NMSettingProxy
 *
 * Returns: the HTTP port number
 **/
guint32
nm_setting_proxy_get_http_port (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), 0);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->http_port;
}

/**
 * nm_setting_proxy_get_http_default:
 * @setting: the #NMSettingProxy
 *
 * Returns: TRUE if HTTP Proxy is default for all
 * protocols. FALSE if not.
 **/
gboolean
nm_setting_proxy_get_http_default (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), FALSE);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->http_default;
}

/**
 * nm_setting_proxy_get_ssl_proxy:
 * @setting: the #NMSettingProxy
 *
 * Returns: the SSL proxy
 **/
const char *
nm_setting_proxy_get_ssl_proxy (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->ssl_proxy;
}

/**
 * nm_setting_proxy_get_ssl_port:
 * @setting: the #NMSettingProxy
 *
 * Returns: the SSL port number
 **/
guint32
nm_setting_proxy_get_ssl_port (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), 0);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->ssl_port;
}

/**
 * nm_setting_proxy_get_ftp_proxy:
 * @setting: the #NMSettingProxy
 *
 * Returns: the FTP proxy
 **/
const char *
nm_setting_proxy_get_ftp_proxy (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->ftp_proxy;
}

/**
 * nm_setting_proxy_get_ftp_port:
 * @setting: the #NMSettingProxy
 *
 * Returns: the FTP port number
 **/
guint32
nm_setting_proxy_get_ftp_port (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), 0);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->ftp_port;
}

/**
 * nm_setting_proxy_get_socks_proxy:
 * @setting: the #NMSettingProxy
 *
 * Returns: the SOCKS proxy
 **/
const char *
nm_setting_proxy_get_socks_proxy (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->socks_proxy;
}

/**
 * nm_setting_proxy_get_socks_port:
 * @setting: the #NMSettingProxy
 *
 * Returns: the SOCKS port number
 **/
guint32
nm_setting_proxy_get_socks_port (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), 0);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->socks_port;
}

/**
 * nm_setting_proxy_get_socks_version_5:
 * @setting: the #NMSettingProxy
 *
 * Returns: TRUE if SOCKS version is 5.
 * FALSE if SOCKS version is 4.
**/
gboolean
nm_setting_proxy_get_socks_version_5 (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), FALSE);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->socks_version_5;
}

/**
 * nm_setting_proxy_get_no_proxy_for:
 * @setting: the #NMSettingProxy
 *
 * Returns: the hosts to be excluded from proxy
 **/
char **
nm_setting_proxy_get_no_proxy_for (NMSettingProxy *setting)
{
	NMSettingProxyPrivate *priv;
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	priv = NM_SETTING_PROXY_GET_PRIVATE (setting);

	return _nm_utils_ptrarray_to_strv (priv->no_proxy_for);
}

/**
 * nm_setting_proxy_get_pac_url:
 * @setting: the #NMSettingProxy
 *
 * Returns: the PAC url for obtaining PAC file
 **/
const char *
nm_setting_proxy_get_pac_url (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->pac_url;
}

/**
 * nm_setting_proxy_get_pac_script:
 * @setting: the #NMSettingProxy
 *
 * Returns: the PAC Script
 **/
const char *
nm_setting_proxy_get_pac_script (NMSettingProxy *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_PROXY (setting), NULL);

	return NM_SETTING_PROXY_GET_PRIVATE (setting)->pac_script;
}

static void
nm_setting_proxy_init (NMSettingProxy *setting)
{
	NMSettingProxyPrivate *priv = NM_SETTING_PROXY_GET_PRIVATE (setting);

	priv->method = NM_SETTING_PROXY_METHOD_NONE;
	priv->no_proxy_for = g_ptr_array_new_with_free_func (g_free);
}

static void
finalize (GObject *object)
{
	NMSettingProxy *self = NM_SETTING_PROXY (object);
	NMSettingProxyPrivate *priv = NM_SETTING_PROXY_GET_PRIVATE (self);

	g_free (priv->http_proxy);
	g_free (priv->ssl_proxy);
	g_free (priv->ftp_proxy);
	g_free (priv->socks_proxy);
	g_free (priv->pac_url);
	g_free (priv->pac_script);

	g_ptr_array_unref (priv->no_proxy_for);

	G_OBJECT_CLASS (nm_setting_proxy_parent_class)->finalize (object);
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMSettingProxy *setting = NM_SETTING_PROXY (object);

	switch (prop_id) {
	case PROP_METHOD:
		g_value_set_int (value, nm_setting_proxy_get_method (setting));
		break;
	case PROP_HTTP_PROXY:
		g_value_set_string (value, nm_setting_proxy_get_http_proxy (setting));
		break;
	case PROP_HTTP_PORT:
		g_value_set_uint (value, nm_setting_proxy_get_http_port (setting));
		break;
	case PROP_HTTP_DEFAULT:
		g_value_set_boolean (value, nm_setting_proxy_get_http_default (setting));
		break;
	case PROP_SSL_PROXY:
		g_value_set_string (value, nm_setting_proxy_get_ssl_proxy (setting));
		break;
	case PROP_SSL_PORT:
		g_value_set_uint (value, nm_setting_proxy_get_ssl_port (setting));
		break;
	case PROP_FTP_PROXY:
		g_value_set_string (value, nm_setting_proxy_get_ftp_proxy (setting));
		break;
	case PROP_FTP_PORT:
		g_value_set_uint (value, nm_setting_proxy_get_ftp_port (setting));
		break;
	case PROP_SOCKS_PROXY:
		g_value_set_string (value, nm_setting_proxy_get_socks_proxy (setting));
		break;
	case PROP_SOCKS_PORT:
		g_value_set_uint (value, nm_setting_proxy_get_socks_port (setting));
		break;
	case PROP_SOCKS_VERSION_5:
		g_value_set_boolean (value, nm_setting_proxy_get_socks_version_5 (setting));
		break;
	case PROP_NO_PROXY_FOR:
		g_value_take_boxed (value, nm_setting_proxy_get_no_proxy_for (setting));
		break;
	case PROP_PAC_URL:
		g_value_set_string (value, nm_setting_proxy_get_pac_url (setting));
		break;
	case PROP_PAC_SCRIPT:
		g_value_set_string (value, nm_setting_proxy_get_pac_script (setting));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingProxyPrivate *priv = NM_SETTING_PROXY_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_METHOD:
		priv->method = g_value_get_int (value);
		break;
	case PROP_HTTP_PROXY:
		g_free (priv->http_proxy);
		priv->http_proxy = g_value_dup_string (value);
		break;
	case PROP_HTTP_PORT:
		priv->http_port = g_value_get_uint (value);
		break;
	case PROP_HTTP_DEFAULT:
		priv->http_default = g_value_get_boolean (value);
		break;
	case PROP_SSL_PROXY:
		g_free (priv->ssl_proxy);
		/* Check if HTTP Proxy has been set for all Protocols */
		priv->ssl_proxy = priv->http_default == TRUE ? g_strdup (priv->http_proxy) : g_value_dup_string (value);
		break;
	case PROP_SSL_PORT:
		priv->ssl_port = priv->http_default == TRUE ? priv->http_port : g_value_get_uint (value);
		break;
	case PROP_FTP_PROXY:
		g_free (priv->ftp_proxy);
		priv->ftp_proxy = priv->http_default == TRUE ? g_strdup (priv->http_proxy) : g_value_dup_string (value);
		break;
	case PROP_FTP_PORT:
		priv->ftp_port = priv->http_default == TRUE ? priv->http_port : g_value_get_uint (value);
		break;
	case PROP_SOCKS_PROXY:
		g_free (priv->socks_proxy);
		priv->socks_proxy = priv->http_default == TRUE ? g_strdup (priv->http_proxy) : g_value_dup_string (value);
		break;
	case PROP_SOCKS_PORT:
		priv->socks_port = priv->http_default == TRUE ? priv->http_port : g_value_get_uint (value);
		break;
	case PROP_SOCKS_VERSION_5:
		priv->socks_version_5 = g_value_get_boolean (value);
		break;
	case PROP_NO_PROXY_FOR:
		g_ptr_array_unref (priv->no_proxy_for);
		priv->no_proxy_for = _nm_utils_strv_to_ptrarray (g_value_get_boxed (value));
		break;
	case PROP_PAC_URL:
		g_free (priv->pac_url);
		priv->pac_url = g_value_dup_string (value);
		break;
	case PROP_PAC_SCRIPT:
		g_free (priv->pac_script);
		priv->pac_script = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_setting_proxy_class_init (NMSettingProxyClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingProxyPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	/**
	 * NMSettingProxy:method:
	 *
	 * Method for proxy configuration, Default is "NONE"
	 **/
	g_object_class_install_property
	    (object_class, PROP_METHOD,
	     g_param_spec_int (NM_SETTING_PROXY_METHOD, "", "",
	                       G_MININT32, G_MAXINT32, NM_SETTING_PROXY_METHOD_NONE,
	                       G_PARAM_READWRITE |
	                       G_PARAM_CONSTRUCT |
	                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:http-proxy:
	 *
	 * HTTP proxy
	 **/
	g_object_class_install_property
	    (object_class, PROP_HTTP_PROXY,
	     g_param_spec_string (NM_SETTING_PROXY_HTTP_PROXY, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:http-port:
	 *
	 * Port number for HTTP proxy
	 **/
	g_object_class_install_property
	    (object_class, PROP_HTTP_PORT,
	     g_param_spec_uint (NM_SETTING_PROXY_HTTP_PORT, "", "",
	                        0, G_MAXUINT32, 0,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:http-default:
	 *
	 * Make HTTP proxy default for all protocols.
	 **/
	g_object_class_install_property
	    (object_class, PROP_HTTP_DEFAULT,
	     g_param_spec_boolean (NM_SETTING_PROXY_HTTP_DEFAULT, "", "",
	                           FALSE,
	                           G_PARAM_READWRITE |
	                           G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:ssl-proxy:
	 *
	 * SSL proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_SSL_PROXY,
	     g_param_spec_string (NM_SETTING_PROXY_SSL_PROXY, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:ssl-port:
	 *
	 * Port number for SSL proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_SSL_PORT,
	     g_param_spec_uint (NM_SETTING_PROXY_SSL_PORT, "", "",
	                        0, G_MAXUINT32, 0,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:ftp-proxy:
	 *
	 * FTP proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_FTP_PROXY,
	     g_param_spec_string (NM_SETTING_PROXY_FTP_PROXY, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:ftp-port:
	 *
	 * Port number for FTP proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_FTP_PORT,
	     g_param_spec_uint (NM_SETTING_PROXY_FTP_PORT, "", "",
	                        0, G_MAXUINT32, 0,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:socks-proxy:
	 *
	 * SOCKS proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_SOCKS_PROXY,
	     g_param_spec_string (NM_SETTING_PROXY_SOCKS_PROXY, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:socks-port:
	 *
	 * Port number for SOCKS proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_SOCKS_PORT,
	     g_param_spec_uint (NM_SETTING_PROXY_SOCKS_PORT, "", "",
	                        0, G_MAXUINT32, 0,
	                        G_PARAM_READWRITE |
	                        G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:socks-version-5:
	 *
	 * set %TRUE if SOCKS version is 5.
	 **/
	g_object_class_install_property
	    (object_class, PROP_SOCKS_VERSION_5,
	     g_param_spec_boolean (NM_SETTING_PROXY_SOCKS_VERSION_5, "", "",
	                           FALSE,
	                           G_PARAM_READWRITE |
	                           G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:nm-proxy-for:
	 *
	 * Array of host to be excluded from proxy.
	 **/
	g_object_class_install_property
	    (object_class, PROP_NO_PROXY_FOR,
	     g_param_spec_boxed (NM_SETTING_PROXY_NO_PROXY_FOR, "", "",
	                         G_TYPE_STRV,
	                         G_PARAM_READWRITE |
	                         G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:pac-url:
	 *
	 * PAC Url for obtaining PAC File.
	 **/
	g_object_class_install_property
	    (object_class, PROP_PAC_URL,
	     g_param_spec_string (NM_SETTING_PROXY_PAC_URL, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingProxy:pac-script:
	 *
	 * PAC Script explicitly entered.
	 **/
	g_object_class_install_property
	    (object_class, PROP_PAC_SCRIPT,
	     g_param_spec_string (NM_SETTING_PROXY_PAC_SCRIPT, "", "",
	                          NULL,
	                          G_PARAM_READWRITE |
	                          G_PARAM_STATIC_STRINGS));
}
