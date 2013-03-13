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
 * Copyright (C) 2004 - 2005 Colin Walters <walters@redhat.com>
 * Copyright (C) 2004 - 2013 Red Hat, Inc.
 * Copyright (C) 2005 - 2008 Novell, Inc.
 *   and others
 */

#include "config.h"

#include <errno.h>
#include <resolv.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "nm-dns-manager.h"
#include "nm-ip4-config.h"
#include "nm-ip6-config.h"
#include "nm-logging.h"
#include "NetworkManagerUtils.h"
#include "nm-posix-signals.h"
#include "nm-config.h"
#include "nm-glib-compat.h"
#include "nm-dbus-manager.h"
#include "nm-dbus-glib-types.h"

#include "nm-dns-plugin.h"
#include "nm-dns-dnsmasq.h"
#include "nm-dns-manager-glue.h"

G_DEFINE_TYPE (NMDnsManager, nm_dns_manager, G_TYPE_OBJECT)

#define NM_DNS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                       NM_TYPE_DNS_MANAGER, \
                                       NMDnsManagerPrivate))

#define HASH_LEN 20

typedef struct {

	char *hostname;
	guint updates_queue;

	/* Raw config data */
	NMIP4Config *ip4_vpn_config;
	NMIP4Config *ip4_device_config;
	NMIP6Config *ip6_vpn_config;
	NMIP6Config *ip6_device_config;
	GSList *configs;

	/* Parsed config data */
	GPtrArray *nameservers;
	const char *domain;
	GPtrArray *searches;
	const char *nis_domain;
	GPtrArray *nis_servers;
	GPtrArray *wins_servers;

	guint8 hash[HASH_LEN];  /* SHA1 hash of current DNS config */
	guint8 prev_hash[HASH_LEN];  /* Hash when begin_updates() was called */

	gboolean manage_dns;
	NMDnsPlugin *plugin;

	gboolean dns_touched;
} NMDnsManagerPrivate;

enum {
	PROPERTIES_CHANGED,

	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_NAMESERVERS,
	PROP_DOMAIN,
	PROP_SEARCHES,
	PROP_NIS_DOMAIN,
	PROP_NIS_SERVERS,
	PROP_WINS_SERVERS,
	PROP_SPLIT_DNS
};

static void
add_string_item (GPtrArray *array, const char *str)
{
	int i;

	g_return_if_fail (array != NULL);
	g_return_if_fail (str != NULL);

	/* Check for dupes before adding */
	for (i = 0; i < array->len; i++) {
		const char *candidate = g_ptr_array_index (array, i);

		if (candidate && !strcmp (candidate, str))
			return;
	}

	/* No dupes, add the new item */
	g_ptr_array_add (array, g_strdup (str));
}

static void
merge_one_ip4_config (NMDnsManager *self, NMIP4Config *src)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	guint32 num, i;

	num = nm_ip4_config_get_num_nameservers (src);
	for (i = 0; i < num; i++) {
		guint32 addr;
		char buf[INET_ADDRSTRLEN];

		addr = nm_ip4_config_get_nameserver (src, i);
		if (inet_ntop (AF_INET, &addr, buf, INET_ADDRSTRLEN) > 0)
			add_string_item (priv->nameservers, buf);
	}

	num = nm_ip4_config_get_num_domains (src);
	for (i = 0; i < num; i++) {
		const char *domain;

		domain = nm_ip4_config_get_domain (src, i);
		if (!priv->domain)
			priv->domain = domain;
		add_string_item (priv->searches, domain);
	}

	num = nm_ip4_config_get_num_searches (src);
	for (i = 0; i < num; i++)
		add_string_item (priv->searches, nm_ip4_config_get_search (src, i));

	/* NIS stuff */
	num = nm_ip4_config_get_num_nis_servers (src);
	for (i = 0; i < num; i++) {
		guint32 addr;
		char buf[INET_ADDRSTRLEN];

		addr = nm_ip4_config_get_nis_server (src, i);
		if (inet_ntop (AF_INET, &addr, buf, INET_ADDRSTRLEN) > 0)
			add_string_item (priv->nis_servers, buf);
	}

	if (nm_ip4_config_get_nis_domain (src)) {
		/* FIXME: handle multiple domains */
		if (!priv->nis_domain)
			priv->nis_domain = nm_ip4_config_get_nis_domain (src);
	}

	/* WINS stuff */
	num = nm_ip4_config_get_num_wins (src);
	for (i = 0; i < num; i++) {
		struct in_addr addr;
		char buf[INET_ADDRSTRLEN];

		addr.s_addr = nm_ip4_config_get_wins (src, i);
		if (inet_ntop (AF_INET, &addr, buf, INET_ADDRSTRLEN) > 0)
			add_string_item (priv->wins_servers, buf);
	}
}

static void
merge_one_ip6_config (NMDnsManager *self, NMIP6Config *src)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	guint32 num, i;
	const char *iface;

	iface = g_object_get_data (G_OBJECT (src), IP_CONFIG_IFACE_TAG);
	g_assert (iface);

	num = nm_ip6_config_get_num_nameservers (src);
	for (i = 0; i < num; i++) {
		const struct in6_addr *addr;
		char buf[INET6_ADDRSTRLEN];
		char *tmp;

		addr = nm_ip6_config_get_nameserver (src, i);

		/* inet_ntop is probably supposed to do this for us, but it doesn't */
		if (IN6_IS_ADDR_V4MAPPED (addr)) {
			if (inet_ntop (AF_INET, &(addr->s6_addr32[3]), buf, INET_ADDRSTRLEN) > 0)
				add_string_item (priv->nameservers, buf);
		} else {
			if (inet_ntop (AF_INET6, addr, buf, INET6_ADDRSTRLEN) > 0) {
				if (IN6_IS_ADDR_LINKLOCAL (addr)) {
					tmp = g_strdup_printf ("%s%%%s", buf, iface);
					add_string_item (priv->nameservers, tmp);
					g_free (tmp);
				} else
					add_string_item (priv->nameservers, buf);
			}
		}
	}

	num = nm_ip6_config_get_num_domains (src);
	for (i = 0; i < num; i++) {
		const char *domain;

		domain = nm_ip6_config_get_domain (src, i);
		if (!priv->domain)
			priv->domain = domain;
		add_string_item (priv->searches, domain);
	}

	num = nm_ip6_config_get_num_searches (src);
	for (i = 0; i < num; i++)
		add_string_item (priv->searches, nm_ip6_config_get_search (src, i));
}

