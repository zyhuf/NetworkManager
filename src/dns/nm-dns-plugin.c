/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* This program is free software; you can redistribute it and/or modify
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
 * Copyright (C) 2010 - 2012 Red Hat, Inc.
 *
 */

#include "nm-default.h"

#include "nm-dns-plugin.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "nm-core-internal.h"
#include "NetworkManagerUtils.h"

/*****************************************************************************/

#define PLUGIN_RATELIMIT_INTERVAL    30
#define PLUGIN_RATELIMIT_BURST       5
#define PLUGIN_RATELIMIT_DELAY       300

NM_GOBJECT_PROPERTIES_DEFINE (NMDnsPlugin,
	PROP_STATE,
);

typedef struct _NMDnsPluginPrivate {
	GPid pid;
	guint watch_id;
	char *progname;
	char *pidfile;
	NMDnsPluginState state;

	struct {
		guint64 ts;
		guint num_restarts;
		guint timer;
	} plugin_ratelimit;
} NMDnsPluginPrivate;

G_DEFINE_TYPE_EXTENDED (NMDnsPlugin, nm_dns_plugin, G_TYPE_OBJECT, G_TYPE_FLAG_ABSTRACT, {})

#define NM_DNS_PLUGIN_GET_PRIVATE(self) _NM_GET_PRIVATE_PTR (self, NMDnsPlugin, NM_IS_DNS_PLUGIN)

/*****************************************************************************/

#define _NMLOG_PREFIX_NAME                "dns-plugin"
#define _NMLOG_DOMAIN                     LOGD_DNS
#define _NMLOG(level, ...) \
    G_STMT_START { \
        const NMLogLevel __level = (level); \
        \
        if (nm_logging_enabled (__level, _NMLOG_DOMAIN)) { \
            char __prefix[20]; \
            const NMDnsPlugin *const __self = (self); \
            \
            _nm_log (__level, _NMLOG_DOMAIN, 0, NULL, NULL, \
                     "%s%s: " _NM_UTILS_MACRO_FIRST (__VA_ARGS__), \
                     _NMLOG_PREFIX_NAME, \
                     (!__self \
                        ? "" \
                        : nm_sprintf_buf (__prefix, "[%p]", __self)) \
                     _NM_UTILS_MACRO_REST (__VA_ARGS__)); \
        } \
    } G_STMT_END

/*****************************************************************************/

gboolean
nm_dns_plugin_update (NMDnsPlugin *self,
                      const GPtrArray *configs,
                      const NMGlobalDnsConfig *global_config,
                      const char *hostname)
{
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	g_return_val_if_fail (NM_DNS_PLUGIN_GET_CLASS (self)->update != NULL, FALSE);

	nm_clear_g_source (&priv->plugin_ratelimit.timer);

	return NM_DNS_PLUGIN_GET_CLASS (self)->update (self,
	                                               configs,
	                                               global_config,
	                                               hostname);
}

const char *
nm_dns_plugin_get_name (NMDnsPlugin *self)
{
	g_assert (NM_DNS_PLUGIN_GET_CLASS (self)->get_name);
	return NM_DNS_PLUGIN_GET_CLASS (self)->get_name (self);
}

/*****************************************************************************/

static void
_clear_pidfile (NMDnsPlugin *self)
{
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	if (priv->pidfile) {
		unlink (priv->pidfile);
		g_clear_pointer (&priv->pidfile, g_free);
	}
}

static void
kill_existing (const char *progname, const char *pidfile, const char *kill_match)
{
	glong pid;
	gs_free char *contents = NULL;
	gs_free char *cmdline_contents = NULL;
	guint64 start_time;
	char proc_path[256];
	gs_free_error GError *error = NULL;

	if (!pidfile)
		return;

	if (!kill_match)
		g_return_if_reached ();

	if (!g_file_get_contents (pidfile, &contents, NULL, &error)) {
		if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			return;
		goto out;
	}

	pid = _nm_utils_ascii_str_to_int64 (contents, 10, 2, INT_MAX, -1);
	if (pid == -1)
		goto out;

	start_time = nm_utils_get_start_time_for_pid (pid, NULL, NULL);
	if (start_time == 0)
		goto out;

	nm_sprintf_buf (proc_path, "/proc/%ld/cmdline", pid);
	if (!g_file_get_contents (proc_path, &cmdline_contents, NULL, NULL))
		goto out;

	if (!strstr (cmdline_contents, kill_match))
		goto out;

	nm_utils_kill_process_sync (pid, start_time, SIGKILL, _NMLOG_DOMAIN,
	                            progname ?: "<dns-process>",
	                            0, 0, 1000);

out:
	unlink (pidfile);
}

static gboolean
emit_ratelimited_child_quit (gpointer user_data)
{
	NMDnsPlugin *self = NM_DNS_PLUGIN (user_data);

	nm_dns_plugin_set_state (self, NM_DNS_PLUGIN_STATE_STOPPED);

	return G_SOURCE_REMOVE;
}

