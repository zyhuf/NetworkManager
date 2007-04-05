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
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>

#include "nm-device.h"
#include "nm-device-802-11-mesh-olpc.h"
#include "nm-device-private.h"
#include "NetworkManagerUtils.h"
#include "nm-device-802-11-wireless.h"
#include "nm-utils.h"
#include "nm-activation-request.h"
#include "NetworkManagerSystem.h"
#include "NetworkManagerPolicy.h"


#if USE_AUTOIP
#define IPV4LL_NETWORK 0xA9FE0000L
#define IPV4LL_NETMASK 0xFFFF0000L
#define IPV4LL_HOSTMASK 0x0000FFFFL
#define IPV4LL_BROADCAST 0xA9FEFFFFL
#else
#define MESH_DHCP_TIMEOUT	20	/* in seconds */
#endif


static void mesh_search_cleanup (NMDevice80211MeshOLPC *self);
#if USE_AUTOIP
static void aipd_cleanup (NMDevice80211MeshOLPC *self);
#endif
static void mpp_discovery_cleanup (NMDevice80211MeshOLPC *self);
static gboolean mpp_discovery_send_rreq (NMDevice80211MeshOLPC *self);


#define NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_802_11_MESH_OLPC, NMDevice80211MeshOLPCPrivate))

#if USE_AUTOIP
struct _autoipd
{
	GPid		pid;
	GSource *	watch;
	GSource *	timeout;

	guint32		ip4_addr;
};
#endif

struct _ethdev {
	NMDevice80211Wireless * dev;
	gulong		scan_started_id;
	gulong		scan_done_id;
	gulong		activation_started_id;
	gulong		activation_done_id;
	gboolean	scanning;
};

struct _msearch {
	GSource *		check;
	guint32			check_tries;

	GHashTable *	fwt;
};

struct _mpp {
	GIOChannel *	chan;
	GSource *		chan_src;
	int				sk;
	guint32			tries;
	GSource *		timeout_src;
};

struct _NMDevice80211MeshOLPCPrivate
{
	gboolean	dispose_has_run;
	gboolean	is_initialized;

	guint32		capabilities;

	struct _ethdev	ethdev;
#if USE_AUTOIP
	struct _autoipd	aipd;
#endif
	struct _mpp		mpp;
	struct _msearch	msearch;

	/* Theory of operation:
	 *
	 * There are 2 attempts to connect to a mesh.  Each attempt
	 * goes through channels 1, 6, and 11.  The first attempt tries mesh
	 * 'discovery' by checking the mesh firmware forwarding table for changes
	 * to quickly identify the presence of a mesh on the channel.  The second
	 * attempt just tries to do MPP discovery on each channel in case there
	 * are not enough mesh members sending traffic during the mesh discovery
	 * in the first attempt.
	 *
	 * 1) Attempt #1 - for each channel of [1, 6, 11]:
	 *    a) Switch to channel
	 *    b) Does the mesh forwarding table change?
	 *        - Yes: go to (1c)
	 *        - No:  another channel to try?
	 *            - Yes: next channel, go to (1a)
	 *            - No:  go to (2)
	 *    c) Get an auto-ip address
	 *    d) Does an MPP exist (determined via MPP discovery)?
	 *        - Yes: Done
	 *        - No:  another channel to try?
	 *            - Yes: next channel, go to (1a)
	 *            - No:  go to (2)
	 * 2) Attempt #2 - for each channel of [1, 6, 11]:
	 *    a) Switch to channel
	 *    b) Get an auto-ip address
	 *    c) Does an MPP exist (determined via MPP discovery)?
	 *        - Yes: Done
	 *        - No:  another channel to try?
	 *            - Yes: next channel, go to (1a)
	 *            - No:  fail activation
	 */
	guint32		channel;
	guint32		num_channels_tried;
	guint32		attempt;

	struct iw_range	range;
};


static void
nm_device_802_11_mesh_olpc_init (NMDevice80211MeshOLPC * self)
{
	self->priv = NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE (self);
	self->priv->dispose_has_run = FALSE;
	self->priv->is_initialized = FALSE;
}

guint32 get_random_channel (void)
{
	GRand * rand;
	guint32 num;

	rand = g_rand_new ();
 	if (!rand) {
		srandom ((int) time (NULL));
		num = random ();
	} else {
		num = g_rand_int (rand);
		g_rand_free (rand);
	}

	/* Return a random channel of [1, 6, 11] */
	return ((num % 3) * 5) + 1;
}

