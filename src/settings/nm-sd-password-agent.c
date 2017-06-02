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
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include <sys/un.h>

#include "nm-core-utils.h"
#include "nm-sd-password-agent.h"
#include "nm-auth-subject.h"

/*****************************************************************************/

typedef struct {
	NMAuthSubject *subject;
} NMSdPasswordAgentPrivate;

struct _NMSdPasswordAgent {
	GObject parent;
	NMSdPasswordAgentPrivate _priv;
};

struct _NMSdPasswordAgentClass {
	GObjectClass parent;
};

static void nm_agent_interface_init (NMAgentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NMSdPasswordAgent, nm_sd_password_agent, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NM_TYPE_AGENT, nm_agent_interface_init))

#define NM_SD_PASSWORD_AGENT_GET_PRIVATE(self) _NM_GET_PRIVATE (self, NMSdPasswordAgent, NM_IS_SD_PASSWORD_AGENT)

/*****************************************************************************/

#define _NMLOG_PREFIX_NAME    "sd-password-agent"
#define _NMLOG_DOMAIN         LOGD_AGENTS
#define _NMLOG(level, ...) \
    G_STMT_START { \
        if (nm_logging_enabled ((level), (_NMLOG_DOMAIN))) { \
            char __prefix[32]; \
            \
            if ((self)) \
                g_snprintf (__prefix, sizeof (__prefix), "%s[%p]", ""_NMLOG_PREFIX_NAME"", (self)); \
            else \
                g_strlcpy (__prefix, _NMLOG_PREFIX_NAME, sizeof (__prefix)); \
            _nm_log ((level), (_NMLOG_DOMAIN), 0, NULL, NULL, \
                     "%s: " _NM_UTILS_MACRO_FIRST(__VA_ARGS__), \
                     __prefix _NM_UTILS_MACRO_REST(__VA_ARGS__)); \
        } \
    } G_STMT_END

#define LOG_REQ_FMT          "req[%p,%s,%s%s%s%s]"

/*****************************************************************************/

struct _NMAgentCallId {
	NMAgent *agent;
	NMAgentCallback callback;
	gpointer user_data;
	const char *setting_name;
	const char *property_name;
	char *filename;
	char *sockname;
	GIOChannel *sockchan;
	guint timeout_id;
	guint event_id;
};

typedef struct _NMAgentCallId Request;

/*****************************************************************************/

static void
request_finish (Request *req, GVariant *secrets, GError *error)
{
	if (req->filename) {
		unlink (req->filename);
		g_free (req->filename);
	}
	if (req->sockname) {
		unlink (req->sockname);
		g_free (req->sockname);
	}
	if (req->sockchan)
		g_io_channel_unref (req->sockchan);
	if (req->event_id) {
		g_source_remove (req->event_id);
		req->event_id = 0;
	}
	if (req->timeout_id) {
		g_source_remove (req->timeout_id);
		req->timeout_id = 0;
	}

	req->callback (req->agent, req, secrets, error, req->user_data);
	g_slice_free (Request, req);
}

static gboolean
socket_event (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
	Request *req = user_data;
	char buf[512] = { 0 };
	int rb;
	GError *error = NULL;
	GVariant *secrets = NULL;

	if (condition & G_IO_IN) {
		rb = recv (g_io_channel_unix_get_fd (source), buf, sizeof (buf) - 1, MSG_DONTWAIT);
		if (buf[0] == '+') {
			secrets = g_variant_new_parsed ("[{%s, [{%s, <%s>}]}]",
			                                req->setting_name,
			                                req->property_name,
			                                &buf[1]);
		} else {
			g_set_error (&error,
			             NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_AGENT_CANCELED,
			             "Password request cancelled");
		}
	} else {
		g_set_error (&error,
		             NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_FAILED,
		             "Unexpected condition on password socket");
	}

	request_finish (req, secrets, error);

	return FALSE;
}

static gboolean
request_timeout (gpointer user_data)
{
	Request *req = user_data;
	GError *error = NULL;;

	g_set_error (&error,
	             NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_NO_SECRETS,
	             "Timeout");
	request_finish (req, NULL, error);

	return G_SOURCE_REMOVE;
}