#if defined(NETCONFIG_PATH)
/**********************************/
/* SUSE */

static void
netconfig_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	pid_t pid = getpid ();
	setpgid (pid, pid);

	/*
	 * We blocked signals in main(). We need to restore original signal
	 * mask for netconfig here so that it can receive signals.
	 */
	nm_unblock_posix_signals (NULL);
}

static GPid
run_netconfig (GError **error, gint *stdin_fd)
{
	char *argv[5];
	char *tmp;
	GPid pid = -1;

	argv[0] = NETCONFIG_PATH;
	argv[1] = "modify";
	argv[2] = "--service";
	argv[3] = "NetworkManager";
	argv[4] = NULL;

	tmp = g_strjoinv (" ", argv);
	nm_log_dbg (LOGD_DNS, "spawning '%s'", tmp);
	g_free (tmp);

	if (!g_spawn_async_with_pipes (NULL, argv, NULL, 0, netconfig_child_setup,
	                               NULL, &pid, stdin_fd, NULL, NULL, error))
		return -1;

	return pid;
}

static void
write_to_netconfig (gint fd, const char *key, const char *value)
{
	char *str;
	int x;

	str = g_strdup_printf ("%s='%s'\n", key, value);
	nm_log_dbg (LOGD_DNS, "writing to netconfig: %s", str);
	x = write (fd, str, strlen (str));
	g_free (str);
}

static gboolean
dispatch_netconfig (const char *domain,
                    char **searches,
                    char **nameservers,
                    const char *nis_domain,
                    char **nis_servers,
                    GError **error)
{
	char *str, *tmp;
	GPid pid;
	gint fd;
	int ret;

	pid = run_netconfig (error, &fd);
	if (pid < 0)
		return FALSE;

	/* NM is writing already-merged DNS information to netconfig, so it
	 * does not apply to a specific network interface.
	 */
	write_to_netconfig (fd, "INTERFACE", "NetworkManager");

	if (searches) {
		str = g_strjoinv (" ", searches);

		if (domain) {
			tmp = g_strconcat (domain, " ", str, NULL);
			g_free (str);
			str = tmp;
		}

		write_to_netconfig (fd, "DNSSEARCH", str);
		g_free (str);
	}

	if (nameservers) {
		str = g_strjoinv (" ", nameservers);
		write_to_netconfig (fd, "DNSSERVERS", str);
		g_free (str);
	}

	if (nis_domain)
		write_to_netconfig (fd, "NISDOMAIN", nis_domain);

	if (nis_servers) {
		str = g_strjoinv (" ", nis_servers);
		write_to_netconfig (fd, "NISSERVERS", str);
		g_free (str);
	}

	close (fd);

	/* Wait until the process exits */

 again:

	ret = waitpid (pid, NULL, 0);
	if (ret < 0 && errno == EINTR)
		goto again;
	else if (ret < 0 && errno == ECHILD) {
		/* When the netconfig exist, the errno is ECHILD, it should return TRUE */
		return TRUE;
	}

	return ret > 0;
}
#endif


static gboolean
write_resolv_conf (FILE *f, const char *domain,
                   char **searches,
                   char **nameservers,
                   GError **error)
{
	char *domain_str = NULL;
	char *searches_str = NULL;
	char *nameservers_str = NULL;
	int i;
	gboolean retval = FALSE;
	GString *str;

	if (fprintf (f, "%s","# Generated by NetworkManager\n") < 0) {
		g_set_error (error,
		             NM_DNS_MANAGER_ERROR,
		             NM_DNS_MANAGER_ERROR_SYSTEM,
		             "Could not write " _PATH_RESCONF ": %s\n",
		             g_strerror (errno));
		return FALSE;
	}

	if (domain)
		domain_str = g_strconcat ("domain ", domain, "\n", NULL);

	if (searches) {
		char *tmp_str;

		tmp_str = g_strjoinv (" ", searches);
		searches_str = g_strconcat ("search ", tmp_str, "\n", NULL);
		g_free (tmp_str);
	}

	str = g_string_new ("");

	if (nameservers) {
		int num = g_strv_length (nameservers);

		for (i = 0; i < num; i++) {
			if (i == 3) {
				g_string_append (str, "# ");
				g_string_append (str, _("NOTE: the libc resolver may not support more than 3 nameservers."));
				g_string_append (str, "\n# ");
				g_string_append (str, _("The nameservers listed below may not be recognized."));
				g_string_append_c (str, '\n');
			}

			g_string_append (str, "nameserver ");
			g_string_append (str, nameservers[i]);
			g_string_append_c (str, '\n');
		}
	}

	nameservers_str = g_string_free (str, FALSE);

	if (fprintf (f, "%s%s%s",
	             domain_str ? domain_str : "",
	             searches_str ? searches_str : "",
	             strlen (nameservers_str) ? nameservers_str : "") != -1)
		retval = TRUE;

	g_free (domain_str);
	g_free (searches_str);
	g_free (nameservers_str);

	return retval;
}