static void
real_init (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	struct iwreq wrq;
	NMSock * sk;

	self->priv->is_initialized = TRUE;
	self->priv->capabilities = 0;
	self->priv->channel = get_random_channel ();
	self->priv->num_channels_tried = 0;
	self->priv->attempt = 1;

	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
	if (sk) {
		memset (&wrq, 0, sizeof (wrq));
		strncpy (wrq.ifr_name, nm_device_get_iface (dev), IFNAMSIZ);
		wrq.u.data.pointer = (caddr_t) &(self->priv->range);
		wrq.u.data.length = sizeof (struct iw_range);
		if (ioctl (nm_dev_sock_get_fd (sk), SIOCGIWRANGE, &wrq) < 0) {
			nm_warning ("%s: couldn't get range information.",
			            nm_device_get_iface (dev));
		}
		nm_dev_sock_close (sk);
	} else {
		nm_warning ("%s: couldn't open device socket.",
			    nm_device_get_iface (dev));
	}
}

static gboolean
ethdev_scan_approval_hook (NMDevice80211Wireless *ethdev,
                           gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_val_if_fail (ethdev != NULL, TRUE);
	g_return_val_if_fail (self != NULL, TRUE);

	if (nm_device_is_activating (NM_DEVICE (self)))
		return FALSE;

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

	self->priv->ethdev.scanning = TRUE;
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

	self->priv->ethdev.scanning = FALSE;

	if (nm_device_is_activating (NM_DEVICE (self))) {
		NMActRequest * req = nm_device_get_act_request (NM_DEVICE (self));
		if (nm_act_request_get_stage (req) == NM_ACT_STAGE_DEVICE_PREPARE) {
			nm_device_activate_schedule_stage2_device_config (req);
		}
	}
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

	ethdev_rec->scanning = FALSE;

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

	if (self->priv->ethdev.dev)
		nm_device_set_active_link (NM_DEVICE (self), TRUE);
}

static gboolean
link_active_cb (gpointer user_data)
{
	nm_device_set_active_link (NM_DEVICE (user_data), TRUE);
	return FALSE;
}

static void
real_notify_device_added (NMDevice *dev,
                          NMDevice *added_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	const char * eth_physdev = NULL;
	GSource * source;

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

	/* schedule a link change for later since this function
	 * is called with the device list lock held.
	 */
	source = g_idle_source_new ();
	if (source) {
		g_source_set_callback (source, link_active_cb, self, NULL);
		g_source_attach (source, nm_device_get_main_context (NM_DEVICE (self)));
		g_source_unref (source);
	}
}

static gboolean
link_inactive_cb (gpointer user_data)
{
	nm_device_set_active_link (NM_DEVICE (user_data), FALSE);
	return FALSE;
}

static void
real_notify_device_removed (NMDevice *dev,
                            NMDevice *removed_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	GSource * source;

	if (dev == removed_dev)
		return;

	if (!self->priv->ethdev.dev)
		return;

	if (   !nm_device_is_802_11_wireless (removed_dev)
	    || (self->priv->ethdev.dev != NM_DEVICE_802_11_WIRELESS (removed_dev)))
		return;

	cleanup_ethdev (self);

	/* schedule a link change for later since this function
	 * is called with the device list lock held.
	 */
	source = g_idle_source_new ();
	if (source) {
		g_source_set_callback (source, link_inactive_cb, self, NULL);
		g_source_attach (source, nm_device_get_main_context (NM_DEVICE (self)));
		g_source_unref (source);
	}
}

static void
real_deactivate_quickly (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	mpp_discovery_cleanup (self);
#if USE_AUTOIP
	aipd_cleanup (self);
#endif
	mesh_search_cleanup (self);
}


static void
real_deactivate (NMDevice *dev)
{
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


static int
get_80211_channel (NMDevice80211MeshOLPC *self)
{
	NMSock * sk;
	int err;
	double freq = 0;
	int channel = 0;
	struct iwreq wrqu;
	const char * iface;

	g_return_val_if_fail (self != NULL, -1);

	iface = nm_device_get_iface (NM_DEVICE (self));
	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", iface);
		return -1;
	}

	err = iw_get_ext (nm_dev_sock_get_fd (sk), iface, SIOCGIWFREQ, &wrqu);
	if (err) {
		nm_warning ("%s: failed to get frequency (errno: %d).",
		            iface,
		            errno);
		goto out;
	}

	freq = iw_freq2float (&wrqu.u.freq);
	channel = iw_freq_to_channel (freq, &(self->priv->range));
	if (channel == -1) {
		/* Already a channel # */
		channel = (int) freq;
	}

out:
	nm_dev_sock_close (sk);
	return channel;
}