static void
request_start (Request *req, int secs)
{
	gchar *tmpname;
	gchar *content;
	int fd, sockfd;
	int retries = 100;
	struct sockaddr_un addr = { 0 };
	struct timespec tp = { 0, };
	int errsv;
	GError *error = NULL;

	req->sockname = g_strdup_printf (NMRUNDIR "/systemd-ask-pass.%p", req);

	addr.sun_family = AF_UNIX;
	strncpy (addr.sun_path, req->sockname, sizeof (addr.sun_path) - 1);
	sockfd = socket (AF_UNIX, SOCK_DGRAM, 0);
	if (sockfd == -1) {
		errsv = errno;
		g_set_error (&error,
		             G_IO_ERROR, g_io_error_from_errno (errsv),
		             "Can not open the password socket: %s", g_strerror (errsv));
		goto error;
	}
	if (bind (sockfd, (const struct sockaddr *)&addr, sizeof (addr)) == -1) {
		errsv = errno;
		g_set_error (&error,
		             G_IO_ERROR, g_io_error_from_errno (errsv),
		             "Can not bind the password socket: %s", g_strerror (errsv));
		goto error;
	}

	if (clock_gettime (CLOCK_MONOTONIC, &tp) == 0)
		tp.tv_sec += secs;

	do {
		tmpname = g_strdup (RUNSTATEDIR "/systemd/ask-password/tmp.XXXXXX");

		fd = g_mkstemp (tmpname);
		if (fd == -1) {
			errsv = errno;
			g_set_error (&error,
			             G_IO_ERROR, g_io_error_from_errno (errsv),
			             "Can not create a temporary file: %s", g_strerror (errsv));
			goto error;
		}

		content = g_strdup_printf ("[Ask]\n"
		                           "PID=%d\n"
		                           "Socket=%s\n"
		                           "AcceptCached=0\n"
		                           "Echo=0\n"
		                           "NotAfter=%ld%ld\n"
		                           "Message=%s.%s\n",
		                           getpid (), req->sockname, tp.tv_sec, tp.tv_nsec / 1000,
		                           req->setting_name, req->property_name);

		nm_utils_fd_set_contents (fd, content, strlen (content), &error);
		close (fd);
		g_free (content);
		if (error)
			g_prefix_error (&error, "Can not write %s: ", tmpname);

		req->filename = g_strdup (tmpname);
		memcpy (&req->filename[sizeof (RUNSTATEDIR "/systemd/ask-password/") - 1], "ask", 3);
		if (link (tmpname, req->filename) == -1) {
			errsv = errno;
			if (errsv != EEXIST) {
				g_set_error (&error,
				             G_IO_ERROR, g_io_error_from_errno (errsv),
				             "Can not create the password request file %s: %s",
				             req->filename,
				             g_strerror (errsv));
				retries = 0;
			}
			g_clear_pointer (&req->filename, g_free);
		} else {
			retries = 0;
		}
		unlink (tmpname);
		g_clear_pointer (&tmpname, g_free);
	} while (retries--);

	if (!req->filename) {
		if (!error) {
			g_set_error (&error,
			             NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_FAILED,
			             "Could not create a password request");
		}
		goto error;
	}

	if (secs)
		req->timeout_id = g_timeout_add_seconds (secs, request_timeout, req);
	req->sockchan = g_io_channel_unix_new (sockfd);
	g_io_channel_set_close_on_unref  (req->sockchan, TRUE);
	req->event_id = g_io_add_watch (req->sockchan,
	                                G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
	                                socket_event, req);
	return;
error:
	close (sockfd);
	request_finish (req, NULL, error);
}

/*****************************************************************************/

static const char *
nm_sd_password_agent_get_description (NMAgent *self)
{
	return "systemd-ask-password agent";
}

static const char *
nm_sd_password_agent_get_dbus_owner (NMAgent *self)
{
	return "";
}

static const char *
nm_sd_password_agent_get_identifier (NMAgent *self)
{
	return "systemd-ask-password";
}

static gulong
nm_sd_password_agent_get_owner_uid  (NMAgent *self)
{
	return 0;
}

