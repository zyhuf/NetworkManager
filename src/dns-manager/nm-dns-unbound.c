/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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
 * Copyright (C) 2014 Red Hat, Inc.
 * Author: Pavel Šimerda <psimerda@redhat.com>
 */

#include "config.h"

#include "nm-dns-unbound.h"
#include "NetworkManagerUtils.h"

G_DEFINE_TYPE (NMDnsUnbound, nm_dns_unbound, NM_TYPE_DNS_PLUGIN)

#define NM_DNS_UNBOUND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DNS_UNBOUND, NMDnsUnboundPrivate))

typedef struct {
	GPid results_pid;
	int results_fd;
	guint results_id;
	GIOChannel *results_chan;
} NMDnsUnboundPrivate;

/*******************************************/

static void
results_cleanup (NMDnsUnbound *self)
{
	NMDnsUnboundPrivate *priv = NM_DNS_UNBOUND_GET_PRIVATE (self);

	if (priv->results_pid == 0)
		return;

	kill (priv->results_pid, SIGTERM);
	g_spawn_close_pid (priv->results_pid);
	priv->results_pid = 0;

	close (priv->results_fd);

	g_io_channel_unref (priv->results_chan);
	priv->results_chan = 0;

	g_source_remove (priv->results_id);
	priv->results_id = 0;
}

static void
results_died (GPid pid,  gint status,  gpointer user_data)
{
	NMDnsUnbound *self = NM_DNS_UNBOUND (user_data);

	if (status != 0) {
		nm_log_warn (LOGD_DNS, "dns-trigger-control: PID %d died with status %d",
		             pid, status);
	}

	results_cleanup (self);
}

static gboolean
results_in (GIOChannel *chan, GIOCondition cond, gpointer user_data)
{
	NMDnsPlugin *plugin = NM_DNS_PLUGIN (user_data);
	GError *error = NULL;
	gchar *str;

	g_io_channel_read_line (chan, &str, NULL, NULL, &error);
	if (error) {
		nm_log_warn (LOGD_DNS, "dns-trigger-control: error reading results: '%s'",
		             error->message);
		g_error_free (error);
	}
	if (str) {
		nm_log_dbg (LOGD_DNS, "dns-trigger-control: got results '%s'", str);
		if (g_str_has_prefix (str, "state:")) {
			if (g_str_has_prefix (str, "state: auth secure"))
				nm_dns_plugin_set_dnssec_level (plugin, NM_DNSSEC_AUTH_SECURE);
			else if (g_str_has_prefix (str, "state: cache secure"))
				nm_dns_plugin_set_dnssec_level (plugin, NM_DNSSEC_CACHE_SECURE);
			else if (g_str_has_prefix (str, "state: disconnected secure"))
				nm_dns_plugin_set_dnssec_level (plugin, NM_DNSSEC_DISCONNECTED_SECURE);
			else
				nm_dns_plugin_set_dnssec_level (plugin, NM_DNSSEC_NOT_SECURE);
		}
		g_free (str);
	}

	return TRUE;
}

static void
results_watch (NMDnsUnbound *self)
{
	NMDnsUnboundPrivate *priv = NM_DNS_UNBOUND_GET_PRIVATE (self);
	char *argv[3];
	GError *error = NULL;

	if (priv->results_pid)
		return;

	argv[0] = DNSSEC_TRIGGER_CONTROL;
	argv[1] = "results";
	argv[2] = NULL;

	if (!g_spawn_async_with_pipes (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL,
	                               self, &priv->results_pid, NULL, &priv->results_fd,
	                               NULL, &error)) {
		nm_log_warn (LOGD_DNS, "dns-trigger-control: could not spawn: '%s'",
		             error->message);
		return;
	}

	nm_log_warn (LOGD_DNS, "dns-trigger-control: running with PID %d\n", priv->results_pid);

	priv->results_chan = g_io_channel_unix_new (priv->results_fd);
	g_io_add_watch (priv->results_chan, G_IO_IN, results_in, self);
	priv->results_id = g_child_watch_add (priv->results_pid, results_died, self);
}

static gboolean
update (NMDnsPlugin *self,
        const GSList *vpn_configs,
        const GSList *dev_configs,
        const GSList *other_configs,
        const NMGlobalDnsConfig *global_config,
        const char *hostname,
        char **nameservers)
{
	gint status;
	gchar *ns, *cmdline;

	results_watch (NM_DNS_UNBOUND (self));

	ns = g_strjoinv (" ", nameservers);
	nm_log_dbg (LOGD_DNS, "dns-trigger-control: using nameservers %s\n", ns);
	cmdline = g_strdup_printf ("%s submit %s", DNSSEC_TRIGGER_CONTROL, ns);
	g_free (ns);
	status = nm_spawn_process (cmdline, NULL);
	g_free (cmdline);

	return status == 0;
}

static gboolean
recheck (NMDnsPlugin *self)
{
	results_watch (NM_DNS_UNBOUND (self));

	nm_log_dbg (LOGD_DNS, "dns-trigger-control: rechecking\n");
	return nm_spawn_process (DNSSEC_TRIGGER_CONTROL " reprobe", NULL) == 0;
}

static gboolean
is_caching (NMDnsPlugin *plugin)
{
	return TRUE;
}

static const char *
get_name (NMDnsPlugin *plugin)
{
	return "unbound";
}

/****************************************************************/

NMDnsPlugin *
nm_dns_unbound_new (void)
{
	return g_object_new (NM_TYPE_DNS_UNBOUND, NULL);
}

static void
nm_dns_unbound_init (NMDnsUnbound *unbound)
{
}

static void
dispose (GObject *object)
{
	results_cleanup (NM_DNS_UNBOUND (object));

	G_OBJECT_CLASS (nm_dns_unbound_parent_class)->dispose (object);
}

static void
nm_dns_unbound_class_init (NMDnsUnboundClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDnsPluginClass *plugin_class = NM_DNS_PLUGIN_CLASS (klass);

	object_class->dispose = dispose;

	g_type_class_add_private (plugin_class, sizeof (NMDnsUnboundPrivate));
	plugin_class->update = update;
	plugin_class->recheck = recheck;
	plugin_class->is_caching = is_caching;
	plugin_class->get_name = get_name;
}