static gboolean
set_80211_channel (NMDevice80211MeshOLPC *self,
                   int channel)
{
	NMSock * sk;
	int err;
	struct iwreq wrqu;
	gboolean success = FALSE;
	int skfd;
	const char * iface;

	g_return_val_if_fail (self != NULL, FALSE);

	if ((channel > 0) && (get_80211_channel (self) == channel))
		return TRUE;

	iface = nm_device_get_iface (NM_DEVICE (self));

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.", iface);
		return FALSE;
	}

	if (channel == 0) {
		/* Try 0 for "auto" first, fall back to -1 below of 0 doesn't work */
		wrqu.u.freq.m = 0;
		wrqu.u.freq.e = 0;
		wrqu.u.freq.flags = 0;
	} else {
		/* Fixed */
		wrqu.u.freq.flags = IW_FREQ_FIXED;
		wrqu.u.freq.e = 0;
		wrqu.u.freq.m = channel;
	}

	skfd = nm_dev_sock_get_fd (sk);
	err = iw_set_ext (skfd, iface, SIOCSIWFREQ, &wrqu);
	if (err) {
		if ((channel == 0) && ((errno == EINVAL) || (errno == EOPNOTSUPP))) {
			/* Ok, try "auto" the other way */
			wrqu.u.freq.m = -1;
			wrqu.u.freq.e = 0;
			wrqu.u.freq.flags = 0;
			err = iw_set_ext (skfd, iface, SIOCSIWFREQ, &wrqu);
			if (err) {
				nm_warning ("%s: failed to set frequency to auto (errno: %d)",
				            iface, errno);
				goto out;
			}
		} else {
			nm_warning ("%s: failed to set channel to %d (errno: %d)",
			            iface, channel, errno);
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

#if USE_AUTOIP

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

	if (self->priv->aipd.pid > 0) {
		kill (self->priv->aipd.pid, SIGKILL);
		self->priv->aipd.pid = -1;
	}

	if (self->priv->aipd.watch) {
		g_source_destroy (self->priv->aipd.watch);
		self->priv->aipd.watch = NULL;
	}

	aipd_remove_timeout (self);

	self->priv->aipd.ip4_addr = 0;
}

static gboolean
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

	return FALSE;
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
	if (self->priv->aipd.watch) {
		g_source_destroy (self->priv->aipd.watch);
		g_source_unref (self->priv->aipd.watch);
	}

	self->priv->aipd.watch = g_child_watch_source_new (pid);
	if (!self->priv->aipd.watch)
		return FALSE;
	g_source_set_callback (self->priv->aipd.watch,
						   (GSourceFunc) aipd_watch_cb,
						   self,
						   NULL);
	g_source_attach (self->priv->aipd.watch, nm_device_get_main_context (NM_DEVICE (self)));
	g_source_unref (self->priv->aipd.watch);

	return TRUE;
}

static gboolean
aipd_timeout_cb (gpointer user_data)
{
	NMDevice * dev = NM_DEVICE (user_data);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);

	g_return_val_if_fail (self != NULL, FALSE);

	if (!self->priv->aipd.timeout)
		return FALSE;

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

	/* Set up a timeout to kill it after 20 seconds */
	self->priv->aipd.timeout = g_timeout_source_new (20000);
	if (!self->priv->aipd.timeout)
		goto out;

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

#endif /* USE_AUTOIP */

/*************************************************************/
/* Mesh Search stuff                                         */
/*************************************************************/

static void
mesh_search_cleanup (NMDevice80211MeshOLPC * self)
{
	if (self->priv->msearch.check) {
		g_source_destroy (self->priv->msearch.check);
		g_source_unref (self->priv->msearch.check);
		self->priv->msearch.check = NULL;
	}

	self->priv->msearch.check_tries = 0;

	if (self->priv->msearch.fwt) {
		g_hash_table_unref (self->priv->msearch.fwt);
		self->priv->msearch.fwt = NULL;
	}
}

#define WLANIOCTL                       SIOCIWFIRSTPRIV
#define WLAN_SET128CHAR_GET128CHAR      (WLANIOCTL + 25)
#define WLAN_SUBCMD_FWT_LIST                    25

static gboolean
mesh_search_get_fwt (NMDevice80211MeshOLPC * self, GSList ** list)
{
	NMDevice * dev = NM_DEVICE (self);
	NMSock * sk;
	int num = 0;
	const char * iface;
	gboolean success = TRUE;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	iface = nm_device_get_iface (dev);

	sk = nm_dev_sock_open (dev, DEV_WIRELESS, __func__, NULL);
	if (!sk)
		return FALSE;

	while (num < 500) {
		char buf[200];
		struct iwreq wrq;
		int err, len, i = 0, nonzero = 0;
		char * item;

		memset (buf, 0, sizeof (buf));
		memset (&wrq, 0, sizeof (wrq));
		strncpy (wrq.ifr_name, iface, IFNAMSIZ);
		wrq.u.data.pointer = (caddr_t) &buf[0];
		wrq.u.data.length = sizeof (buf) - 1;
		wrq.u.data.flags = WLAN_SUBCMD_FWT_LIST;
		err = ioctl (nm_dev_sock_get_fd (sk), WLAN_SET128CHAR_GET128CHAR, &wrq);
		if (err < 0) {
			nm_warning ("%s: couldn't get FWT list.  error %d", iface, err);
			if (i == 0)
				success = FALSE;
			break;
		}
		len = wrq.u.data.length;

		/* Check if we're done */
		if ((len == 0) || !strcmp (buf, " (null)") || !strlen (buf))
			break;
		while ((i < len) && ((buf[i] == ' ') || (buf[i] == ':') || (buf[i] == '0')))
 			i++;
		if (i >= len)
			break; /* All zeros, we're done too */

		if (len < 35) {
			nm_warning ("%s: FWT entry too short.", iface);
			continue;
		}

		buf[sizeof (buf) - 1] = '\0';
		item = g_strdup (buf);
		if (!item) {
			nm_warning ("%s: not enough memory for FWT entry.", iface);
			continue;
		}
		num++;
		*list = g_slist_append (*list, item);
	}

out:
	nm_dev_sock_close (sk);
	return success;
}

