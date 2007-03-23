/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <glib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include "nm-device.h"
#include "nm-device-802-11-mesh-olpc.h"
#include "nm-device-private.h"
#include "NetworkManagerUtils.h"
#include "nm-device-802-11-wireless.h"
#include "nm-utils.h"
#include "nm-activation-request.h"


#define IPV4LL_NETWORK 0xA9FE0000L
#define IPV4LL_NETMASK 0xFFFF0000L
#define IPV4LL_HOSTMASK 0x0000FFFFL
#define IPV4LL_BROADCAST 0xA9FEFFFFL

static void aipd_cleanup (NMDevice80211MeshOLPC *self);


#define NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPCPrivate))

struct _autoipd
{
	GPid		pid;
	GSource *	watch;
	GSource *	timeout;

	guint32		ip4_addr;
};

struct _ethdev {
	NMDevice80211Wireless * dev;
	gulong		scan_started_id;
	gulong		scan_done_id;
	gulong		activation_started_id;
	gulong		activation_done_id;
};

struct _NMDevice80211MeshOLPCPrivate
{
	gboolean	dispose_has_run;
	gboolean	is_initialized;

	guint32		capabilities;

	struct _ethdev ethdev;

	struct _autoipd	aipd;
};


static void
nm_device_802_11_mesh_olpc_init (NMDevice80211MeshOLPC * self)
{
	self->priv = NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE (self);
	self->priv->dispose_has_run = FALSE;
	self->priv->is_initialized = FALSE;
}

static void
real_init (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	self->priv->is_initialized = TRUE;
	self->priv->capabilities = 0;
}

static gboolean
ethdev_scan_approval_hook (NMDevice80211Wireless *ethdev,
                           gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_val_if_fail (ethdev != NULL, TRUE);
	g_return_val_if_fail (self != NULL, TRUE);

fprintf (stderr, "%s: allowing scan for %s\n",
         nm_device_get_iface (NM_DEVICE (self)),
         nm_device_get_iface (NM_DEVICE (ethdev)));

	return TRUE;
}