#ifdef RESOLVCONF_PATH
static gboolean
dispatch_resolvconf (const char *domain,
                     char **searches,
                     char **nameservers,
                     GError **error)
{
	char *cmd;
	FILE *f;
	gboolean retval = FALSE;

	if (! g_file_test (RESOLVCONF_PATH, G_FILE_TEST_IS_EXECUTABLE))
		return FALSE;

	if (domain || searches || nameservers) {
		cmd = g_strconcat (RESOLVCONF_PATH, " -a ", "NetworkManager", NULL);
		nm_log_info (LOGD_DNS, "Writing DNS information to %s", RESOLVCONF_PATH);
		if ((f = popen (cmd, "w")) == NULL)
			g_set_error (error,
			             NM_DNS_MANAGER_ERROR,
			             NM_DNS_MANAGER_ERROR_SYSTEM,
			             "Could not write to %s: %s\n",
			             RESOLVCONF_PATH,
			             g_strerror (errno));
		else {
			retval = write_resolv_conf (f, domain, searches, nameservers, error);
			retval &= (pclose (f) == 0);
		}
	} else {
		cmd = g_strconcat (RESOLVCONF_PATH, " -d ", "NetworkManager", NULL);
		nm_log_info (LOGD_DNS, "Removing DNS information from %s", RESOLVCONF_PATH);
		if (nm_spawn_process (cmd) == 0)
			retval = TRUE;
	}

	g_free (cmd);

	return retval;
}
#endif

static gboolean
update_resolv_conf (const char *domain,
                    const char **searches,
                    const char **nameservers,
                    GError **error)
{
	char *tmp_resolv_conf;
	char *tmp_resolv_conf_realpath;
	char *resolv_conf_realpath;
	FILE *f;
	int do_rename = 1;
	int old_errno = 0;

	g_return_val_if_fail (error != NULL, FALSE);

	/* Find the real path of resolv.conf; it could be a symlink to something */
	resolv_conf_realpath = realpath (_PATH_RESCONF, NULL);
	if (!resolv_conf_realpath)
		resolv_conf_realpath = strdup (_PATH_RESCONF);

	/* Build up the real path for the temp resolv.conf that we're about to
	 * write out.
	 */
	tmp_resolv_conf = g_strdup_printf ("%s.tmp", resolv_conf_realpath);
	tmp_resolv_conf_realpath = realpath (tmp_resolv_conf, NULL);
	if (!tmp_resolv_conf_realpath)
		tmp_resolv_conf_realpath = strdup (tmp_resolv_conf);
	g_free (tmp_resolv_conf);
	tmp_resolv_conf = NULL;

	if ((f = fopen (tmp_resolv_conf_realpath, "w")) == NULL) {
		do_rename = 0;
		old_errno = errno;
		if ((f = fopen (_PATH_RESCONF, "w")) == NULL) {
			g_set_error (error,
			             NM_DNS_MANAGER_ERROR,
			             NM_DNS_MANAGER_ERROR_SYSTEM,
			             "Could not open %s: %s\nCould not open %s: %s\n",
			             tmp_resolv_conf_realpath,
			             g_strerror (old_errno),
			             _PATH_RESCONF,
			             g_strerror (errno));
			goto out;
		}
		/* Update tmp_resolv_conf_realpath so the error message on fclose()
		 * failure will be correct.
		 */
		strcpy (tmp_resolv_conf_realpath, _PATH_RESCONF);
	}

	write_resolv_conf (f, domain, (char **) searches, (char **) nameservers, error);

	if (fclose (f) < 0) {
		if (*error == NULL) {
			/* only set an error here if write_resolv_conf() was successful,
			 * since its error is more important.
			 */
			g_set_error (error,
			             NM_DNS_MANAGER_ERROR,
			             NM_DNS_MANAGER_ERROR_SYSTEM,
			             "Could not close %s: %s\n",
			             tmp_resolv_conf_realpath,
			             g_strerror (errno));
		}
	}

	/* Don't rename the tempfile over top of the existing resolv.conf if there
	 * was an error writing it out.
	 */
	if (*error == NULL && do_rename) {
		if (rename (tmp_resolv_conf_realpath, resolv_conf_realpath) < 0) {
			g_set_error (error,
			             NM_DNS_MANAGER_ERROR,
			             NM_DNS_MANAGER_ERROR_SYSTEM,
			             "Could not replace " _PATH_RESCONF ": %s\n",
			             g_strerror (errno));
		}
	}

out:
	free (tmp_resolv_conf_realpath);
	free (resolv_conf_realpath);
	return *error ? FALSE : TRUE;
}

static void
compute_hash (NMDnsManager *self, guint8 buffer[HASH_LEN])
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	GChecksum *sum;
	GSList *iter;
	gsize len = HASH_LEN;

	sum = g_checksum_new (G_CHECKSUM_SHA1);
	g_assert (len == g_checksum_type_get_length (G_CHECKSUM_SHA1));

	if (priv->ip4_vpn_config)
		nm_ip4_config_hash (priv->ip4_vpn_config, sum, TRUE);
	if (priv->ip4_device_config)
		nm_ip4_config_hash (priv->ip4_device_config, sum, TRUE);

	if (priv->ip6_vpn_config)
		nm_ip6_config_hash (priv->ip6_vpn_config, sum, TRUE);
	if (priv->ip6_device_config)
		nm_ip6_config_hash (priv->ip6_device_config, sum, TRUE);

	/* add any other configs we know about */
	for (iter = priv->configs; iter; iter = g_slist_next (iter)) {
		if (   (iter->data == priv->ip4_vpn_config)
		    && (iter->data == priv->ip4_device_config)
		    && (iter->data == priv->ip6_vpn_config)
		    && (iter->data == priv->ip6_device_config))
			continue;

		if (NM_IS_IP4_CONFIG (iter->data))
			nm_ip4_config_hash (NM_IP4_CONFIG (iter->data), sum, TRUE);
		else if (NM_IS_IP6_CONFIG (iter->data))
			nm_ip6_config_hash (NM_IP6_CONFIG (iter->data), sum, TRUE);
	}

	g_checksum_get_digest (sum, buffer, &len);
	g_checksum_free (sum);
}

