/* nm-dhcp-manager.c - Handle the DHCP daemon for NetworkManager
 *
 * Copyright (C) 2005 Dan Williams
 *
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */


#include <glib.h>
#include <dbus/dbus.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

#include "nm-dhcp-manager.h"
#include "nm-dhcp-marshal.h"
#include "nm-utils.h"
#include "NetworkManagerUtils.h"


#define NM_DHCP_TIMEOUT   	45 /* DHCP timeout, in seconds */

static const char *dhclient_binary_paths[] =
{
	"/sbin/dhclient",
	"/usr/sbin/dhclient",
	"/usr/local/sbin/dhclient",
	NULL
};

typedef struct {
        char *			iface;
        guchar 			state;
        GPid 			dhclient_pid;
        GSource *		timeout_source;
        GSource *		cancel_source;
        GSource *		watch_source;
        NMDHCPManager *	manager;
        GHashTable *	options;
} NMDHCPDevice;

typedef struct {
	GHashTable *	devices;
	NMData *		data;
} NMDHCPManagerPrivate;


#define NM_DHCP_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DHCP_MANAGER, NMDHCPManagerPrivate))

G_DEFINE_TYPE (NMDHCPManager, nm_dhcp_manager, G_TYPE_OBJECT)

enum {
	STATE_CHANGED,
	TIMEOUT,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static NMDHCPManager *nm_dhcp_manager_new (NMData * data);

static void nm_dhcp_manager_cancel_transaction_real (NMDHCPDevice *device, gboolean blocking);

static char *
get_pidfile_for_iface (const char * iface)
{
	return g_strdup_printf ("%s/%s-%s.%s",
	                        NM_DHCP_MANAGER_PID_DIR,
	                        NM_DHCP_MANAGER_PID_FILENAME,
	                        iface,
	                        NM_DHCP_MANAGER_PID_FILE_EXT);
}


NMDHCPManager *
nm_dhcp_manager_get (NMData * data)
{
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
	static NMDHCPManager *singleton = NULL;

	g_static_mutex_lock (&mutex);
	if (!singleton) {
		if (!data) {
			nm_error ("Initial creation must pass NMData");
			g_assert_not_reached ();
		}
		singleton = nm_dhcp_manager_new (data);
	}
	g_object_ref (singleton);
	g_static_mutex_unlock (&mutex);

	return singleton;
}

static void
nm_dhcp_manager_init (NMDHCPManager *msg)
{
}

static void
finalize (GObject *object)
{
	G_OBJECT_CLASS (nm_dhcp_manager_parent_class)->finalize (object);
}

static void
nm_dhcp_manager_class_init (NMDHCPManagerClass *manager_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

	g_type_class_add_private (manager_class, sizeof (NMDHCPManagerPrivate));

	/* virtual methods */
	object_class->finalize = finalize;

	/* signals */
	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMDHCPManagerClass, state_changed),
					  NULL, NULL,
					  nm_dhcp_marshal_VOID__STRING_UCHAR,
					  G_TYPE_NONE, 2,
					  G_TYPE_STRING,
					  G_TYPE_UCHAR);

	signals[TIMEOUT] =
		g_signal_new ("timeout",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMDHCPManagerClass, timeout),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__STRING,
					  G_TYPE_NONE, 1,
					  G_TYPE_STRING);
}

static gboolean state_is_bound (guint8 state)
{
	if ((state == DHC_BOUND)
	    || (state == DHC_RENEW)
	    || (state == DHC_REBOOT)
	    || (state == DHC_REBIND)
		|| (state == DHC_START))
		return TRUE;

	return FALSE;
}


static void
nm_dhcp_device_timeout_cleanup (NMDHCPDevice * device)
{
	if (!device->timeout_source)
		return;
	g_source_destroy (device->timeout_source);
	g_source_unref (device->timeout_source);
	device->timeout_source = NULL;
}

static void
nm_dhcp_device_cancel_cleanup (NMDHCPDevice * device)
{
	if (!device->cancel_source)
		return;
fprintf (stderr, "%s: cleaning up cancel source %p\n", __func__, device->cancel_source);
	g_source_destroy (device->cancel_source);
	g_source_unref (device->cancel_source);
	device->cancel_source = NULL;
}

static void
nm_dhcp_device_watch_cleanup (NMDHCPDevice * device)
{
	if (!device->watch_source)
		return;
	g_source_destroy (device->watch_source);
	g_source_unref (device->watch_source);
	device->watch_source = NULL;
}