static gboolean
mesh_search_check_cb (gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMDevice * dev = NM_DEVICE (user_data);
	GSList * list = NULL;
	GSList * elt;
	gboolean again = TRUE;
	NMActRequest * req;

	req = nm_device_get_act_request (dev);
	if (    !req
	    || (nm_act_request_get_stage (req) != NM_ACT_STAGE_DEVICE_CONFIG)
		|| nm_device_activation_should_cancel (dev)) {
		/* Must have been cancelled */
		return FALSE;
	}

	self->priv->msearch.check_tries++;
	if (self->priv->msearch.check_tries > 5) {
		nm_info ("Activation (%s/mesh) Stage 2 of 6 (Device Configure) no mesh "
		         "found on channel %d.",
		         nm_device_get_iface (dev),
		         self->priv->channel);
		mesh_search_cleanup (self);
		nm_policy_schedule_activation_failed (req);
		return FALSE;
	}

	if (!mesh_search_get_fwt (self, &list)) {
		nm_warning ("%s: could not get FWT list.", nm_device_get_iface (dev));
		goto out;
	}

	/* If there's even one entry in the table, we have a mesh */
	if (list && list->data && strlen (list->data)) {
		again = FALSE;
		mesh_search_cleanup (self);
		nm_device_activate_schedule_stage3_ip_config_start (req);
	}

#if 0
	/* Match all returned FWT entries with ones in the saved table.  If there
	 * are any entries _not_ in the saved table, there's a mesh on this channel.
	 */
	for (elt = list; elt; elt = g_slist_next (elt)) {
		if (!g_hash_table_lookup (self->priv->msearch.fwt, elt->data)) {
			again = FALSE;
			mesh_search_cleanup (self);
			nm_device_activate_schedule_stage3_ip_config_start (req);
			break;
		}
	}
#endif

	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);

out:
	return again;
}

#define WLAN_SETNONE_GETNONE	        (WLANIOCTL + 8)
#define WLAN_SUBCMD_FWT_RESET			14

static gboolean
mesh_search_fwt_reset (NMDevice80211MeshOLPC * self)
{
	NMSock * sk;
	struct iwreq wrq;
	int err;
	const char * iface;
	gboolean success = TRUE;

	g_return_val_if_fail (self != NULL, FALSE);

	iface = nm_device_get_iface (NM_DEVICE (self));

	sk = nm_dev_sock_open (NM_DEVICE (self), DEV_WIRELESS, __func__, NULL);
	if (!sk)
		return FALSE;

	memset (&wrq, 0, sizeof (wrq));
	strncpy (wrq.ifr_name, iface, IFNAMSIZ);
	wrq.u.data.flags = WLAN_SUBCMD_FWT_RESET;
	err = ioctl (nm_dev_sock_get_fd (sk), WLAN_SETNONE_GETNONE, &wrq);
	if (err < 0) {
		nm_warning ("%s: couldn't reset FWT.  error %d", iface, err);
		goto out;
	}
	success = TRUE;

out:
	nm_dev_sock_close (sk);
	return success;
}