static gboolean
update_dns (NMDnsManager *self,
            gboolean no_caching,
            GError **error)
{
	NMDnsManagerPrivate *priv;
	GSList *iter, *vpn_configs = NULL, *dev_configs = NULL, *other_configs = NULL;
	int num, i, len;
	gboolean success = FALSE, caching = FALSE;
	GError *my_error = NULL;
	GObject *object;

	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (*error == NULL, FALSE);

	priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	if (!priv->manage_dns)
		return TRUE;

	priv->dns_touched = TRUE;

	nm_log_dbg (LOGD_DNS, "updating resolv.conf");

	/* Update hash with config we're applying */
	compute_hash (self, priv->hash);

	/* Now process the new data */
	g_ptr_array_set_size (priv->nameservers, 0);
	g_ptr_array_set_size (priv->searches, 0);
	g_ptr_array_set_size (priv->nis_servers, 0);
	g_ptr_array_set_size (priv->wins_servers, 0);
	priv->domain = NULL;
	priv->nis_domain = NULL;

	if (priv->ip4_vpn_config)
		merge_one_ip4_config (self, priv->ip4_vpn_config);
	if (priv->ip4_device_config)
		merge_one_ip4_config (self, priv->ip4_device_config);

	if (priv->ip6_vpn_config)
		merge_one_ip6_config (self, priv->ip6_vpn_config);
	if (priv->ip6_device_config)
		merge_one_ip6_config (self, priv->ip6_device_config);

	for (iter = priv->configs; iter; iter = g_slist_next (iter)) {
		if (   (iter->data == priv->ip4_vpn_config)
		    || (iter->data == priv->ip4_device_config)
		    || (iter->data == priv->ip6_vpn_config)
		    || (iter->data == priv->ip6_device_config))
			continue;

		if (NM_IS_IP4_CONFIG (iter->data)) {
			NMIP4Config *config = NM_IP4_CONFIG (iter->data);

			merge_one_ip4_config (self, config);
		} else if (NM_IS_IP6_CONFIG (iter->data)) {
			NMIP6Config *config = NM_IP6_CONFIG (iter->data);

			merge_one_ip6_config (self, config);
		} else
			g_assert_not_reached ();
	}

	/* Add the current domain name (from the hostname) to the searches list;
	 * see rh #600407.  The bug report is that when the hostname is set to
	 * something like 'dcbw.foobar.com' (ie an FQDN) that pinging 'dcbw' doesn't
	 * work because the resolver doesn't have anything to append to 'dcbw' when
	 * looking it up.
	 */
	if (priv->hostname) {
		const char *hostsearch = strchr (priv->hostname, '.');

		/* +1 to get rid of the dot */
		if (hostsearch && strlen (hostsearch + 1))
			add_string_item (priv->searches, hostsearch + 1);
	}

	/* Per 'man resolv.conf', the search list is limited to 6 domains
	 * totalling 256 characters.
	 */
	num = MIN (priv->searches->len, 6);
	for (i = 0, len = 0; i < num; i++) {
		len += strlen (priv->searches->pdata[i]) + 1; /* +1 for spaces */
		if (len > 256)
			break;
	}
	g_ptr_array_set_size (priv->searches, i);

	g_ptr_array_add (priv->nameservers, NULL);
	g_ptr_array_add (priv->searches, NULL);
	g_ptr_array_add (priv->nis_servers, NULL);
	g_ptr_array_add (priv->wins_servers, NULL);

	/* Build up config lists for plugins; we use the raw configs here, not the
	 * merged information that we write to resolv.conf so that the plugins can
	 * still use the domain information in each config to provide split DNS if
	 * they want to.
	 */
	if (priv->ip4_vpn_config)
		vpn_configs = g_slist_append (vpn_configs, priv->ip4_vpn_config);
	if (priv->ip6_vpn_config)
		vpn_configs = g_slist_append (vpn_configs, priv->ip6_vpn_config);
	if (priv->ip4_device_config)
		dev_configs = g_slist_append (dev_configs, priv->ip4_device_config);
	if (priv->ip6_device_config)
		dev_configs = g_slist_append (dev_configs, priv->ip6_device_config);
	for (iter = priv->configs; iter; iter = g_slist_next (iter)) {
		if (   (iter->data != priv->ip4_vpn_config)
		    && (iter->data != priv->ip4_device_config)
		    && (iter->data != priv->ip6_vpn_config)
		    && (iter->data != priv->ip6_device_config))
			other_configs = g_slist_append (other_configs, iter->data);
	}

	/* Let any plugins do their thing first */
	if (priv->plugin) {
		NMDnsPlugin *plugin = priv->plugin;
		const char *plugin_name = nm_dns_plugin_get_name (plugin);

		if (nm_dns_plugin_is_caching (plugin)) {
			if (no_caching) {
				nm_log_dbg (LOGD_DNS, "DNS: plugin %s ignored (caching disabled)",
				            plugin_name);
				goto skip;
			}
			caching = TRUE;
		}

		nm_log_dbg (LOGD_DNS, "DNS: updating plugin %s", plugin_name);
		if (!nm_dns_plugin_update (plugin,
		                           vpn_configs,
		                           dev_configs,
		                           other_configs,
		                           priv->hostname)) {
			nm_log_warn (LOGD_DNS, "DNS: plugin %s update failed", plugin_name);

			/* If the plugin failed to update, we shouldn't write out a local
			 * caching DNS configuration to resolv.conf.
			 */
			caching = FALSE;
		}

	skip:
		;
	}

	g_slist_free (vpn_configs);
	g_slist_free (dev_configs);
	g_slist_free (other_configs);

	/* If caching was successful, we only send 127.0.0.1 to /etc/resolv.conf
	 * to ensure that the glibc resolver doesn't try to round-robin nameservers,
	 * but only uses the local caching nameserver.
	 */
	if (caching) {
		g_ptr_array_set_size (priv->nameservers, 0);
		add_string_item (priv->nameservers, "127.0.0.1");
	}

#ifdef RESOLVCONF_PATH
	success = dispatch_resolvconf (nm_dns_manager_get_domain (self),
	                               nm_dns_manager_get_searches (self),
	                               nm_dns_manager_get_nameservers (self),
	                               &my_error);
	if (my_error) {
		nm_log_warn (LOGD_DNS, "DNS: resolvconf failed: %s", my_error->message);
		g_clear_error (&my_error);
	}
#endif

#ifdef NETCONFIG_PATH
	if (success == FALSE) {
		success = dispatch_netconfig (nm_dns_manager_get_domain (self),
		                              nm_dns_manager_get_searches (self),
		                              nm_dns_manager_get_nameservers (self),
		                              nm_dns_manager_get_nis_domain (self),
		                              nm_dns_manager_get_nis_servers (self),
		                              &my_error);
		if (my_error) {
			nm_log_warn (LOGD_DNS, "DNS: netconfig failed: %s", my_error->message);
			g_clear_error (&my_error);
		}
	}
#endif

	if (success == FALSE) {
		success = update_resolv_conf (nm_dns_manager_get_domain (self),
		                              nm_dns_manager_get_searches (self),
		                              nm_dns_manager_get_nameservers (self),
		                              &my_error);
		if (my_error) {
			nm_log_warn (LOGD_DNS, "DNS: resolv.conf update failed: %s", my_error->message);
			g_propagate_error (error, my_error);
		}
	}

	/* signal that resolv.conf was changed */
	object = G_OBJECT (self);
	g_object_freeze_notify (object);
	g_object_notify (object, NM_DNS_MANAGER_NAMESERVERS);
	g_object_notify (object, NM_DNS_MANAGER_DOMAIN);
	g_object_notify (object, NM_DNS_MANAGER_SEARCHES);
	g_object_notify (object, NM_DNS_MANAGER_NIS_DOMAIN);
	g_object_notify (object, NM_DNS_MANAGER_NIS_SERVERS);
	g_object_notify (object, NM_DNS_MANAGER_WINS_SERVERS);
	g_object_notify (object, NM_DNS_MANAGER_SPLIT_DNS);
	g_object_thaw_notify (object);

	return success;
}