static void
nm_dhcp_device_destroy (NMDHCPDevice *device)
{
	nm_dhcp_device_timeout_cleanup (device);
fprintf (stderr, "%s: calling cancel_cleanup\n", __func__);
	nm_dhcp_device_cancel_cleanup (device);
	nm_dhcp_device_watch_cleanup (device);
	g_hash_table_remove_all (device->options);
	g_free (device->iface);
	g_slice_free (NMDHCPDevice, device);
}


static inline const char *
state_to_string (guint32 state)
{
	switch (state)
	{
		case DHC_PREINIT:
			return "preinit";
		case DHC_BOUND:
			return "bound";
		case DHC_RENEW:
			return "renew";
		case DHC_REBOOT:
			return "reboot";
		case DHC_REBIND:
			return "rebind";
		case DHC_STOP:
			return "stop";
		case DHC_MEDIUM:
			return "medium";
		case DHC_TIMEOUT:
			return "timeout";
		case DHC_FAIL:
			return "fail";
		case DHC_EXPIRE:
			return "expire";
		case DHC_RELEASE:
			return "release";
		case DHC_START:
			return "successfully started";
		case DHC_ABEND:
			return "abnormal exit";
		case DHC_END:
			return "normal exit";
		default:
			break;
	}
	return NULL;
}

static inline guint32
string_to_state (const char *state)
{
	if (strcmp("PREINIT", state) == 0)
		return DHC_PREINIT;
	else if (strcmp("BOUND", state) == 0)
		return DHC_BOUND;
	else if (strcmp("RENEW", state) == 0)
		return DHC_RENEW;
	else if (strcmp("REBOOT", state) == 0)
		return DHC_REBOOT;
	else if (strcmp("REBIND", state) == 0)
		return DHC_REBIND;
	else if (strcmp("STOP", state) == 0)
		return DHC_STOP;
	else if (strcmp("MEDIUM", state) == 0)
		return DHC_MEDIUM;
	else if (strcmp("TIMEOUT", state) == 0)
		return DHC_TIMEOUT;
	else if (strcmp("FAIL", state) == 0)
		return DHC_FAIL;
	else if (strcmp("EXPIRE", state) == 0)
		return DHC_EXPIRE;
	else if (strcmp("RELEASE", state) == 0)
		return DHC_RELEASE;
	else if (strcmp("START", state) == 0)
		return DHC_START;
	else if (strcmp("ABEND", state) == 0)
		return DHC_ABEND;
	else if (strcmp("END", state) == 0)
		return DHC_END;
	else
		return 255;
}


/*
 * nm_dhcp_dbus_set_state
 *
 * Set the dhcp state for the interface (and store the data that dhclient gave us)
 *
 */