static void
watch_cb (GPid pid, gint status, gpointer user_data)
{
	NMDnsPlugin *self = NM_DNS_PLUGIN (user_data);
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);
	NMDnsPluginClass *klass = NM_DNS_PLUGIN_GET_CLASS (self);
	gint64 ts = nm_utils_get_monotonic_timestamp_ms ();
	gboolean failed = FALSE;

	priv->pid = 0;
	priv->watch_id = 0;
	g_clear_pointer (&priv->progname, g_free);
	_clear_pidfile (self);

	if (klass->child_quit)
		failed = klass->child_quit (self, status);

	if (   !priv->plugin_ratelimit.ts
	    || (ts - priv->plugin_ratelimit.ts) / 1000 > PLUGIN_RATELIMIT_INTERVAL
	    || failed) {
	        priv->plugin_ratelimit.ts = ts;
	        priv->plugin_ratelimit.num_restarts = 0;
	} else {
		priv->plugin_ratelimit.num_restarts++;
		if (priv->plugin_ratelimit.num_restarts > PLUGIN_RATELIMIT_BURST) {
			_LOGW ("plugin %s child respawning too fast, delaying update for %u seconds",
			       nm_dns_plugin_get_name (self), PLUGIN_RATELIMIT_DELAY);
			priv->plugin_ratelimit.timer = g_timeout_add_seconds (PLUGIN_RATELIMIT_DELAY,
			                                                      emit_ratelimited_child_quit,
			                                                      self);
			return;
		}
	}

	if (failed)
		nm_dns_plugin_set_state (self, NM_DNS_PLUGIN_STATE_FAILED);
	else
		nm_dns_plugin_set_state (self, NM_DNS_PLUGIN_STATE_STOPPED);
}

/*****************************************************************************/

GPid
nm_dns_plugin_child_pid (NMDnsPlugin *self)
{
	NMDnsPluginPrivate *priv;

	g_return_val_if_fail (NM_IS_DNS_PLUGIN (self), 0);

	priv = NM_DNS_PLUGIN_GET_PRIVATE (self);
	return priv->pid;
}

GPid
nm_dns_plugin_child_spawn (NMDnsPlugin *self,
                           const char **argv,
                           const char *pidfile,
                           const char *kill_match)
{
	NMDnsPluginPrivate *priv;
	GError *error = NULL;
	GPid pid;
	gs_free char *cmdline = NULL;
	gs_free char *progname = NULL;

	g_return_val_if_fail (argv && argv[0], 0);
	g_return_val_if_fail (NM_IS_DNS_PLUGIN (self), 0);

	priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	g_return_val_if_fail (!priv->pid, 0);
	nm_assert (!priv->progname);
	nm_assert (!priv->watch_id);
	nm_assert (!priv->pidfile);

	progname = g_path_get_basename (argv[0]);
	kill_existing (progname, pidfile, kill_match);

	_LOGI ("starting %s...", progname);
	_LOGD ("command line: %s",
	       (cmdline = g_strjoinv (" ", (char **) argv)));

	if (!g_spawn_async (NULL, (char **) argv, NULL,
	                   G_SPAWN_DO_NOT_REAP_CHILD,
	                   nm_utils_setpgid, NULL,
	                   &pid,
	                   &error)) {
		_LOGW ("failed to spawn %s: %s",
		       progname, error->message);
		g_clear_error (&error);
		return 0;
	}

	_LOGD ("%s started with pid %d", progname, pid);
	priv->watch_id = g_child_watch_add (pid, (GChildWatchFunc) watch_cb, self);
	priv->pid = pid;
	priv->progname = g_steal_pointer (&progname);
	priv->pidfile = g_strdup (pidfile);

	return pid;
}

gboolean
nm_dns_plugin_child_kill (NMDnsPlugin *self)
{
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	priv->plugin_ratelimit.ts = 0;
	nm_clear_g_source (&priv->plugin_ratelimit.timer);
	nm_clear_g_source (&priv->watch_id);
	if (priv->pid) {
		nm_utils_kill_child_sync (priv->pid, SIGTERM, _NMLOG_DOMAIN,
		                          priv->progname ?: "<dns-process>", NULL, 1000, 0);
		priv->pid = 0;
		g_clear_pointer (&priv->progname, g_free);
	}
	_clear_pidfile (self);

	return TRUE;
}

void
nm_dns_plugin_stop (NMDnsPlugin *self)
{
	nm_dns_plugin_child_kill (self);
}

/*****************************************************************************/

NMDnsPluginState
nm_dns_plugin_get_state (NMDnsPlugin *self)
{
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	return priv->state;
}

void
nm_dns_plugin_set_state (NMDnsPlugin *self, NMDnsPluginState state)
{
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	if (priv->state == state)
		return;

	priv->state = state;
	_notify (self, PROP_STATE);
}

/*****************************************************************************/

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDnsPlugin *self = NM_DNS_PLUGIN (object);
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
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
	NMDnsPlugin *self = NM_DNS_PLUGIN (object);
	NMDnsPluginPrivate *priv = NM_DNS_PLUGIN_GET_PRIVATE (self);

	switch (prop_id) {
	case PROP_STATE:
		priv->state = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*****************************************************************************/

static void
nm_dns_plugin_init (NMDnsPlugin *self)
{
	self->_priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NM_TYPE_DNS_PLUGIN, NMDnsPluginPrivate);
}

static void
dispose (GObject *object)
{
	NMDnsPlugin *self = NM_DNS_PLUGIN (object);

	nm_dns_plugin_stop (self);

	G_OBJECT_CLASS (nm_dns_plugin_parent_class)->dispose (object);
}

static void
nm_dns_plugin_class_init (NMDnsPluginClass *plugin_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (plugin_class);

	g_type_class_add_private (plugin_class, sizeof (NMDnsPluginPrivate));

	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	obj_properties[PROP_STATE] =
	    g_param_spec_uint (NM_DNS_PLUGIN_STATE, "", "",
	                       NM_DNS_PLUGIN_STATE_STOPPED,
	                       NM_DNS_PLUGIN_STATE_FAILED,
	                       NM_DNS_PLUGIN_STATE_STOPPED,
	                       G_PARAM_READABLE
	                         | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, _PROPERTY_ENUMS_LAST, obj_properties);
}