static void
ethdev_scan_started_cb (GObject * obj,
                        gpointer user_data)
{
	NMDevice80211Wireless * ethdev = NM_DEVICE_802_11_WIRELESS (obj);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_if_fail (self != NULL);
	g_return_if_fail (ethdev != NULL);

	fprintf (stderr, "%s: scan started event for %s\n",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
}

static void
ethdev_scan_done_cb (GObject * obj,
                     gpointer user_data)
{
	NMDevice80211Wireless * ethdev = NM_DEVICE_802_11_WIRELESS (obj);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_if_fail (self != NULL);
	g_return_if_fail (ethdev != NULL);

	fprintf (stderr, "%s: scan done event for %s\n",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
}

static void
ethdev_activation_started_cb (GObject * obj,
                              gpointer user_data)
{
	NMDevice80211Wireless * ethdev = NM_DEVICE_802_11_WIRELESS (obj);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_if_fail (self != NULL);
	g_return_if_fail (ethdev != NULL);

	fprintf (stderr, "%s: activation started event for %s\n",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
}

static void
ethdev_activation_done_cb (GObject * obj,
                           gpointer user_data)
{
	NMDevice80211Wireless * ethdev = NM_DEVICE_802_11_WIRELESS (obj);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_if_fail (self != NULL);
	g_return_if_fail (ethdev != NULL);

	fprintf (stderr, "%s: activation done event for %s\n",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
}

static void
setup_ethdev (NMDevice80211MeshOLPC *self,
              NMDevice80211Wireless *ethdev)
{
	struct _ethdev	* ethdev_rec;

	g_return_if_fail (self != NULL);
	g_return_if_fail (ethdev != NULL);

	g_object_ref (ethdev);
	self->priv->ethdev.dev = NM_DEVICE_802_11_WIRELESS (ethdev);
	nm_info ("%s: found 802.11 companion device %s.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));

	if (nm_device_802_11_wireless_is_scanning (ethdev))
		ethdev_scan_started_cb (G_OBJECT (ethdev), (gpointer) self);

	nm_device_802_11_wireless_register_scan_approval_hook (ethdev,
	        ethdev_scan_approval_hook, self);

	ethdev_rec = &self->priv->ethdev;
	ethdev_rec->scan_started_id =
				g_signal_connect (G_OBJECT (ethdev),
	                              "scan-started",
	                              G_CALLBACK (ethdev_scan_started_cb),
	                              self);
	ethdev_rec->scan_done_id =
				g_signal_connect (G_OBJECT (ethdev),
	                              "scan-done",
	                              G_CALLBACK (ethdev_scan_done_cb),
	                              self);
	ethdev_rec->activation_started_id =
				g_signal_connect (G_OBJECT (NM_DEVICE (ethdev)),
	                              "activation-started",
	                              G_CALLBACK (ethdev_activation_started_cb),
	                              self);
	ethdev_rec->activation_done_id =
				g_signal_connect (G_OBJECT (NM_DEVICE (ethdev)),
	                              "activation-done",
	                              G_CALLBACK (ethdev_activation_done_cb),
	                              self);
}

static void
cleanup_ethdev (NMDevice80211MeshOLPC *self)
{
	NMDevice80211Wireless * ethdev;
	struct _ethdev	* ethdev_rec;

	g_return_if_fail (self != NULL);

	ethdev_rec = &self->priv->ethdev;
	ethdev = ethdev_rec->dev;

	nm_device_802_11_wireless_register_scan_approval_hook (ethdev, NULL, NULL);

	g_signal_handler_disconnect (G_OBJECT (ethdev), ethdev_rec->scan_started_id);
	ethdev_rec->scan_started_id = 0;
	g_signal_handler_disconnect (G_OBJECT (ethdev), ethdev_rec->scan_done_id);
	ethdev_rec->scan_done_id = 0;
	g_signal_handler_disconnect (G_OBJECT (ethdev), ethdev_rec->activation_started_id);
	ethdev_rec->activation_started_id = 0;
	g_signal_handler_disconnect (G_OBJECT (ethdev), ethdev_rec->activation_done_id);
	ethdev_rec->activation_done_id = 0;

	nm_info ("%s: 802.11 companion device %s removed.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
	g_object_unref (ethdev);
	ethdev_rec->dev = NULL;
}

static void
real_start (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * data = nm_device_get_app_data (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	GSList * elt;

	g_assert (mesh_physdev);

	/* Find the 802.11 non-mesh device if HAL knows about it already */
	nm_lock_mutex (data->dev_list_mutex, __FUNCTION__);

	for (elt = data->dev_list; elt != NULL; elt = g_slist_next (elt)) {
		NMDevice * ethdev = (NMDevice *)(elt->data);
		const char * eth_physdev;

		if (!nm_device_is_802_11_wireless (ethdev))
			continue;
		eth_physdev = nm_device_get_physical_device_udi (ethdev);
		if (!eth_physdev)
			continue;
		if (strcmp (mesh_physdev, eth_physdev) == 0) {
			setup_ethdev (self, NM_DEVICE_802_11_WIRELESS (ethdev));
			break;
		}
	}

	nm_unlock_mutex (data->dev_list_mutex, __FUNCTION__);

#if 0
	if (self->priv->ethdev)
		nm_device_set_active_link (NM_DEVICE (self), TRUE);
#endif
}

static void
real_notify_device_added (NMDevice *dev,
                          NMDevice *added_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * data = nm_device_get_app_data (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	const char * eth_physdev = NULL;

	if (dev == added_dev)
		return;

	if (self->priv->ethdev.dev || !nm_device_is_802_11_wireless (added_dev))
		return;

	eth_physdev = nm_device_get_physical_device_udi (added_dev);
	if (!eth_physdev)
		return;

	if (strcmp (mesh_physdev, eth_physdev) != 0)
		return;

	setup_ethdev (self, NM_DEVICE_802_11_WIRELESS (added_dev));

#if 0
	nm_device_set_active_link (NM_DEVICE (self), TRUE);
#endif
}

static void
real_notify_device_removed (NMDevice *dev,
                            NMDevice *removed_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * data = nm_device_get_app_data (dev);

	if (dev == removed_dev)
		return;

	if (!self->priv->ethdev.dev)
		return;

	if (   !nm_device_is_802_11_wireless (removed_dev)
	    || (self->priv->ethdev.dev != NM_DEVICE_802_11_WIRELESS (removed_dev)))
		return;

	cleanup_ethdev (self);

	nm_device_set_active_link (NM_DEVICE (self), FALSE);
}

static void
real_deactivate_quickly (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	aipd_cleanup (self);
}


static void
real_deactivate (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
}

static guint32
real_get_generic_capabilities (NMDevice *dev)
{
	return NM_DEVICE_CAP_NM_SUPPORTED;
}

static guint32
real_get_type_capabilities (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	return self->priv->capabilities;
}

static int
get_80211_mode (NMDevice80211MeshOLPC *self)
{
	NMSock * sk;
	int mode = -1;
	struct iwreq wrq;
	int err;

	g_return_val_if_fail (self != NULL, -1);

	/* Force the card into Managed/Infrastructure mode */
	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", nm_device_get_iface (NM_DEVICE (self)));
		return -1;
	}

	err = iw_get_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (self)),
	                  SIOCGIWMODE,
	                  &wrq);
	if (err) {
		nm_warning ("%s: failed to get device mode (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (self)),
		            errno);
		goto out;
	}

	mode = wrq.u.mode;

out:
	nm_dev_sock_close (sk);
	return mode;
}

static gboolean
set_80211_mode (NMDevice80211MeshOLPC *self,
                int mode)
{
	NMSock * sk;
	gboolean success = FALSE;
	struct iwreq wrqu;
	int err;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail ((mode == IW_MODE_INFRA) || (mode == IW_MODE_ADHOC), FALSE);

	if (get_80211_mode (self) == mode)
		return TRUE;

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", nm_device_get_iface (NM_DEVICE (self)));
		return FALSE;
	}

	wrqu.u.mode = mode;
	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (self)),
	                  SIOCSIWMODE,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to set device mode to %d (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (self)),
		            mode,
		            errno);
		goto out;
	}

	success = TRUE;

out:
	nm_dev_sock_close (sk);
	return success;
}

static gboolean
clear_80211_keys (NMDevice80211MeshOLPC *self)
{
	NMSock * sk;
	struct iwreq wrqu;
	gboolean success = FALSE;
	int err;

	g_return_val_if_fail (self != NULL, FALSE);

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", nm_device_get_iface (NM_DEVICE (self)));
		return FALSE;
	}

	wrqu.u.data.pointer = (caddr_t) NULL;
	wrqu.u.data.length = 0;
	wrqu.u.data.flags = IW_ENCODE_DISABLED | IW_ENCODE_NOKEY;

	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (self)),
	                  SIOCSIWENCODE,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to clear encryption keys (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (self)),
		            errno);
		goto out;
	}

	success = TRUE;