static gboolean
mesh_search_begin (NMDevice80211MeshOLPC * self)
{
	GSList * list = NULL;
	GSList * elt;
	gboolean success = FALSE;
	GMainContext * context;

	g_return_val_if_fail (self != NULL, FALSE);

	mesh_search_cleanup (self);

	if (!mesh_search_fwt_reset (self)) {
		nm_warning ("%s: couldn't clear mesh FWT.",
		            nm_device_get_iface (NM_DEVICE (self)));
		goto out;
	}

#if 0
	if (!mesh_search_get_fwt (self, &list))
		return FALSE;

	self->priv->msearch.fwt = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (!self->priv->msearch.fwt) {
		nm_warning ("%s: not enough memory for mesh search table.",
		            nm_device_get_iface (NM_DEVICE (self)));
		g_slist_foreach (list, (GFunc) g_free, NULL);
		goto out;
	}

	/* Walk the list and add items to the hash table */
	for (elt = list; elt; elt = g_slist_next (elt)) {
		g_hash_table_insert (self->priv->msearch.fwt,
		                     (gpointer) elt->data,
		                     GINT_TO_POINTER (1));
	}
#endif

	self->priv->msearch.check = g_timeout_source_new (750);
	if (!self->priv->msearch.check)
		goto out;
	context = nm_device_get_main_context (NM_DEVICE (self));
	g_source_set_callback (self->priv->msearch.check,
						   mesh_search_check_cb,
						   self,
						   NULL);
	g_source_attach (self->priv->msearch.check, context);
	success = TRUE;

out:
#if 0
	g_slist_free (list);
#endif
	return success;
}


/*************************************************************/


static NMActStageReturn
real_act_stage1_prepare (NMDevice *dev, NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	nm_device_set_active_link (dev, TRUE);

	/* Have to wait until ethdev is done scanning before continuing */
	if (self->priv->ethdev.scanning)
		return NM_ACT_STAGE_RETURN_POSTPONE;

	return NM_ACT_STAGE_RETURN_SUCCESS;
}

static NMActStageReturn
real_act_stage2_config (NMDevice *dev,
                        NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;

	if (!clear_80211_keys (self))
		goto out;

	/* Mesh interface should be in adhoc mode */
	if (!set_80211_mode (self, IW_MODE_ADHOC))
		goto out;

	if (!set_80211_channel (self, self->priv->channel))
		goto out;

#define MESH_SSID "olpc-mesh"
	if (!set_80211_ssid (self, MESH_SSID, strlen (MESH_SSID)))
		goto out;

	nm_info ("Activation (%s/mesh) Stage 2 of 6 (Device Configure) looking for "
	         "a mesh on channel %d.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         self->priv->channel);

	/* Start looking for a mesh */
	if (self->priv->attempt == 1) {
		if (!mesh_search_begin (self))
			goto out;
		ret = NM_ACT_STAGE_RETURN_POSTPONE;
	} else {
		ret = NM_ACT_STAGE_RETURN_SUCCESS;
	}

out:
	return ret;
}

static NMActStageReturn
real_act_stage3_ip_config_start (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	NMData *			data = NULL;
	const char * iface = nm_device_get_iface (dev);

	data = nm_act_request_get_data (req);
	g_assert (data);

#if USE_AUTOIP
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
#else
	{
		NMDevice80211MeshOLPCClass *	klass;
		NMDeviceClass * parent_class;

		/* Chain up to parent */
		nm_act_request_set_dhcp_timeout_wait (req, MESH_DHCP_TIMEOUT);
		klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (self);
		parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
		ret = parent_class->act_stage3_ip_config_start (dev, req);
	}
#endif

out:
	return ret;
}

static NMActStageReturn
real_act_stage4_ip_config_timeout (NMDevice *self,
                                   NMActRequest *req,
                                   NMIP4Config **config)
{
	g_return_val_if_fail (config != NULL, NM_ACT_STAGE_RETURN_FAILURE);
	g_return_val_if_fail (*config == NULL, NM_ACT_STAGE_RETURN_FAILURE);

	g_assert (req);

	/* No DHCP reply; fail association */
	nm_info ("No DHCP reply received.");
	return NM_ACT_STAGE_RETURN_FAILURE;
}

static NMActStageReturn
real_act_stage4_get_ip4_config (NMDevice *dev,
                                NMActRequest *req,
                                NMIP4Config **config)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	NMIP4Config *			real_config = NULL;

#if USE_AUTOIP
	g_return_val_if_fail (config != NULL, NM_ACT_STAGE_RETURN_FAILURE);
	g_return_val_if_fail (*config == NULL, NM_ACT_STAGE_RETURN_FAILURE);

	real_config = nm_ip4_config_new ();
	nm_ip4_config_set_address (real_config, self->priv->aipd.ip4_addr);
	nm_ip4_config_set_netmask (real_config, (guint32)(ntohl (IPV4LL_NETMASK)));
	nm_ip4_config_set_broadcast (real_config, (guint32)(ntohl (IPV4LL_BROADCAST)));
	nm_ip4_config_set_gateway (real_config, 0);

	ret = NM_ACT_STAGE_RETURN_SUCCESS;
#else
	{
		NMDevice80211MeshOLPCClass *	klass;
		NMDeviceClass * parent_class;

		/* Chain up to parent */
		klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (self);
		parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
		ret = parent_class->act_stage4_get_ip4_config (dev, req, &real_config);
	}
#endif
	*config = real_config;

	return ret;
}