static void
plugin_failed (NMDnsPlugin *plugin, gpointer user_data)
{
	NMDnsManager *self = NM_DNS_MANAGER (user_data);
	GError *error = NULL;

	/* Errors with non-caching plugins aren't fatal */
	if (!nm_dns_plugin_is_caching (plugin))
		return;

	/* Disable caching until the next DNS update */
	if (!update_dns (self, TRUE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}
}

gboolean
nm_dns_manager_add_ip4_config (NMDnsManager *mgr,
                               const char *iface,
                               NMIP4Config *config,
                               NMDnsIPConfigType cfg_type)
{
	NMDnsManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);

	g_object_set_data_full (G_OBJECT (config), IP_CONFIG_IFACE_TAG, g_strdup (iface), g_free);

	switch (cfg_type) {
	case NM_DNS_IP_CONFIG_TYPE_VPN:
		priv->ip4_vpn_config = config;
		break;
	case NM_DNS_IP_CONFIG_TYPE_BEST_DEVICE:
		priv->ip4_device_config = config;
		break;
	default:
		break;
	}

	/* Don't allow the same zone added twice */
	if (!g_slist_find (priv->configs, config))
		priv->configs = g_slist_append (priv->configs, g_object_ref (config));

	if (!priv->updates_queue && !update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	return TRUE;
}

gboolean
nm_dns_manager_remove_ip4_config (NMDnsManager *mgr, NMIP4Config *config)
{
	NMDnsManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);

	/* Can't remove it if it wasn't in the list to begin with */
	if (!g_slist_find (priv->configs, config))
		return FALSE;

	priv->configs = g_slist_remove (priv->configs, config);

	if (config == priv->ip4_vpn_config)
		priv->ip4_vpn_config = NULL;
	if (config == priv->ip4_device_config)
		priv->ip4_device_config = NULL;

	g_object_unref (config);

	if (!priv->updates_queue && !update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	g_object_set_data (G_OBJECT (config), IP_CONFIG_IFACE_TAG, NULL);

	return TRUE;
}

gboolean
nm_dns_manager_add_ip6_config (NMDnsManager *mgr,
                               const char *iface,
                               NMIP6Config *config,
                               NMDnsIPConfigType cfg_type)
{
	NMDnsManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);

	g_object_set_data_full (G_OBJECT (config), IP_CONFIG_IFACE_TAG, g_strdup (iface), g_free);

	switch (cfg_type) {
	case NM_DNS_IP_CONFIG_TYPE_VPN:
		priv->ip6_vpn_config = config;
		break;
	case NM_DNS_IP_CONFIG_TYPE_BEST_DEVICE:
		priv->ip6_device_config = config;
		break;
	default:
		break;
	}

	/* Don't allow the same zone added twice */
	if (!g_slist_find (priv->configs, config))
		priv->configs = g_slist_append (priv->configs, g_object_ref (config));

	if (!priv->updates_queue && !update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	return TRUE;
}

gboolean
nm_dns_manager_remove_ip6_config (NMDnsManager *mgr, NMIP6Config *config)
{
	NMDnsManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);

	/* Can't remove it if it wasn't in the list to begin with */
	if (!g_slist_find (priv->configs, config))
		return FALSE;

	priv->configs = g_slist_remove (priv->configs, config);

	if (config == priv->ip6_vpn_config)
		priv->ip6_vpn_config = NULL;
	if (config == priv->ip6_device_config)
		priv->ip6_device_config = NULL;

	g_object_unref (config);	

	if (!priv->updates_queue && !update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	g_object_set_data (G_OBJECT (config), IP_CONFIG_IFACE_TAG, NULL);

	return TRUE;
}

void
nm_dns_manager_set_hostname (NMDnsManager *mgr,
                               const char *hostname)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);
	GError *error = NULL;
	const char *filtered = NULL;

	/* Certain hostnames we don't want to include in resolv.conf 'searches' */
	if (   hostname
	    && strcmp (hostname, "localhost.localdomain")
	    && strcmp (hostname, "localhost6.localdomain6")
	    && !strstr (hostname, ".in-addr.arpa")
	    && strchr (hostname, '.')) {
		filtered = hostname;
	}

	if (   (!priv->hostname && !filtered)
	    || (priv->hostname && filtered && !strcmp (priv->hostname, filtered)))
		return;

	g_free (priv->hostname);
	priv->hostname = g_strdup (filtered);

	if (!priv->updates_queue && !update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}
}