out:
	nm_dev_sock_close (sk);
	return success;
}

static gboolean 
set_80211_ssid (NMDevice80211MeshOLPC *self,
                const char *ssid,
                const guint32 ssid_len)
{
	NMSock * sk;
	int err;
	struct iwreq wrqu;
	char * safe_ssid = NULL;
	guint32 safe_len = (ssid_len > IW_ESSID_MAX_SIZE) ? IW_ESSID_MAX_SIZE : ssid_len;
	gboolean success = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);

	/* Make sure the essid we get passed is a valid size */
	if (!ssid || !ssid_len) {
		safe_len = 0;
	} else {
		safe_ssid = g_malloc0 (IW_ESSID_MAX_SIZE + 1);
		memcpy (safe_ssid, ssid, safe_len);
	}

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", nm_device_get_iface (NM_DEVICE (self)));
		return FALSE;
	}

	wrqu.u.essid.pointer = (caddr_t) safe_ssid;
	wrqu.u.essid.length	 = safe_len;
	wrqu.u.essid.flags	 = 1;	/* Enable essid on card */
	
	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (self)),
	                  SIOCSIWESSID,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to set SSID (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (self)),
		            errno);
		goto out;
	}

	success = TRUE;

out:
	g_free (safe_ssid);
	nm_dev_sock_close (sk);
	return success;
}


static double
get_80211_freq (NMDevice80211MeshOLPC *self)
{
	NMSock * sk;
	int err;
	double freq = 0;
	struct iwreq wrqu;
	const char * iface;

	g_return_val_if_fail (self != NULL, -1);

	iface = nm_device_get_iface (NM_DEVICE (self));
	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", iface);
		return -1;
	}

	err = iw_get_ext (nm_dev_sock_get_fd (sk),
	                  iface,
	                  SIOCGIWFREQ,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to get frequency (errno: %d).",
		            iface,
		            errno);
		goto out;
	}

	freq = iw_freq2float (&wrqu.u.freq);

out:
	nm_dev_sock_close (sk);
	return freq;
}