/*************************************************************/
/* MPP discovery stuff                                       */
/*************************************************************/

static void
mpp_discovery_cleanup_timeout_source (NMDevice80211MeshOLPC *self)
{
	if (!self->priv->mpp.timeout_src)
		return;

	g_source_destroy (self->priv->mpp.timeout_src);
	g_source_unref (self->priv->mpp.timeout_src);
	self->priv->mpp.timeout_src = NULL;
}

static void
mpp_discovery_cleanup (NMDevice80211MeshOLPC *self)
{
	g_return_if_fail (self != NULL);

	if (self->priv->mpp.chan) {
		g_io_channel_shutdown (self->priv->mpp.chan, FALSE, NULL);
		g_io_channel_unref (self->priv->mpp.chan);		
		self->priv->mpp.chan = NULL;
	}

	if (self->priv->mpp.chan_src) {
		g_source_destroy (self->priv->mpp.chan_src);
		g_source_unref (self->priv->mpp.chan_src);
		self->priv->mpp.chan_src = NULL;
	}

	if (self->priv->mpp.sk >= 0) {
		close (self->priv->mpp.sk	);
		self->priv->mpp.sk = -1;
	}

	mpp_discovery_cleanup_timeout_source (self);

	self->priv->mpp.tries = 0;
}

static gboolean
interpret_address (NMDevice * dev,
                   const char * line,
                   struct in_addr * addr,
                   const char * errmsg)
{
	const char * iface;

	g_return_val_if_fail (dev != NULL, FALSE);
	g_return_val_if_fail (addr != NULL, FALSE);
	g_return_val_if_fail (errmsg != NULL, FALSE);

	iface = nm_device_get_iface (dev);

	if (!line) {
		nm_info ("Activation (%s/mesh): %s.  Empty reply message line.",
		         iface, errmsg);
		return FALSE;
	}

	if (strlen (line) > 15) {
		nm_info ("Activation (%s/mesh): %s.  Invalid address; too long.",
		          iface, errmsg);
		return FALSE;
	}

	if (inet_aton (line, addr) == 0) {
		nm_info ("Activation (%s/mesh): %s.  Invalid address.", iface, errmsg);
		return FALSE;
	}

	return TRUE;
}

#define MESSAGE_LEN	100

static gboolean
mpp_discovery_receive_cb (GIOChannel *source,
                          GIOCondition condition,
                          gpointer user_data)
{
	NMDevice *			dev = NM_DEVICE (user_data);
	NMDevice80211MeshOLPC *self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMActRequest * 		req;
	char				message[MESSAGE_LEN];
	gsize				bytes_read = 0;
	GError				*error = NULL;
	GIOStatus			status;
	gchar **			lines = NULL;
	const char *		iface = nm_device_get_iface (dev);
	struct in_addr		addr;
	NMIP4Config *		ip4_config;
	NMData *			app_data;

	/* Do nothing if we're supposed to be canceling activation.
	 * We'll get cleaned up by the cancellation handlers later.
	 */
	if (nm_device_activation_should_cancel (dev))
		return TRUE;

	req = nm_device_get_act_request (NM_DEVICE (self));
	if (nm_act_request_get_stage (req) != NM_ACT_STAGE_POST_IP_START)
		return TRUE;

	memset (message, 0, sizeof (message));
	status = g_io_channel_read_chars (self->priv->mpp.chan,
	                                  &message[0],
	                                  sizeof (message),
	                                  &bytes_read,
	                                  &error);
	if (status != G_IO_STATUS_NORMAL) {
		nm_info ("Activation (%s/mesh): error reading from MPP discovery "
		         "socket. status: %d, error: %d '%s'",
		         iface, status,
		         error ? error->code : -1, error ? error->message : "<none>");
		g_error_free (error);
		goto out;
	}

	nm_info ("Activation (%s/mesh): MPP discovery returned '%s'", iface, message);

	lines = g_strsplit (message, "\n", 5);
	if (lines == NULL) {
		nm_info ("Activation (%s/mesh): empty MPPREQ reply.", iface);
		goto out;
	}

	if (!lines[0] || strcmp (lines[0], "IPv4-0")) {
		nm_info ("Activation (%s/mesh): invalid MPPREQ header.", iface);
		goto out;
	}

	ip4_config = nm_device_get_ip4_config (dev);

	if (!interpret_address (dev, lines[1], &addr, "invalid MPPREQ gateway"))
		goto out;
	nm_ip4_config_set_gateway (ip4_config, addr.s_addr);

	if (!interpret_address (dev, lines[2], &addr, "invalid MPPREQ nameserver #1"))
		goto out;
	nm_ip4_config_add_nameserver (ip4_config, addr.s_addr);

	if (lines[3]) {
		/* Ignore whitespace */
		g_strstrip (lines[3]);
		if (strlen (lines[3])) {
			if (!interpret_address (dev, lines[3], &addr, "invalid MPPREQ nameserver #2"))
				goto out;
			nm_ip4_config_add_nameserver (ip4_config, addr.s_addr);
		}
	}

	/* Only cancel timeout now, so that errors above trigger retries */
	mpp_discovery_cleanup_timeout_source (self);

	app_data = nm_device_get_app_data (dev);
	nm_named_manager_remove_ip4_config (app_data->named_manager, ip4_config);
	if (!nm_system_device_set_from_ip4_config (dev)) {
		nm_info ("Activation (%s/mesh): failed to set IP4 config.", iface);
		nm_policy_schedule_activation_failed (req);
		goto out;
	}

	/* Clean up to ensure we never process more than one MPP response */
	mpp_discovery_cleanup (self);
	nm_policy_schedule_activation_finish (req);

out:
	g_strfreev (lines);
	return TRUE;
}