static const char *
nm_sd_password_agent_get_owner_username (NMAgent *self)
{
	return "root";
}

static gulong
nm_sd_password_agent_get_pid (NMAgent *self)
{
	return 1;
}

static NMSecretAgentCapabilities
nm_sd_password_agent_get_capabilities (NMAgent *self)
{
	return NM_SECRET_AGENT_CAPABILITY_NONE;
}

static NMAuthSubject *
nm_sd_password_agent_get_subject (NMAgent *self)
{
	NMSdPasswordAgentPrivate *priv = NM_SD_PASSWORD_AGENT_GET_PRIVATE (NM_SD_PASSWORD_AGENT (self));

	return priv->subject;
}

static void
_get_wanted_property (NMSetting *setting, const char *key, const GValue *value, GParamFlags flags, gpointer user_data)
{
	const char **property_name = user_data;
	NMSettingSecretFlags secret_flags;

	if (    nm_setting_get_secret_flags (setting, key, &secret_flags, NULL)
	    && secret_flags == NM_SETTING_SECRET_FLAG_NOT_SAVED)
		*property_name = key;
}


static NMAgentCallId
nm_sd_password_agent_get_secrets (NMAgent *self,
                             const char *path,
                             NMConnection *connection,
                             const char *setting_name,
                             const char **hints,
                             NMSecretAgentGetSecretsFlags flags,
                             NMAgentCallback callback,
                             gpointer callback_data)
{
	NMSetting *setting;
	Request *req;
	const char *property_name = NULL;

	setting = nm_connection_get_setting_by_name (connection, setting_name);
	nm_setting_enumerate_values (setting, _get_wanted_property, &property_name);

	if (!property_name)
		return NULL;

	req = g_slice_new0 (Request);
	req->agent = self;
	req->callback = callback;
	req->user_data = callback_data;
	req->setting_name = setting_name;
	req->property_name = property_name;

	request_start (req, 10);
		
	return req;
}

static void
nm_sd_password_agent_cancel_secrets (NMAgent *self, NMAgentCallId req)
{
	GError *error = NULL;

	g_set_error (&error,
	             NM_SECRET_AGENT_ERROR, NM_SECRET_AGENT_ERROR_AGENT_CANCELED,
	             "Password request cancelled");
	request_finish (req, NULL, error);
}

/*****************************************************************************/

NMSdPasswordAgent *
nm_sd_password_agent_new ()
{
	return (NMSdPasswordAgent *) g_object_new (NM_TYPE_SD_PASSWORD_AGENT, NULL);
}

static void
nm_agent_interface_init (NMAgentInterface *iface)
{
	iface->get_description = nm_sd_password_agent_get_description;
	iface->get_dbus_owner = nm_sd_password_agent_get_dbus_owner;
	iface->get_identifier = nm_sd_password_agent_get_identifier;
	iface->get_owner_uid = nm_sd_password_agent_get_owner_uid;
	iface->get_owner_username = nm_sd_password_agent_get_owner_username;
	iface->get_pid = nm_sd_password_agent_get_pid;
	iface->get_capabilities = nm_sd_password_agent_get_capabilities;
	iface->get_subject = nm_sd_password_agent_get_subject;
	iface->get_secrets = nm_sd_password_agent_get_secrets;
	iface->cancel_secrets = nm_sd_password_agent_cancel_secrets;
}

static void
nm_sd_password_agent_init (NMSdPasswordAgent *self)
{
	NMSdPasswordAgentPrivate *priv = NM_SD_PASSWORD_AGENT_GET_PRIVATE (self);

	priv->subject = nm_auth_subject_new_internal ();
}

static void
dispose (GObject *object)
{
	NMSdPasswordAgent *self = NM_SD_PASSWORD_AGENT (object);
	NMSdPasswordAgentPrivate *priv = NM_SD_PASSWORD_AGENT_GET_PRIVATE (self);

	g_clear_object (&priv->subject);

	G_OBJECT_CLASS (nm_sd_password_agent_parent_class)->dispose (object);
}

static void
nm_sd_password_agent_class_init (NMSdPasswordAgentClass *config_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (config_class);

	object_class->dispose = dispose;
}