static gboolean
set_80211_frequency (NMDevice80211MeshOLPC *self,
                     const double freq)
{
	NMSock * sk;
	int err;
	struct iwreq wrqu;
	gboolean success = FALSE;
	int skfd;
	const char * iface;

	g_return_val_if_fail (self != NULL, FALSE);

	if ((freq > 0) && (get_80211_frequency (self) == freq))
		return FALSE;

	iface = nm_device_get_iface (NM_DEVICE (self));

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", iface);
		return FALSE;
	}

	if (freq <= 0) {
		/* Try 0 for "auto" first, fall back to -1 below of 0 doesn't work */
		wrqu.u.freq.m = 0;
		wrqu.u.freq.e = 0;
		wrqu.u.freq.flags = 0;
	} else {
		/* Fixed */
		wrqu.u.freq.flags = IW_FREQ_FIXED;
		iw_float2freq (freq, &wrqu.u.freq);
	}

	skfd = nm_dev_sock_get_fd (sk);
	err = iw_set_ext (skfd, iface, SIOCSIWFREQ, &wrqu);
	if (err) {
		if ((freq <= 0) && ((errno == EINVAL) || (errno == EOPNOTSUPP))) {
			/* Ok, try "auto" the other way */
			wrqu.u.freq.m = -1;
			wrqu.u.freq.e = 0;
			wrqu.u.freq.flags = 0;
			err = iw_set_ext (skfd, iface, SIOCSIWFREQ, &wrqu);
			if (err) {
				nm_warning ("%s: failed to set frequency to auto (errno: %d)",
				            iface,
				            errno);
				goto out;
			}
		} else {
			nm_warning ("%s: failed to set frequency to %f (errno: %d)",
			            iface,
			            freq,
			            errno);
			goto out;
		}
	}

	success = TRUE;

out:
	nm_dev_sock_close (sk);
	return success;
}


/*************************************************************/
/* avahi-autoipd babysitting junk because it doesn't do DBus */
/*************************************************************/

static void
aipd_remove_timeout (NMDevice80211MeshOLPC *self)
{
	g_return_if_fail (self != NULL);

	/* Remove any pending timeouts on the request */
	if (!self->priv->aipd.timeout)
		return;

	g_source_destroy (self->priv->aipd.timeout);
	self->priv->aipd.timeout = NULL;
}

static void
aipd_cleanup (NMDevice80211MeshOLPC *self)
{
	g_return_if_fail (self != NULL);

	if (self->priv->aipd.pid > 0)
	{
		kill (self->priv->aipd.pid, SIGTERM);
		self->priv->aipd.pid = -1;
	}
	if (self->priv->aipd.watch)
	{
		g_source_destroy (self->priv->aipd.watch);
		self->priv->aipd.watch = NULL;
	}

	aipd_remove_timeout (self);

	self->priv->aipd.ip4_addr = 0;
}

static void
aipd_watch_done (gpointer user_data)
{
	NMDevice80211MeshOLPC *device = NM_DEVICE_802_11_MESH_OLPC (user_data);

	device->priv->aipd.watch = NULL;
}

static void
aipd_watch_cb (GPid pid,
               gint status,
               gpointer user_data)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMDevice * dev = NM_DEVICE (user_data);

	g_assert (self);

	if (WIFEXITED (status))
		nm_warning ("avahi-autoipd exited with error code %d", WEXITSTATUS (status));
	else if (WIFSTOPPED (status)) 
		nm_warning ("avahi-autoipd stopped unexpectedly with signal %d", WSTOPSIG (status));
	else if (WIFSIGNALED (status))
		nm_warning ("avahi-autoipd died with signal %d", WTERMSIG (status));
	else
		nm_warning ("avahi-autoipd died from an unknown cause");

	aipd_cleanup (self);

/*	nm_device_set_active_link (dev, FALSE); */
}

static void
supplicant_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point.
	 * Give child it's own program group for signal
	 * separation.
	 */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static gboolean
aipd_exec (NMDevice80211MeshOLPC *self)
{
	char * argv[3];
	GError * error = NULL;
	GPid pid = -1;
	gboolean success = FALSE;

	argv[0] = "/usr/sbin/avahi-autoipd";
	argv[1] = (char *) nm_device_get_iface (NM_DEVICE (self));
	argv[2] = NULL;

	success = g_spawn_async ("/", argv, NULL, 0,
	                         &supplicant_child_setup, NULL, &pid, &error);
	if (!success) {
		if (error) {
			nm_warning ("Couldn't start avahi-autoipd.  Error: (%d) %s",
					error->code, error->message);
			g_error_free (error);
		} else {
			nm_warning ("Couldn't start avahi-autoipd due to an unknown error.");
		}
		return FALSE;
	}

	/* Monitor the child process so we know when it stops */
	self->priv->aipd.pid = pid;
	if (self->priv->aipd.watch)
		g_source_destroy (self->priv->aipd.watch);
	self->priv->aipd.watch = g_child_watch_source_new (pid);
	g_source_set_callback (self->priv->aipd.watch,
						   (GSourceFunc) aipd_watch_cb,
						   self,
						   aipd_watch_done);
	g_source_attach (self->priv->aipd.watch, nm_device_get_main_context (NM_DEVICE (self)));
	g_source_unref (self->priv->aipd.watch);

	return TRUE;
}