static void
mpp_discovery_receive_done (gpointer user_data)
{
	NMDevice80211MeshOLPC *self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	self->priv->mpp.chan_src = NULL;
}

static gboolean
mpp_discovery_rreq_timeout_cb (gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMActRequest * req;

	self->priv->mpp.tries++;

	req = nm_device_get_act_request (NM_DEVICE (self));
	if (!req || (nm_act_request_get_stage (req) != NM_ACT_STAGE_POST_IP_START)) {
		/* Must have been cancelled */
		mpp_discovery_cleanup (self);
		return FALSE;
	}

	if (self->priv->mpp.tries < 5) {
		mpp_discovery_cleanup_timeout_source (self);
		if (!mpp_discovery_send_rreq (self))
			nm_policy_schedule_activation_failed (req);
	} else {
		NMActRequest * req = nm_device_get_act_request (NM_DEVICE (self));
		mpp_discovery_cleanup (self);
		nm_policy_schedule_activation_failed (req);
	}

	return TRUE;
}

#define MPPREQ_MSG	"MPPREQ"
#define MPPREQ_PORT	16
#define MPPREQ_IP4	"172.31.255.254"
#define MPPREQ_MAC	"c027c027c027"

static gboolean
mpp_discovery_send_rreq (NMDevice80211MeshOLPC *self)
{
	const char * msg = MPPREQ_MSG;
	const char * iface;
	struct sockaddr_in sin;
	int ret;
	gboolean success = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (self->priv->mpp.sk >= 0, FALSE);

	iface = nm_device_get_iface (NM_DEVICE (self));

	memset (&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons (MPPREQ_PORT);
	if (!inet_aton (MPPREQ_IP4, &sin.sin_addr)) {
		nm_info ("Error converting " MPPREQ_IP4 " to numeric address.");
		goto done;
	}

	ret = sendto (self->priv->mpp.sk, msg, strlen (msg), 0,
	              (struct sockaddr *) &sin, sizeof (sin));
	if (ret < 0) {
		nm_info ("Activation (%s/mesh): could not send route request. errno: %d.",
		         iface, errno);
		goto done;
	} else if (ret < strlen (msg)) {
		nm_info ("Activation (%s/mesh): couldn't send entire route request "
		         "message.  Sent %d bytes.",
		         iface, ret);
		goto done;
	}

	nm_info ("Activation (%s/mesh): sent route request #%d.",
	         iface,
	         self->priv->mpp.tries + 1);

	mpp_discovery_cleanup_timeout_source (self);
	self->priv->mpp.timeout_src = g_timeout_source_new (2000);
	if (!self->priv->mpp.timeout_src)
		goto done;
	g_source_set_callback (self->priv->mpp.timeout_src,
	                       mpp_discovery_rreq_timeout_cb,
	                       self,
	                       NULL);
	g_source_attach (self->priv->mpp.timeout_src,
	                 nm_device_get_main_context (NM_DEVICE (self)));
	success = TRUE;

done:
	return success;
}

static gboolean
mpp_discovery_start (NMDevice80211MeshOLPC *self)
{
	struct timeval tv = { 2, 0 };
	struct sockaddr_in sin;
	int opt = 1;
	NMIP4Config * ip4_config;
	const char * iface;

	g_return_val_if_fail (self != NULL, FALSE);

	mpp_discovery_cleanup (self);

	ip4_config = nm_device_get_ip4_config (NM_DEVICE (self));
	g_return_val_if_fail (ip4_config != NULL, FALSE);

	iface = nm_device_get_iface (NM_DEVICE (self));
	nm_info ("Activation (%s/mesh): starting MPP discovery...", iface);

	/* Open the MPP discovery socket */
	self->priv->mpp.sk = socket (AF_INET, SOCK_DGRAM, 0);
	if (self->priv->mpp.sk < 0) {
		nm_info ("Activation (%s/mesh): could not open socket for MPP "
		         "discovery. errno: %d.",
		         iface,
		         errno);
		goto error;
	}

	if (setsockopt (self->priv->mpp.sk, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0) {
		nm_info ("Activation (%s/mesh): error setting socket options for MPP "
		         "discovery. errno: %d.",
		         iface,
		         errno);
		goto error;
	}

	/* Set send & receive timeouts */
	if (setsockopt(self->priv->mpp.sk, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
		nm_info ("Activation (%s/mesh): error setting socket send timeout for MPP "
		         "discovery. errno: %d.",
		         iface,
		         errno);
		goto error;
	}
	if (setsockopt(self->priv->mpp.sk, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		nm_info ("Activation (%s/mesh): error setting socket receive for MPP "
		         "discovery. errno: %d.",
		         iface,
		         errno);
		goto error;
	}

	memset (&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	if (bind(self->priv->mpp.sk, (struct sockaddr*)&sin, sizeof (sin)) < 0) {
		nm_info ("Activation (%s/mesh): error binding socket for MPP "
		         "discovery. errno: %d.",
		         iface,
		         errno);
		goto error;
	}

	/* Wait for an MPP reply */
	self->priv->mpp.chan = g_io_channel_unix_new (self->priv->mpp.sk);
	g_io_channel_set_flags (self->priv->mpp.chan, G_IO_FLAG_NONBLOCK, NULL);
	self->priv->mpp.chan_src = g_io_create_watch (self->priv->mpp.chan, G_IO_IN | G_IO_ERR);
	if (!self->priv->mpp.chan_src)
		goto error;
	g_source_set_callback (self->priv->mpp.chan_src,
						   (GSourceFunc) mpp_discovery_receive_cb,
						   self,
						   mpp_discovery_receive_done);
	g_source_attach (self->priv->mpp.chan_src, nm_device_get_main_context (NM_DEVICE (self)));
	return TRUE;

error:
	mpp_discovery_cleanup (self);
	return FALSE;
}


static NMActStageReturn
real_act_stage6_post_ip_start (NMDevice *dev,
                               NMActRequest *req)
{	
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);

	nm_system_device_add_route_via_device_with_iface (nm_device_get_iface (dev),
			MPPREQ_IP4);

	if (nm_spawn_process ("/sbin/arp -s " MPPREQ_IP4 " " MPPREQ_MAC)) {
		nm_warning ("Activation (%s/mesh): couldn't create anycast ARP"
		            " mapping for MPP discovery.",
		            nm_device_get_iface (dev));
		return NM_ACT_STAGE_RETURN_FAILURE;
	}

	if (!mpp_discovery_start (self))
		return NM_ACT_STAGE_RETURN_FAILURE;

	/* Send first packet */
	if (!mpp_discovery_send_rreq (self))
		return NM_ACT_STAGE_RETURN_FAILURE;

	return NM_ACT_STAGE_RETURN_POSTPONE;
}

static void
real_activation_success_handler (NMDevice *dev,
                                 NMActRequest *req)
{
	NMData * app_data;

	app_data = nm_act_request_get_data (req);
	g_assert (app_data);

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

	self->priv->channel += 5;
	if (self->priv->channel > 11)
		self->priv->channel = 1;

	self->priv->num_channels_tried++;
	if (self->priv->num_channels_tried > 2) {
		/* After the third channel fails, jump to the next attempt */
		self->priv->num_channels_tried = 0;
		self->priv->attempt++;
	}

	if (self->priv->attempt > 2) {
		/* Total failure to get a mesh after 2 passes */
		self->priv->num_channels_tried = 0;
		self->priv->attempt = 1;
		self->priv->channel = get_random_channel ();
		nm_device_set_active_link (dev, FALSE);

		/* Reset to managed mode to make scanning happy */
		set_80211_mode (self, IW_MODE_INFRA);
	}
}

#if USE_AUTOIP
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
		nm_warning ("%s: autoip address %s no longer valid because '%s'.",
		         nm_device_get_iface (dev),
		         addr,
		         event);

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
#endif

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

	parent_class->act_stage1_prepare = real_act_stage1_prepare;
	parent_class->act_stage2_config = real_act_stage2_config;
	parent_class->act_stage3_ip_config_start = real_act_stage3_ip_config_start;
	parent_class->act_stage4_get_ip4_config = real_act_stage4_get_ip4_config;
	parent_class->act_stage4_ip_config_timeout = real_act_stage4_ip_config_timeout;
	parent_class->act_stage6_post_ip_start = real_act_stage6_post_ip_start;
	parent_class->activation_failure_handler = real_activation_failure_handler;
	parent_class->activation_success_handler = real_activation_success_handler;

#if USE_AUTOIP
	parent_class->handle_autoip_event = real_handle_autoip_event;
#endif

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