void
nm_dns_manager_begin_updates (NMDnsManager *mgr, const char *func)
{
	NMDnsManagerPrivate *priv;

	g_return_if_fail (mgr != NULL);
	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);

	/* Save current hash when starting a new batch */
	if (priv->updates_queue == 0)
		memcpy (priv->prev_hash, priv->hash, sizeof (priv->hash));

	priv->updates_queue++;

	nm_log_dbg (LOGD_DNS, "(%s): queueing DNS updates (%d)", func, priv->updates_queue);
}

void
nm_dns_manager_end_updates (NMDnsManager *mgr, const char *func)
{
	NMDnsManagerPrivate *priv;
	GError *error = NULL;
	gboolean changed;
	guint8 new[HASH_LEN];

	g_return_if_fail (mgr != NULL);

	priv = NM_DNS_MANAGER_GET_PRIVATE (mgr);
	g_return_if_fail (priv->updates_queue > 0);

	compute_hash (mgr, new);
	changed = (memcmp (new, priv->prev_hash, sizeof (new)) != 0) ? TRUE : FALSE;
	nm_log_dbg (LOGD_DNS, "(%s): DNS configuration %s", __func__, changed ? "changed" : "did not change");

	priv->updates_queue--;
	if ((priv->updates_queue > 0) || (changed == FALSE)) {
		nm_log_dbg (LOGD_DNS, "(%s): no DNS changes to commit (%d)", func, priv->updates_queue);
		return;
	}

	/* Commit all the outstanding changes */
	nm_log_dbg (LOGD_DNS, "(%s): committing DNS changes (%d)", func, priv->updates_queue);
	if (!update_dns (mgr, FALSE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes: (%d) %s",
			         error ? error->code : -1,
			         error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	memset (priv->prev_hash, 0, sizeof (priv->prev_hash));
}

const char **
nm_dns_manager_get_nameservers (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return (const char **)priv->nameservers->pdata;
}

const char *
nm_dns_manager_get_domain (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return priv->domain;
}

const char **
nm_dns_manager_get_searches (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return (const char **)priv->searches->pdata;
}

const char **
nm_dns_manager_get_nis_servers (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return (const char **)priv->nis_servers->pdata;
}

const char *
nm_dns_manager_get_nis_domain (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return priv->nis_domain;
}

const char **
nm_dns_manager_get_wins_servers (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);

	return (const char **)priv->wins_servers->pdata;
}

/******************************************************************/

static void
add_ip4_config_to_split_dns (NMIP4Config *ip4, GPtrArray *networks, GPtrArray *fallback)
{
	gboolean do_fallback = !nm_ip4_config_get_never_default (ip4);
	GValueArray *network;
	GValue value = G_VALUE_INIT;
	GPtrArray *strings;
	char buf[INET_ADDRSTRLEN + 1];
	struct in_addr addr;
	int n, i;
	const char *iface;

	n = nm_ip4_config_get_num_nameservers (ip4);
	if (n == 0)
		return;

	iface = g_object_get_data (G_OBJECT (ip4), IP_CONFIG_IFACE_TAG);
	g_assert (iface);

	if (do_fallback)
		strings = fallback;
	else
		strings = g_ptr_array_new ();

	/* nameservers */
	for (i = 0; i < n; i++) {
		addr.s_addr = nm_ip4_config_get_nameserver (ip4, i);
		inet_ntop (AF_INET, &addr, buf, sizeof (buf));
		g_ptr_array_add (strings, g_strdup (buf));
	}
	if (do_fallback)
		return;

	g_ptr_array_add (strings, NULL);

	network = g_value_array_new (4);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, iface);
	g_value_array_append (network, &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	/* domains */
	strings = g_ptr_array_new ();
	n = nm_ip4_config_get_num_domains (ip4);
	for (i = 0; i < n; i++)
		g_ptr_array_add (strings, g_strdup (nm_ip4_config_get_domain (ip4, i)));
	n = nm_ip4_config_get_num_searches (ip4);
	for (i = 0; i < n; i++)
		g_ptr_array_add (strings, g_strdup (nm_ip4_config_get_search (ip4, i)));
	g_ptr_array_add (strings, NULL);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	/* IP ranges */
	strings = g_ptr_array_new ();
	n = nm_ip4_config_get_num_routes (ip4);
	for (i = 0; i < n; i++) {
		NMPlatformIP4Route *route = nm_ip4_config_get_route (ip4, i);

		inet_ntop (AF_INET, &route->network, buf, sizeof (buf));
		g_ptr_array_add (strings, g_strdup_printf ("%s/%d", buf, route->plen));
	}

	g_ptr_array_add (strings, NULL);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	g_ptr_array_add (networks, network);
}

#define IP6_ADDR_BUFLEN (INET6_ADDRSTRLEN + 50)

static char *
ip6_addr_to_string (const struct in6_addr *addr, const char *iface)
{
	char *buf;

	/* allocate enough space for the address + interface name */
	buf = g_malloc0 (IP6_ADDR_BUFLEN + 1);

	/* inet_ntop is probably supposed to do this for us, but it doesn't */
	if (IN6_IS_ADDR_V4MAPPED (addr)) {
		if (!inet_ntop (AF_INET, &(addr->s6_addr32[3]), buf, IP6_ADDR_BUFLEN))
			goto error;
		return buf;
	}

	if (!inet_ntop (AF_INET6, addr, buf, INET6_ADDRSTRLEN + 1))
		goto error;

	if (IN6_IS_ADDR_LINKLOCAL (addr)) {
		/* Append the interface name. */
		strncat (buf, "%", IP6_ADDR_BUFLEN - strlen (buf));
		strncat (buf, iface, IP6_ADDR_BUFLEN - strlen (buf));
	}

	return buf;

 error:
	g_free (buf);
	return NULL;
}

static void
add_ip6_config_to_split_dns (NMIP6Config *ip6, GPtrArray *networks, GPtrArray *fallback)
{
	gboolean do_fallback = !nm_ip6_config_get_never_default (ip6);
	GValueArray *network;
	GValue value = G_VALUE_INIT;
	GPtrArray *strings;
	const struct in6_addr *addr;
	char *buf;
	int n, i;
	const char *iface;

	n = nm_ip6_config_get_num_nameservers (ip6);
	if (n == 0)
		return;

	iface = g_object_get_data (G_OBJECT (ip6), IP_CONFIG_IFACE_TAG);
	g_assert (iface);

	if (do_fallback)
		strings = fallback;
	else
		strings = g_ptr_array_new ();

	/* nameservers */
	for (i = 0; i < n; i++) {
		addr = nm_ip6_config_get_nameserver (ip6, 0);
		buf = ip6_addr_to_string (addr, iface);
		g_ptr_array_add (strings, buf);
	}

	if (do_fallback)
		return;

	g_ptr_array_add (strings, NULL);

	network = g_value_array_new (4);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, iface);
	g_value_array_append (network, &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	/* domains */
	strings = g_ptr_array_new ();
	n = nm_ip6_config_get_num_domains (ip6);
	for (i = 0; i < n; i++)
		g_ptr_array_add (strings, g_strdup (nm_ip6_config_get_domain (ip6, i)));
	n = nm_ip6_config_get_num_searches (ip6);
	for (i = 0; i < n; i++)
		g_ptr_array_add (strings, g_strdup (nm_ip6_config_get_search (ip6, i)));
	g_ptr_array_add (strings, NULL);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	/* IP ranges */
	n = nm_ip6_config_get_num_routes (ip6);
	for (i = 0; i < n; i++) {
		NMPlatformIP6Route *route = nm_ip6_config_get_route (ip6, i);

		buf = ip6_addr_to_string (&route->network, iface);		
		g_ptr_array_add (strings, g_strdup_printf ("%s/%d", buf, route->plen));
		g_free (buf);
	}

	g_ptr_array_add (strings, NULL);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (strings, FALSE));
	g_value_array_append (network, &value);
	g_value_unset (&value);

	g_ptr_array_add (networks, network);
}