static gboolean
aipd_timeout_cb (gpointer user_data)
{
	NMDevice * dev = NM_DEVICE (user_data);
 	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);	

	g_assert (dev);

	nm_info ("%s: avahi-autoipd timed out.", nm_device_get_iface (dev));
	nm_policy_schedule_activation_failed (nm_device_get_act_request (dev));

	return FALSE;
}

static void
aipd_timeout_done (gpointer user_data)
{
	NMDevice80211MeshOLPC *device = NM_DEVICE_802_11_MESH_OLPC (user_data);

	device->priv->aipd.timeout = NULL;
}

static gboolean
aipd_monitor_start (NMDevice80211MeshOLPC *self)
{
	gboolean success = FALSE;
	GIOChannel *	channel;
	GMainContext *	context;

	g_return_val_if_fail (self != NULL, FALSE);

	/* Set up a timeout to kill it after 10 seconds */
	self->priv->aipd.timeout = g_timeout_source_new (10000);
	context = nm_device_get_main_context (NM_DEVICE (self));
	g_source_set_callback (self->priv->aipd.timeout,
						   aipd_timeout_cb,
						   self,
						   aipd_timeout_done);
	g_source_attach (self->priv->aipd.timeout, context);
	g_source_unref (self->priv->aipd.timeout);

	success = TRUE;

out:
	return success;
}

/*************************************************************/

static NMActStageReturn
real_act_stage2_config (NMDevice *dev,
                        NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;

	/* Mesh interface should be in adhoc mode */
	if (!set_80211_mode (self, IW_MODE_ADHOC))
		goto out;

	if (!clear_80211_keys (self))
		goto out;

#define MESH_SSID "olpc-mesh"
	if (!set_80211_ssid (self, MESH_SSID, strlen (MESH_SSID)))
		goto out;

	ret = NM_ACT_STAGE_RETURN_SUCCESS;

out:
	return ret;
}

static NMActStageReturn
real_act_stage3_ip_config_start (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	const char * iface = nm_device_get_iface (dev);

	if (!aipd_exec (self)) {
		nm_warning ("Activation (%s/mesh): couldn't start avahi-autoipd.",
			iface);
		goto out;
	}

	if (!aipd_monitor_start (self)) {
		nm_warning ("Activation (%s/mesh): couldn't monitor avahi-autoipd.",
			iface);
		goto out;
	}

	ret = NM_ACT_STAGE_RETURN_POSTPONE;

out:
	return ret;
}

static NMActStageReturn
real_act_stage4_get_ip4_config (NMDevice *dev,
                                NMActRequest *req,
                                NMIP4Config **config)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	NMIP4Config *			real_config = NULL;

	g_return_val_if_fail (config != NULL, NM_ACT_STAGE_RETURN_FAILURE);
	g_return_val_if_fail (*config == NULL, NM_ACT_STAGE_RETURN_FAILURE);

	real_config = nm_ip4_config_new ();
	nm_ip4_config_set_address (real_config, self->priv->aipd.ip4_addr);
	nm_ip4_config_set_netmask (real_config, (guint32)(ntohl (IPV4LL_NETMASK)));
	nm_ip4_config_set_broadcast (real_config, (guint32)(ntohl (IPV4LL_BROADCAST)));
	nm_ip4_config_set_gateway (real_config, 0);

	*config = real_config;

	return NM_ACT_STAGE_RETURN_SUCCESS;
}

static void
real_activation_success_handler (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * app_data;

	app_data = nm_act_request_get_data (req);
	g_assert (app_data);

	/* FIXME: start route request stuff */

	nm_device_set_active_link (dev, TRUE);
}


static void
real_activation_failure_handler (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData *			app_data;

	app_data = nm_act_request_get_data (req);
	g_assert (app_data);

	/* FIXME: whatever */
}