static gboolean
nm_dhcp_dbus_set_state (NMDHCPManager *manager,
                        DBusMessage *message)
{
	NMDHCPManagerPrivate *	priv;
	NMDHCPDevice *		device;
	const char *		data = NULL;
	DBusError		error;
	gboolean		success = FALSE;
	char *			iface;
	gchar **		keys;
	GKeyFile *		keyfile = NULL;
	GError *		gerror = NULL;
	gsize			length;
	int				i;

	g_return_val_if_fail (manager != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	dbus_error_init (&error);
	if (!dbus_message_get_args (message, &error,
				    DBUS_TYPE_STRING, &data,
				    DBUS_TYPE_INVALID))
	{
		nm_warning ("Could not process the request because its arguments were "
		            " invalid.  dbus said: '%s'",
		            error.message);
		dbus_error_free (&error);
		goto out;
	}

	keyfile = g_key_file_new ();
	if (!g_key_file_load_from_data (keyfile,
	                                (gchar *) data,
	                                strlen (data),
	                                0,
	                                &gerror)) {
		nm_warning ("Unable to parse data from dhclient: '%s'", gerror->message);
		g_error_free (gerror);
		goto out;
	}

	/* Grab the device, if it exists... and set options for it (since as of
	 * now all interfaces are through the same dbus object)
	 */
	iface = (char *) g_key_file_get_string (keyfile, "dhclient", "interface", NULL);
	if (!iface) {
		nm_warning ("Couldn't get dhclient interface from options.");
		goto out;
	}

	device = (NMDHCPDevice *) g_hash_table_lookup (priv->devices, iface);
	if (!device) {
		nm_warning ("Interface %s not registered for DHCP", iface);
		goto out;
	}

	nm_dhcp_device_timeout_cleanup (device);

	keys = g_key_file_get_keys (keyfile, "dhclient", &length, NULL);
	if (!keys) {
		nm_warning ("Not enough memory for parsing dhclient options.");
		goto out;
	}

	for (i = 0; i < length; i++) {
		char * key, * value;

		key = g_strdup (keys[i]);
		if (!key) {
			nm_warning ("Not enough memory to parse dhclient option %s", keys[i]);
			goto done_parse;
		}

		value = g_strdup (g_key_file_get_string (keyfile, "dhclient", key, NULL));
		if (!key) {
			nm_warning ("Not enough memory to parse dhclient option %s value.", keys[i]);
			goto done_parse;
		}

		g_hash_table_insert (device->options, key, value);
		if (strcmp (keys[i], "reason") == 0) {
			guint32 old_state = device->state;
			device->state = string_to_state (value);
			nm_info ("DHCP: device %s state changed %s -> %s\n",
			         device->iface,
			         state_to_string (old_state),
			         state_to_string (device->state));
		}
	}

done_parse:
	g_strfreev (keys);

	g_signal_emit (G_OBJECT (device->manager), signals[STATE_CHANGED], 0, device->iface, device->state);
	success = TRUE;

out:
	if (keyfile)
		g_key_file_free (keyfile);
	return success;
}


gboolean
nm_dhcp_manager_process_signal (NMDHCPManager * manager,
                                DBusMessage *message)
{
	const char * method;
	const char * path;

	g_return_val_if_fail (message != NULL, FALSE);

	method = dbus_message_get_member (message);
	path = dbus_message_get_path (message);

	if (strcmp ("Event", method) || strcmp (path, "/"))
		return FALSE;

	nm_dhcp_dbus_set_state (manager, message);
	return TRUE;
}


static NMDHCPManager *
nm_dhcp_manager_new (NMData * data)
{
	NMDHCPManager *manager;
	NMDHCPManagerPrivate *priv;

	manager = g_object_new (NM_TYPE_DHCP_MANAGER, NULL);
	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
										   NULL,
										   (GDestroyNotify) nm_dhcp_device_destroy);
	priv->data = data;

	return manager;
}


/*
 * nm_dhcp_manager_handle_timeout
 *
 * Called after timeout of a DHCP transaction to notify device of the failure.
 *
 */
static gboolean
nm_dhcp_manager_handle_timeout (gpointer user_data)
{
	NMDHCPDevice *device = (NMDHCPDevice *) user_data;

	nm_info ("Device '%s' DHCP transaction took too long (>%ds), stopping it.",
			 device->iface, NM_DHCP_TIMEOUT);

	g_signal_emit (G_OBJECT (device->manager), signals[TIMEOUT], 0, device->iface);

	nm_dhcp_manager_cancel_transaction (device->manager, device->iface, FALSE);

	return FALSE;
}

static NMDHCPDevice *
nm_dhcp_device_new (NMDHCPManager *manager, const char *iface)
{
	NMDHCPDevice *device;
	GHashTable * hash = NM_DHCP_MANAGER_GET_PRIVATE (manager)->devices;

	device = g_slice_new0 (NMDHCPDevice);
	if (!device) {
		nm_warning ("%s: Out of memory creating DHCP transaction object.", iface);
		return NULL;
	}

	device->iface = g_strdup (iface);
	if (!device) {
		nm_warning ("%s: Out of memory creating DHCP transaction object "
		            "property 'iface'.",
		            iface);
		goto error;
	}
	
	device->manager = manager;

	nm_dhcp_manager_cancel_transaction_real (device, FALSE);

	/* Do this after the transaction cancel since that clears options out */
	device->options = g_hash_table_new_full (g_str_hash,
	                                         g_str_equal,
	                                         g_free,
	                                         g_free);
	if (!device->options) {
		nm_warning ("%s: Out of memory creating DHCP transaction object "
		            "property 'options'.",
		            iface);
		goto error;
	}

	g_hash_table_insert (hash, device->iface, device);
	return device;

error:
	g_hash_table_destroy (device->options);
	g_free (device->iface);
	g_slice_free (NMDHCPDevice, device);
	return NULL;
}


/*
 * nm_dhcp_manager_get_option
 *
 * Return the requested dhcp item for the given interface
 *
 */
static gpointer
nm_dhcp_manager_get_option(NMDHCPDevice *device, const char *key)
{
	return g_hash_table_lookup (device->options, key);
}


