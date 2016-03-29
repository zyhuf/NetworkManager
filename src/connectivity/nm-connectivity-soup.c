/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
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
 * Copyright (C) 2016 Red Hat, Inc.
 *
 */

#include "nm-connectivity-soup.h"
#include "../nm-connectivity.h"
#include "../nm-connectivity-priv.h"
#include "../nm-config.h"

typedef struct {
	GSimpleAsyncResult *simple;
	char *uri;
	char *response;
	guint check_id_when_scheduled;
} ConCheckCbData;


static void
nm_connectivity_check_cb (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
	NMConnectivity *self;
	NMConnectivityPrivate *priv;
	ConCheckCbData *cb_data = user_data;
	GSimpleAsyncResult *simple = cb_data->simple;
	NMConnectivityState new_state;
	const char *nm_header;
	const char *uri = cb_data->uri;
	const char *response = cb_data->response ? cb_data->response : NM_CONFIG_DEFAULT_CONNECTIVITY_RESPONSE;

	self = NM_CONNECTIVITY (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
	/* it is safe to unref @self here, @simple holds yet another reference. */
	g_object_unref (self);
	priv = NM_CONNECTIVITY_GET_PRIVATE (self);

	if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code)) {
		_LOGI ("check for uri '%s' failed with '%s'", uri, msg->reason_phrase);
		new_state = NM_CONNECTIVITY_LIMITED;
		goto done;
	}

	if (msg->status_code == 511) {
		_LOGD ("check for uri '%s' returned status '%d %s'; captive portal present.",
		       uri, msg->status_code, msg->reason_phrase);
		new_state = NM_CONNECTIVITY_PORTAL;
	} else {
		/* Check headers; if we find the NM-specific one we're done */
		nm_header = soup_message_headers_get_one (msg->response_headers, "X-NetworkManager-Status");
		if (g_strcmp0 (nm_header, "online") == 0) {
			_LOGD ("check for uri '%s' with Status header successful.", uri);
			new_state = NM_CONNECTIVITY_FULL;
		} else if (msg->status_code == SOUP_STATUS_OK) {
			/* check response */
			if (msg->response_body->data && g_str_has_prefix (msg->response_body->data, response)) {
				_LOGD ("check for uri '%s' successful.", uri);
				new_state = NM_CONNECTIVITY_FULL;
			} else {
				_LOGI ("check for uri '%s' did not match expected response '%s'; assuming captive portal.",
					   uri, response);
				new_state = NM_CONNECTIVITY_PORTAL;
			}
		} else {
			_LOGI ("check for uri '%s' returned status '%d %s'; assuming captive portal.",
			       uri, msg->status_code, msg->reason_phrase);
			new_state = NM_CONNECTIVITY_PORTAL;
		}
	}

 done:
	/* Only update the state, if the call was done from external, or if the periodic check
	 * is still the one that called this async check. */
	if (!cb_data->check_id_when_scheduled || cb_data->check_id_when_scheduled == priv->concheck.check_id) {
		/* Only update the state, if the URI and response parameters did not change
		 * since invocation.
		 * The interval does not matter for exernal calls, and for internal calls
		 * we don't reach this line if the interval changed. */
		if (   !g_strcmp0 (cb_data->uri, priv->uri)
		    && !g_strcmp0 (cb_data->response, priv->response))
			update_state (self, new_state);
	}

	g_simple_async_result_set_op_res_gssize (simple, new_state);
	g_simple_async_result_complete (simple);
	g_object_unref (simple);

	g_free (cb_data->uri);
	g_free (cb_data->response);
	g_slice_free (ConCheckCbData, cb_data);
}


gboolean
nm_connectivity_do_check (char                   *uri,
                          char                   *response,
                          guint                   interval,
                          GSimpleAsyncResult     *simple,
                          NMConnectivityConcheck *concheck,
                          gboolean                periodic_check)
{
	if (uri && interval) {
		SoupMessage *msg;
		ConCheckCbData *cb_data = g_slice_new (ConCheckCbData);

		msg = soup_message_new ("GET", uri);
		soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
		/* Disable HTTP/1.1 keepalive; the connection should not persist */
		soup_message_headers_append (msg->request_headers, "Connection", "close");
		cb_data->simple = simple;
		cb_data->uri = g_strdup (uri);
		cb_data->response = g_strdup (response);

		/* For internal calls (periodic), remember the check-id at time of scheduling. */
		cb_data->check_id_when_scheduled = periodic_check ? concheck->check_id : 0;

		soup_session_queue_message (concheck->soup_session,
		                            msg,
		                            nm_connectivity_check_cb,
		                            cb_data);
		concheck->initial_check_obsoleted = TRUE;

		_LOGD ("check: send %srequest to '%s'", periodic_check ? "periodic " : "", uri);
		return TRUE;
	} else {
		g_warn_if_fail (!periodic_check);
		_LOGD ("check: faking request. Connectivity check disabled");
		return FALSE;
	}
}

gboolean
nm_connectivity_lib_is_uri_valid (const char *uri, gboolean changed)
{
	SoupURI *soup_uri = soup_uri_new (uri);
	gboolean is_valid = TRUE;

	if (!soup_uri || !SOUP_URI_VALID_FOR_HTTP (soup_uri)) {
		_LOGE ("invalid uri '%s' for connectivity check.", uri);
		is_valid = FALSE;
	} else if (changed && soup_uri_get_scheme(soup_uri) == SOUP_URI_SCHEME_HTTPS) {
		_LOGW ("use of HTTPS for connectivity checking is not reliable and is discouraged (URI: %s)", uri);
	}
	if (soup_uri)
		soup_uri_free (soup_uri);
	return is_valid;
}

void
nm_connectivity_lib_init (NMConnectivityConcheck *concheck, guint timeout)
{
	concheck->soup_session = soup_session_async_new_with_options (SOUP_SESSION_TIMEOUT, timeout, NULL);
}

void
nm_connectivity_lib_dispose (NMConnectivityConcheck *concheck)
{
	if (concheck->soup_session) {
		soup_session_abort (concheck->soup_session);
		g_clear_object (&concheck->soup_session);
	}

	nm_clear_g_source (&concheck->check_id);
}