static void
real_handle_autoip_event (NMDevice *dev,
                          const char *event,
                          const char *addr)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActRequest * req = nm_device_get_act_request (dev);
	struct in_addr ip;
	int ret;

	if (!req) {
		nm_warning ("%s: not active but got avahi-autoip event %s for %s.",
		         nm_device_get_iface (dev),
		         event,
		         addr);
		return;
	}

	if (strcmp (event, "BIND") == 0) {
	 	if (nm_act_request_get_stage (req) != NM_ACT_STAGE_IP_CONFIG_START) {
			nm_warning ("%s: got avahi-autoip event %s for %s at wrong activation stage.",
			         nm_device_get_iface (dev),
			         event,
			         addr);
			return;
	 	}

		ret = inet_aton (addr, &ip);
		if (ret) {
			if ((ntohl(ip.s_addr) & IPV4LL_NETMASK) != IPV4LL_NETWORK) {
				nm_warning ("%s: got invalid autoip address %s (0x%X).",
				         nm_device_get_iface (dev),
				         addr, ip.s_addr);
				nm_policy_schedule_activation_failed (req);
			} else {
				/* success */
				self->priv->aipd.ip4_addr = ip.s_addr;
				aipd_remove_timeout (self);
				nm_device_activate_schedule_stage4_ip_config_get (req);
			}
		} else {
			nm_warning ("%s: got invalid autoip address %s.",
			         nm_device_get_iface (dev),
			         addr);
			nm_policy_schedule_activation_failed (req);
		}		
	} else {
		nm_warning ("%s: autoip address %s no longer valid.",
		         nm_device_get_iface (dev),
		         addr);

		/* The LL address is gone.  Terminate the connection or fail
		 * activation.
		 */
		if (nm_device_is_activating (dev)) {
			nm_policy_schedule_activation_failed (req);
		} else {
			nm_device_set_active_link (dev, FALSE);
		}
	}
}

static void
nm_device_802_11_mesh_olpc_dispose (GObject *object)
{
	NMDevice80211MeshOLPC *		self = NM_DEVICE_802_11_MESH_OLPC (object);
	NMDevice80211MeshOLPCClass *	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (object);
	NMDeviceClass *			parent_class;

	/* Make sure dispose does not run twice. */
	if (self->priv->dispose_has_run)
		return;

	self->priv->dispose_has_run = TRUE;

	/* Only do this part of the cleanup if the object is initialized */
	if (self->priv->is_initialized) {
	}

	/* Chain up to the parent class */
	parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
nm_device_802_11_mesh_olpc_finalize (GObject *object)
{
	NMDevice80211MeshOLPCClass *	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (object);
	NMDeviceClass *			parent_class;

	/* Chain up to the parent class */
	parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
nm_device_802_11_mesh_olpc_class_init (NMDevice80211MeshOLPCClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *parent_class = NM_DEVICE_CLASS (klass);

	object_class->dispose = nm_device_802_11_mesh_olpc_dispose;
	object_class->finalize = nm_device_802_11_mesh_olpc_finalize;

	parent_class->get_type_capabilities = real_get_type_capabilities;
	parent_class->get_generic_capabilities = real_get_generic_capabilities;
	parent_class->init = real_init;
	parent_class->start = real_start;
	parent_class->deactivate = real_deactivate;
	parent_class->deactivate_quickly = real_deactivate_quickly;
	parent_class->notify_device_added = real_notify_device_added;
	parent_class->notify_device_removed = real_notify_device_removed;

	parent_class->act_stage2_config = real_act_stage2_config;
	parent_class->act_stage3_ip_config_start = real_act_stage3_ip_config_start;
	parent_class->act_stage4_get_ip4_config = real_act_stage4_get_ip4_config;
	parent_class->activation_failure_handler = real_activation_failure_handler;
	parent_class->activation_success_handler = real_activation_success_handler;

	parent_class->handle_autoip_event = real_handle_autoip_event;

	g_type_class_add_private (object_class, sizeof (NMDevice80211MeshOLPCPrivate));
}

GType
nm_device_802_11_mesh_olpc_get_type (void)
{
	static GType type = 0;
	if (type == 0)
	{
		static const GTypeInfo info =
		{
			sizeof (NMDevice80211MeshOLPCClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) nm_device_802_11_mesh_olpc_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (NMDevice80211MeshOLPC),
			0,		/* n_preallocs */
			(GInstanceInitFunc) nm_device_802_11_mesh_olpc_init,
			NULL		/* value_table */
		};
		type = g_type_register_static (NM_TYPE_DEVICE,
					       "NMDevice80211MeshOLPC",
					       &info, 0);
	}
	return type;
}