/*
 * dhclient_watch_cb
 *
 * Watch our child dhclient process and get notified of events from it.
 *
 */
static void dhclient_watch_cb (GPid pid, gint status, gpointer user_data)
{
	NMDHCPDevice *device = (NMDHCPDevice *)user_data;

	if (!WIFEXITED (status)) {
		device->state = DHC_ABEND;
		nm_warning ("dhclient died abnormally");
	}
	device->dhclient_pid = 0;

	nm_dhcp_device_watch_cleanup (device);
	nm_dhcp_device_timeout_cleanup (device);

	g_signal_emit (G_OBJECT (device->manager), signals[STATE_CHANGED], 0, device->iface, device->state);
}

static gboolean
dhclient_run (NMDHCPDevice *device, gchar *xtra_arg)
{
	const char **	dhclient_binary = NULL;
	GPtrArray *		dhclient_argv = NULL;
	GPid			pid;
	GError *		error = NULL;
	char *			pidfile = NULL;
	gboolean		success = FALSE;
	NMDHCPManagerPrivate *	priv;

	/* Find dhclient */
	dhclient_binary = dhclient_binary_paths;
	while (*dhclient_binary != NULL) {
		if (g_file_test (*dhclient_binary, G_FILE_TEST_EXISTS))
			break;
		dhclient_binary++;
	}

	if (!*dhclient_binary) {
		nm_warning ("Could not find dhclient binary.");
		goto out;
	}

	pidfile = get_pidfile_for_iface (device->iface);
	if (!pidfile) {
		nm_warning ("%s: not enough memory for dhclient options.", device->iface);
		goto out;
	}

	// FIXME: look for existing pidfile and kill dhclient

	dhclient_argv = g_ptr_array_new ();
	g_ptr_array_add (dhclient_argv, (gpointer) (*dhclient_binary));

	g_ptr_array_add (dhclient_argv, (gpointer) "-d");
	g_ptr_array_add (dhclient_argv, (gpointer) "-x");

	if (xtra_arg != NULL)
		g_ptr_array_add (dhclient_argv, (gpointer) xtra_arg);

	g_ptr_array_add (dhclient_argv, (gpointer) "-sf");	/* Set script file */
	g_ptr_array_add (dhclient_argv, (gpointer) SYSCONFDIR "/NetworkManager/callouts/nm-dhcp-client.action" );

	g_ptr_array_add (dhclient_argv, (gpointer) "-pf");	/* Set pid file */
	g_ptr_array_add (dhclient_argv, (gpointer) pidfile);

	g_ptr_array_add (dhclient_argv, (gpointer) device->iface);
	g_ptr_array_add (dhclient_argv, NULL);

	if (!g_spawn_async (NULL, (char **) dhclient_argv->pdata, NULL,
	                    G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
		nm_warning ("dhclient failed to start.  error: '%s'", error->message);
		g_error_free (error);
		goto out;
	}

	nm_info ("dhclient started with pid %d", pid);

	device->dhclient_pid = pid;
	device->watch_source = g_child_watch_source_new (pid);
	g_source_set_callback (device->watch_source,
	                       (GSourceFunc) dhclient_watch_cb,
	                       device,
	                       NULL);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (device->manager);
	g_source_attach (device->watch_source, priv->data->main_context);
	success = TRUE;

out:
	g_free (pidfile);
	g_ptr_array_free (dhclient_argv, TRUE);
	return success;
}

gboolean
nm_dhcp_manager_begin_transaction (NMDHCPManager *manager,
								   const char *iface,
								   guint32 timeout)
{
	NMDHCPManagerPrivate *priv;
	NMDHCPDevice *device;

	g_return_val_if_fail (NM_IS_DHCP_MANAGER (manager), FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	device = (NMDHCPDevice *) g_hash_table_lookup (priv->devices, iface);
	if (!device)
		device = nm_dhcp_device_new (manager, iface);

	if (state_is_bound (device->state)) {
		/* Cancel any DHCP transaction already in progress */
		nm_dhcp_manager_cancel_transaction_real (device, TRUE);
	}

	nm_info ("Activation (%s) Beginning DHCP transaction.", iface);

	if (timeout == 0)
		timeout = NM_DHCP_TIMEOUT;

	/* Set up a timeout on the transaction to kill it after the timeout */
	device->timeout_source = g_timeout_source_new (timeout * 1000);
	g_source_set_callback (device->timeout_source,
	                       nm_dhcp_manager_handle_timeout,
	                       device,
	                       NULL);
	g_source_attach (device->timeout_source,
	                 priv->data->main_context);

	dhclient_run (device, NULL);

	return TRUE;
}

static void
nm_dhcp_manager_cancel_transaction_real (NMDHCPDevice *device, gboolean blocking)
{
	int i = 20; /* 4 seconds */
	char * pidfile;

	if (!device->dhclient_pid)
		return;

	kill (device->dhclient_pid, SIGTERM);

	/* Yes, the state has to reach DHC_END. */
	while (blocking && i-- > 0) {
		gint child_status;
		if (waitpid (device->dhclient_pid, &child_status, WNOHANG) > 0)
			break;
		g_usleep (G_USEC_PER_SEC / 5);
	}

	if (i <= 0) {
		nm_warning ("%s: dhclient pid %d didn't exit, will kill it.",
		            device->iface, device->dhclient_pid);
		kill (device->dhclient_pid, SIGKILL);
	}

	/* Clean up the pidfile if it got left around */
	pidfile = get_pidfile_for_iface (device->iface);
	if (pidfile) {
		remove (pidfile);
		g_free (pidfile);
	}
	device->dhclient_pid = 0;
	device->state = DHC_END;

	nm_dhcp_device_watch_cleanup (device);
	nm_dhcp_device_timeout_cleanup (device);
	g_hash_table_remove_all (device->options);	
}


/*
 * nm_dhcp_manager_cancel_transaction
 *
 * Stop any in-progress DHCP transaction on a particular device.
 *
 */
void
nm_dhcp_manager_cancel_transaction (NMDHCPManager *manager,
                                    const char *iface,
									gboolean blocking)
{
	NMDHCPDevice *device;
	NMDHCPManagerPrivate *priv;

	g_return_if_fail (NM_IS_DHCP_MANAGER (manager));
	g_return_if_fail (iface != NULL);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	device = (NMDHCPDevice *) g_hash_table_lookup (priv->devices, iface);

	if (!device || !device->dhclient_pid)
		return;

	nm_dhcp_manager_cancel_transaction_real (device, blocking);
}


static gboolean
handle_request_cancel (gpointer user_data)
{
	NMDHCPDevice *device = (NMDHCPDevice *) user_data;

fprintf (stderr, "request_handle_cancel started...\n");
	nm_dhcp_manager_cancel_transaction_real (device, TRUE);
fprintf (stderr, "request_handle_cancel done.\n");
fprintf (stderr, "%s: calling cancel_cleanup\n", __func__);
	nm_dhcp_device_cancel_cleanup (device);
	return FALSE;
}


/*
 * nm_dhcp_manager_request_cancel_transaction
 *
 * Request that any in-progress transaction be canceled.
 *
 */
void
nm_dhcp_manager_request_cancel_transaction (NMDHCPManager *manager,
                                            const char *iface,
                                            gboolean blocking)
{
	NMDHCPDevice *device;
	NMDHCPManagerPrivate *priv;

	g_return_if_fail (NM_IS_DHCP_MANAGER (manager));
	g_return_if_fail (iface != NULL);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	device = (NMDHCPDevice *) g_hash_table_lookup (priv->devices, iface);

	if (!device || !device->dhclient_pid)
		return;

	if (!device->cancel_source) {
		device->cancel_source = g_idle_source_new ();
fprintf (stderr, "%s: created cancel source %p\n", __func__, device->cancel_source);
		g_source_set_priority (device->cancel_source, G_PRIORITY_HIGH_IDLE);
		g_source_set_callback (device->cancel_source,
		                       handle_request_cancel,
		                       device,
		                       NULL);
		g_source_attach (device->cancel_source,
		                 priv->data->main_context);
	}

	while (blocking && device->cancel_source)
		g_usleep (G_USEC_PER_SEC / 10);
}


/*
 * nm_dhcp_manager_get_ip4_config
 *
 * Get IP4 configuration values from the DHCP daemon
 *
 */
NMIP4Config *
nm_dhcp_manager_get_ip4_config (NMDHCPManager *manager,
                                const char *iface)
{
	NMDHCPManagerPrivate *priv;
	NMDHCPDevice *device;
	NMIP4Config *		ip4_config = NULL;
	guint32			ip4_address = 0;
	guint32			ip4_netmask = 0;
	guint32			ip4_broadcast = 0;
	guint32			ip4_gateway = 0;
	char *			hostname = NULL;
	char *			domain_names = NULL;
	char *			nameservers = NULL;
	char *			nis_domain = NULL;
	char *			nis_servers = NULL;
	char *			ip = NULL;		//this is a general string that is used as a temporary place for ip(s)

	g_return_val_if_fail (NM_IS_DHCP_MANAGER (manager), NULL);
	g_return_val_if_fail (iface != NULL, NULL);

	priv = NM_DHCP_MANAGER_GET_PRIVATE (manager);

	device = (NMDHCPDevice *) g_hash_table_lookup (priv->devices, iface);
	if (!device) {
		nm_warning ("Device '%s' transaction not started.", iface);
		return NULL;
	}

	if (!state_is_bound (device->state)) {
		nm_warning ("%s: dhclient didn't bind to a lease.", device->iface);
		return NULL;
	}

	if (!state_is_bound (device->state)) {
		nm_warning ("Tried to get IP4 Config for a device when DHCP "
		            "wasn't in a BOUND state!");
		return NULL;
	}

	ip = g_hash_table_lookup (device->options, "new_ip_address");
	if (ip != NULL) {
		ip4_address = inet_addr (ip);
		nm_info("  address %s", ip);
	}
	else {
		return NULL;
	}

	ip = g_hash_table_lookup (device->options, "new_subnet_mask");
	if (ip != NULL) {
		ip4_netmask = inet_addr (ip);
		nm_info("  netmask %s", ip);
	}
	else {
		return NULL;
	}

	ip = g_hash_table_lookup (device->options, "new_broadcast_address");
	if (ip != NULL) {
		ip4_broadcast = inet_addr (ip);
		nm_info("  broadcast %s", ip);
	}
	else {
		return NULL;
	}

	ip = g_hash_table_lookup (device->options, "new_routers");
	if (ip != NULL) {
		ip4_gateway = inet_addr (ip);
	}
	else { /* If DHCP doesn't have a 'routers', just use the DHCP server's address as our gateway for now */
		ip = g_hash_table_lookup (device->options, "new_dhcp_server_identifier");
		if (ip != NULL)
			ip4_gateway = inet_addr (ip);
		else
			return NULL;
	}

	nm_info("  gateway %s", ip);

	ip4_config = nm_ip4_config_new ();
	nm_ip4_config_set_address (ip4_config, ip4_address);
	nm_ip4_config_set_netmask (ip4_config, ip4_netmask);
	nm_ip4_config_set_broadcast (ip4_config, ip4_broadcast);
	nm_ip4_config_set_gateway (ip4_config, ip4_gateway);

	hostname = g_hash_table_lookup (device->options, "new_host_name");
	nameservers = g_hash_table_lookup (device->options, "new_domain_name_servers");
	domain_names = g_hash_table_lookup (device->options, "new_domain_name");
	nis_domain = g_hash_table_lookup (device->options, "new_nis_domain");
	nis_servers = g_hash_table_lookup (device->options, "new_nis_servers");


	if (nameservers) {
		char **searches = g_strsplit (nameservers, " ", 0);
		char **s;
		int ip4_nameserver;

		for (s = searches; *s; s++) {
			ip4_nameserver = inet_addr (*s);
			nm_ip4_config_add_nameserver (ip4_config, ip4_nameserver);
			nm_info ("  nameserver '%s'", *s);
		}
		g_strfreev (searches);
	}

	if (hostname) {
		nm_ip4_config_set_hostname (ip4_config, hostname);
		nm_info ("  hostname '%s'", hostname);
	}

	if (domain_names) {
		char **searches = g_strsplit (domain_names, " ", 0);
		char **s;

		for (s = searches; *s; s++) {
			nm_info ("  domain name '%s'", *s);
			nm_ip4_config_add_domain (ip4_config, *s);
		}
		g_strfreev (searches);
	}

	if (nis_domain) {
		nm_ip4_config_set_nis_domain (ip4_config, nis_domain);
		nm_info ("  nis domain '%s'", nis_domain);
	}

	if (nis_servers) {
		char **searches = g_strsplit (nis_servers, " ", 0);
		char **s;
		int ip4_nis_server;

		for (s = searches; *s; s++) {
			ip4_nis_server = inet_addr (*s);
			nm_ip4_config_add_nis_server (ip4_config, ip4_nis_server);
			nm_info ("  nis server '%s'", *s);
		}
		g_strfreev (searches);
	}

	/*
	 * FIXME:
	 * Grab the MTU from the backend. If DHCP servers can send recommended
	 * MTU's, should set that here.
	 */

	return ip4_config;
}