static GValueArray *
build_split_dns_config (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	GValueArray *config;
	GValue value = G_VALUE_INIT;
	GPtrArray *networks;
	GPtrArray *fallback_ns;
	GSList *iter;

	networks = g_ptr_array_new ();
	fallback_ns = g_ptr_array_new ();

	if (priv->ip4_vpn_config)
		add_ip4_config_to_split_dns (priv->ip4_vpn_config, networks, fallback_ns);
	if (priv->ip6_vpn_config)
		add_ip6_config_to_split_dns (priv->ip6_vpn_config, networks, fallback_ns);
	if (priv->ip4_device_config)
		add_ip4_config_to_split_dns (priv->ip4_device_config, networks, fallback_ns);
	if (priv->ip6_device_config)
		add_ip6_config_to_split_dns (priv->ip6_device_config, networks, fallback_ns);

	for (iter = priv->configs; iter; iter = iter->next) {
		if (   (iter->data == priv->ip4_vpn_config)
		    || (iter->data == priv->ip4_device_config)
		    || (iter->data == priv->ip6_vpn_config)
		    || (iter->data == priv->ip6_device_config))
			continue;

		if (NM_IS_IP4_CONFIG (iter->data))
			add_ip4_config_to_split_dns (iter->data, networks, fallback_ns);
		else if (NM_IS_IP6_CONFIG (iter->data))
			add_ip6_config_to_split_dns (iter->data, networks, fallback_ns);
	}

	g_ptr_array_add (fallback_ns, NULL);

	config = g_value_array_new (2);

	g_value_init (&value, DBUS_TYPE_NM_SPLIT_DNS_NETWORKS);
	g_value_take_boxed (&value, networks);
	g_value_array_append (config, &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRV);
	g_value_take_boxed (&value, g_ptr_array_free (fallback_ns, FALSE));
	g_value_array_append (config, &value);
	g_value_unset (&value);

	return config;
}

/******************************************************************/

NMDnsManager *
nm_dns_manager_get (void)
{
	static NMDnsManager * singleton = NULL;

	if (!singleton) {
		singleton = NM_DNS_MANAGER (g_object_new (NM_TYPE_DNS_MANAGER, NULL));
		g_assert (singleton);
	} else
		g_object_ref (singleton);

	return singleton;
}

GQuark
nm_dns_manager_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("nm_dns_manager_error");

	return quark;
}

static void
nm_dns_manager_init (NMDnsManager *self)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	NMDBusManager *dbus_mgr;
	DBusGConnection *connection;
	const char *mode;

	/* Set the initial hash */
	compute_hash (self, NM_DNS_MANAGER_GET_PRIVATE (self)->hash);

	mode = nm_config_get_dns_mode (nm_config_get ());
	if (!g_strcmp0 (mode, "none")) {
		priv->manage_dns = FALSE;
		nm_log_info (LOGD_DNS, "DNS: not managing " _PATH_RESCONF);
	} else {
		priv->manage_dns = TRUE;
		if (!g_strcmp0 (mode, "dnsmasq"))
			priv->plugin = nm_dns_dnsmasq_new ();
		else if (mode && g_strcmp0 (mode, "default") != 0)
			nm_log_warn (LOGD_DNS, "Unknown DNS mode '%s'", mode);
	}

	if (priv->plugin) {
		nm_log_info (LOGD_DNS, "DNS: loaded plugin %s", nm_dns_plugin_get_name (priv->plugin));
		g_signal_connect (priv->plugin, NM_DNS_PLUGIN_FAILED, G_CALLBACK (plugin_failed), self);
	}

	priv->nameservers = g_ptr_array_new_with_free_func (g_free);
	priv->searches = g_ptr_array_new_with_free_func (g_free);
	priv->nis_servers = g_ptr_array_new_with_free_func (g_free);
	priv->wins_servers = g_ptr_array_new_with_free_func (g_free);

	dbus_mgr = nm_dbus_manager_get ();
	connection = nm_dbus_manager_get_connection (dbus_mgr);
	dbus_g_connection_register_g_object (connection,
	                                     NM_DBUS_PATH_DNS_MANAGER,
	                                     G_OBJECT (self));
	g_object_unref (dbus_mgr);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDnsManager *self = NM_DNS_MANAGER (object);

	switch (prop_id) {
	case PROP_NAMESERVERS:
		g_value_set_boxed (value, nm_dns_manager_get_nameservers (self));
		break;
	case PROP_DOMAIN:
		g_value_set_string (value, nm_dns_manager_get_domain (self));
		break;
	case PROP_SEARCHES:
		g_value_set_boxed (value, nm_dns_manager_get_searches (self));
		break;
	case PROP_NIS_DOMAIN:
		g_value_set_string (value, nm_dns_manager_get_nis_domain (self));
		break;
	case PROP_NIS_SERVERS:
		g_value_set_boxed (value, nm_dns_manager_get_nis_servers (self));
		break;
	case PROP_WINS_SERVERS:
		g_value_set_boxed (value, nm_dns_manager_get_wins_servers (self));
		break;
	case PROP_SPLIT_DNS:
		g_value_take_boxed (value, build_split_dns_config (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
dispose (GObject *object)
{
	NMDnsManager *self = NM_DNS_MANAGER (object);
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (self);
	GError *error = NULL;

	g_clear_object (&priv->plugin);

	/* If we're quitting, leave a valid resolv.conf in place, not one
	 * pointing to 127.0.0.1 if any plugins were active.  Thus update
	 * DNS after disposing of all plugins.  But if we haven't done any
	 * DNS updates yet, there's no reason to touch resolv.conf on shutdown.
	 */
	if (priv->dns_touched && !update_dns (self, TRUE, &error)) {
		nm_log_warn (LOGD_DNS, "could not commit DNS changes on shutdown: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		priv->dns_touched = FALSE;
	}

	g_slist_free_full (priv->configs, g_object_unref);
	priv->configs = NULL;

	g_clear_pointer (&priv->nameservers, g_ptr_array_unref);
	g_clear_pointer (&priv->searches, g_ptr_array_unref);
	g_clear_pointer (&priv->nis_servers, g_ptr_array_unref);
	g_clear_pointer (&priv->wins_servers, g_ptr_array_unref);

	G_OBJECT_CLASS (nm_dns_manager_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMDnsManagerPrivate *priv = NM_DNS_MANAGER_GET_PRIVATE (object);

	g_free (priv->hostname);

	G_OBJECT_CLASS (nm_dns_manager_parent_class)->finalize (object);
}

static void
nm_dns_manager_class_init (NMDnsManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMDnsManagerPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_NAMESERVERS,
		 g_param_spec_boxed (NM_DNS_MANAGER_NAMESERVERS,
		                     "Nameservers",
		                     "resolv.conf nameservers",
		                     G_TYPE_STRV,
		                     G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_DOMAIN,
		 g_param_spec_string (NM_DNS_MANAGER_DOMAIN,
		                      "Domain",
		                      "resolv.conf domain",
		                      NULL,
		                      G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_SEARCHES,
		 g_param_spec_boxed (NM_DNS_MANAGER_SEARCHES,
		                     "Searches",
		                     "resolv.conf searches",
		                     G_TYPE_STRV,
		                     G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_NIS_DOMAIN,
		 g_param_spec_string (NM_DNS_MANAGER_NIS_DOMAIN,
		                      "NIS Domain",
		                      "NIS Domain",
		                      NULL,
		                      G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_NIS_SERVERS,
		 g_param_spec_boxed (NM_DNS_MANAGER_NIS_SERVERS,
		                     "NIS Servers",
		                     "NIS Servers",
		                     G_TYPE_STRV,
		                     G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_WINS_SERVERS,
		 g_param_spec_boxed (NM_DNS_MANAGER_WINS_SERVERS,
		                     "WINS Servers",
		                     "WINS Servers",
		                     G_TYPE_STRV,
		                     G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_SPLIT_DNS,
		 g_param_spec_boxed (NM_DNS_MANAGER_SPLIT_DNS,
		                     "Split DNS",
		                     "Split DNS configuration info",
		                     DBUS_TYPE_NM_SPLIT_DNS_CONFIG,
		                     G_PARAM_READABLE));

	nm_dbus_manager_register_exported_type (nm_dbus_manager_get (),
	                                        G_TYPE_FROM_CLASS (klass),
	                                        &dbus_glib_nm_dns_manager_object_info);
}

