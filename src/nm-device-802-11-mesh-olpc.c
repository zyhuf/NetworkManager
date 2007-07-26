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
#include <arpa/inet.h>

#include "nm-device.h"
#include "nm-device-802-11-mesh-olpc.h"
#include "nm-device-private.h"
#include "NetworkManagerUtils.h"
#include "nm-device-802-11-wireless.h"
#include "nm-utils.h"
#include "nm-activation-request.h"
#include "NetworkManagerSystem.h"
#include "NetworkManagerPolicy.h"
#include "nm-dhcp-manager.h"

#define MESH_SSID "olpc-mesh"
#define MPP_DEFAULT_CHANNEL	1

#define IPV4LL_NETWORK 0xA9FE0000L
#define IPV4LL_NETMASK 0xFFFF0000L
#define IPV4LL_HOSTMASK 0x0000FFFFL
#define IPV4LL_BROADCAST 0xA9FEFFFFL

#define MESH_DHCP_TIMEOUT	15	/* in seconds */

#define ETC_DHCLIENT_CONF_PATH SYSCONFDIR"/dhclient.conf"
#define MESH_STEP_FILE SYSCONFDIR"/NetworkManager/mesh-start"
#define MESH_BEACON_FILE SYSCONFDIR"/NetworkManager/mesh-beacons"

static void channel_failure_handler (NMDevice80211MeshOLPC *self, NMActRequest *req);
static void aipd_cleanup (NMDevice80211MeshOLPC *self);
static void mpp_clear_hash_tables (NMDevice80211MeshOLPC *self);
static void mpp_device_activated_cb (GObject *obj, gpointer user_data);
static void mpp_device_deactivated_cb (GObject *obj, gpointer user_data);
static void mpp_cleanup (NMDevice80211MeshOLPC *self);
static gboolean aipd_exec (NMDevice80211MeshOLPC *self);
static gboolean aipd_monitor_start (NMDevice80211MeshOLPC *self);
static void real_deactivate_quickly (NMDevice *dev);
static void assoc_timeout_cleanup (NMDevice80211MeshOLPC * self);
static gboolean is_mpp_active (NMDevice80211MeshOLPC *self);
static gboolean mpp_autoip_start (NMDevice80211MeshOLPC *self);
static gboolean assoc_timeout_start (NMDevice80211MeshOLPC *self);


void
nm_get_anycast_addrs (NMData *data)
{
	char * buf = NULL;
	int fd, len;
	char def_ssaddr[ETH_ALEN] = { 0xC0, 0x27, 0xC0, 0x27, 0xC0, 0x00 };
	char def_xoaddr[ETH_ALEN] = { 0xC0, 0x27, 0xC0, 0x27, 0xC0, 0x01 };
	char ssaddr[ETH_ALEN];
	char xoaddr[ETH_ALEN];

	g_return_if_fail (data != NULL);

	/* Defaults */
	memcpy (data->school_mpp_anycast, def_ssaddr, ETH_ALEN);
	memcpy (data->xo_mpp_anycast, def_xoaddr, ETH_ALEN);

	fd = open (SYSCONFDIR "/NetworkManager/anycast.conf", O_RDONLY);
	if (fd < 0)
		goto out;

#define BUFLEN 40
	buf = calloc (BUFLEN, sizeof (char));
	len = read (fd, buf, BUFLEN - 1);
	if (len < 35) {
		nm_info ("Error: invalid anycast.conf (too short), ignoring.");
		goto out;
	}

#define SSCANF_MAC_FMT "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx"
	len = sscanf (buf, SSCANF_MAC_FMT " " SSCANF_MAC_FMT,
	              &ssaddr[0], &ssaddr[1], &ssaddr[2], &ssaddr[3], &ssaddr[4], &ssaddr[5],
	              &xoaddr[0], &xoaddr[1], &xoaddr[2], &xoaddr[3], &xoaddr[4], &xoaddr[5]);
	if (len != 12) {
		nm_info ("Error: invalid anycast.conf, ignoring.");
		goto out;
	}

	memcpy (data->school_mpp_anycast, ssaddr, ETH_ALEN);
	memcpy (data->xo_mpp_anycast, xoaddr, ETH_ALEN);

out:
	nm_info ("Anycast addresses: server (" MAC_FMT ")  xo (" MAC_FMT ")",
	         MAC_ARG (data->school_mpp_anycast),
	         MAC_ARG (data->xo_mpp_anycast));
	if (fd >= 0)
		close (fd);
	free (buf);
}

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
	gboolean	scanning;
};

struct _mpp {
	/* Device with the primary connection */
	NMDevice *		primary;
	gboolean		associated;

	/* Attach to device activated signals */
	GHashTable *	activated_ids;
	GHashTable *	deactivated_ids;
};

#define MESH_S1_SCHOOL_MPP	1
#define MESH_S2_AP			2
#define MESH_S3_XO_MPP		3
#define MESH_S4_P2P_MESH	4

struct _NMDevice80211MeshOLPCPrivate
{
	gboolean	dispose_has_run;
	gboolean	is_initialized;

	guint32		capabilities;

	gulong		wireless_event_id;
	GSource *	assoc_timeout;

	struct _ethdev		ethdev;
	struct _autoipd		aipd;
	struct _mpp			mpp;

	/* Steps:
	 *
	 * 1. For each channel in [1, 6, 11]:
	 *    a. try DHCP
	 *    b. if DHCP times out, try next channel
	 *    c. if DHCP response includes a non-link-local IP address,
	 *      then CONNECTED/DONE
	 *
	 * 2. Try last successful AP connection
	 *    a. if success then CONNECTED/DONE
	 *
	 * 3. For each channel in [1...14]
	 *    a. try DHCP
	 *    b. if DHCP times out, try next channel
	 *    c. if DHCP response includes a link-local IP address,
	 *      then acquire autoip address and apply DHCP settings (except for
	 *      IP address), CONNECTED/DONE
	 *
	 * 4. Jump to channel 1
	 *    a. acquire autoip address, CONNECTED/DONE
	 */

	guint32 step;	/* 1, 2, 3, or 4 from behavior description above */
	guint32 default_first_step;

	guint32	channel;
	gboolean use_mesh_beacons;

	struct iw_range	range;
};


typedef struct WirelessEventCBData
{
	NMDevice80211MeshOLPC * self;
	NMDevice80211Wireless * ethdev;
	char * data;
	int len;
} WirelessEventCBData;

static void
wireless_event_cb_data_free (WirelessEventCBData *data)
{
	if (!data)
		return;

	g_object_unref (data->self);
	g_object_unref (data->ethdev);
	g_free (data->data);
	g_free (data);
}

static gboolean
handle_association_event (gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	g_return_val_if_fail (self != NULL, FALSE);

	assoc_timeout_cleanup (self);

	nm_info ("%s: got association event from driver.",
	         nm_device_get_iface (NM_DEVICE (self)));

	if (nm_device_is_activating (NM_DEVICE (self))) {
		NMActRequest * req = nm_device_get_act_request (NM_DEVICE (self));
		NMActStage stage;

		if (!req)
			goto out;

		stage = nm_act_request_get_stage (req);
		if (stage != NM_ACT_STAGE_DEVICE_CONFIG)
			goto out;

		nm_device_activate_schedule_stage3_ip_config_start (req);
	} else if (is_mpp_active (self) && !self->priv->mpp.associated) {
		self->priv->mpp.associated = TRUE;
		mpp_autoip_start (self);
	}

out:
	g_object_unref (self);
	return FALSE;
}

static gboolean
wireless_event_helper (gpointer user_data)
{
	NMDevice80211MeshOLPC *	self;
	WirelessEventCBData *	cb_data;
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom, *addr;
	const char badaddr1[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const char badaddr2[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	const char badaddr3[ETH_ALEN] = { 0x44, 0x44, 0x44, 0x44, 0x44, 0x44 };

	cb_data = (WirelessEventCBData *) user_data;
	g_return_val_if_fail (cb_data != NULL, FALSE);
	g_return_val_if_fail (cb_data->data != NULL, FALSE);
	g_return_val_if_fail (cb_data->len >= 0, FALSE);

	self = cb_data->self;
	pos = cb_data->data;
	end = cb_data->data + cb_data->len;

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing.
		 */
		memcpy (&iwe_buf, pos, IW_EV_LCP_LEN);
		if (iwe->len <= IW_EV_LCP_LEN)
			break;

		custom = pos + IW_EV_POINT_LEN;
		memcpy (&iwe_buf, pos, sizeof (struct iw_event));
		custom += IW_EV_POINT_OFF;

		if (iwe->cmd == SIOCGIWAP) {
			addr = iwe->u.ap_addr.sa_data;
			if (   !memcmp (addr, badaddr1, ETH_ALEN)
			    || !memcmp (addr, badaddr2, ETH_ALEN)
			    || !memcmp (addr, badaddr3, ETH_ALEN)) {
				/* disassociated */
			} else {
				/* associated */
				GSource * source = g_idle_source_new ();
				if (source) {
nm_info ("%s: Got association; scheduling association handler", nm_device_get_iface (NM_DEVICE (self)));
					g_object_ref (self);
					g_source_set_priority (source, G_PRIORITY_HIGH_IDLE);
					g_source_set_callback (source, handle_association_event, self, NULL);
					g_source_attach (source, nm_device_get_main_context (NM_DEVICE (self)));
					g_source_unref (source);
				}
			}
		}
		pos += iwe->len;
	}

	return FALSE;
}

static void
nm_device_802_11_mesh_olpc_wireless_event (NmNetlinkMonitor *monitor,
                                           GObject *obj,
                                           char *data,
                                           int data_len,
                                           NMDevice80211MeshOLPC * self)
{
	GSource *				source;
	WirelessEventCBData *	cb_data;

	g_return_if_fail (obj != NULL);
	g_return_if_fail (self != NULL);

	/* Make sure signal is for our eth dev */
	if (NM_DEVICE (self->priv->ethdev.dev) != NM_DEVICE (obj))
		return;

	cb_data = g_malloc0 (sizeof (WirelessEventCBData));
	if (!cb_data) {
		nm_info ("%s(): couldn't allocate memory for callback data.", __func__);
		goto out;
	}
	cb_data->self = NM_DEVICE_802_11_MESH_OLPC(g_object_ref (G_OBJECT (self)));
	cb_data->ethdev = NM_DEVICE_802_11_WIRELESS (g_object_ref (obj));
	cb_data->data = g_malloc (data_len);
	memcpy (cb_data->data, data, data_len);
	cb_data->len = data_len;

	source = g_idle_source_new ();
	if (!source) {
		nm_info ("%s(): couldn't allocate memory for callback source.", __func__);
		goto out;
	}

	g_source_set_callback (source, (GSourceFunc) wireless_event_helper,
			cb_data, (GDestroyNotify) wireless_event_cb_data_free);
	g_source_attach (source, nm_device_get_main_context (NM_DEVICE (self)));
	g_source_unref (source);

out:
	return;
}


static void
nm_device_802_11_mesh_olpc_init (NMDevice80211MeshOLPC * self)
{
	self->priv = NM_DEVICE_802_11_MESH_OLPC_GET_PRIVATE (self);
	self->priv->dispose_has_run = FALSE;
	self->priv->is_initialized = FALSE;
}

#if 0
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
#endif

static void
real_init (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	struct iwreq wrq;
	NMSock * sk;
	NMData * app_data = nm_device_get_app_data (dev);
	NmNetlinkMonitor * monitor;
	char * automesh_path = NULL;
	char * contents = NULL;
	gboolean success = FALSE;

	self->priv->is_initialized = TRUE;
	self->priv->capabilities = 0;

	self->priv->default_first_step = MESH_S1_SCHOOL_MPP;
	success = g_file_get_contents (MESH_STEP_FILE,
	                               &contents,
	                               NULL,
	                               NULL);
	if (success && contents) {
		contents = g_strstrip (contents);
		if (!strcmp (contents, "school-mpp")) {
			nm_info ("%s: Mesh behavior override: School Server first", nm_device_get_iface (dev));
			self->priv->default_first_step = MESH_S1_SCHOOL_MPP;
		} else if (!strcmp (contents, "infra")) {
			nm_info ("%s: Mesh behavior override: Infrastructure AP first", nm_device_get_iface (dev));
			self->priv->default_first_step = MESH_S2_AP;
		} else if (!strcmp (contents, "xo-mpp")) {
			nm_info ("%s: Mesh behavior override: XO Mesh Portal first", nm_device_get_iface (dev));
			self->priv->default_first_step = MESH_S3_XO_MPP;
		} else if (!strcmp (contents, "local")) {
			nm_info ("%s: Mesh behavior override: link-local only.", nm_device_get_iface (dev));
			self->priv->default_first_step = MESH_S4_P2P_MESH;
		} else {
			nm_info ("%s: Unknown Mesh behavior override '%s', defaulting to School Server.", nm_device_get_iface (dev));
		}
		g_free (contents);
	}
	self->priv->step = self->priv->default_first_step;

	if (g_file_test (MESH_BEACON_FILE, G_FILE_TEST_EXISTS)) {
		nm_info ("%s: Will take advantage of mesh beacons.", nm_device_get_iface (dev));
		self->priv->use_mesh_beacons = TRUE;
	}

	self->priv->channel = 1;
	self->priv->assoc_timeout = NULL;
	self->priv->wireless_event_id = 0;

	self->priv->mpp.activated_ids = g_hash_table_new (g_direct_hash,
	                                                  g_direct_equal);
	self->priv->mpp.deactivated_ids = g_hash_table_new (g_direct_hash,
	                                                    g_direct_equal);
	if (   !self->priv->mpp.activated_ids
	    || !self->priv->mpp.deactivated_ids) {
		nm_warning ("%s: couldn't allocate MPP tables.",
		            nm_device_get_iface (NM_DEVICE (self)));
		mpp_clear_hash_tables (self);
	}

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

	/* Mesh device is interested when there is no best device so that it
	 * can jump to the next step in the meta-activation if needed.
	 */
	nm_policy_add_nbd_notifier (NM_DEVICE (self));

	monitor = app_data->netlink_monitor;
	self->priv->wireless_event_id = 
			g_signal_connect (G_OBJECT (monitor), "wireless-event",
				G_CALLBACK (nm_device_802_11_mesh_olpc_wireless_event), self);

	/* Disable automesh; it's only used for the standalone libertas adapters */
	automesh_path = g_strdup_printf ("/sys/class/net/%s/autostart_enabled",
	                                 nm_device_get_iface (dev));
	if (automesh_path) {
		int fd, ret;

		fd = open (automesh_path, O_RDWR);
		if (fd < 0) {
			nm_info ("%s: couldn't turn off automesh at %s",
			         nm_device_get_iface (dev),
			         automesh_path);
			goto out;
		}
		ret = write (fd, "0", 1);
		if (ret < 1) {
			nm_info ("%s: couldn't turn off automesh: ret %d, errno %d",
			         nm_device_get_iface (dev),
			         ret,
			         errno);
		}
		close (fd);
	}

out:
	g_free (automesh_path);
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

	nm_info ("%s: allowing scan for %s",
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

	nm_info ("%s: scan started event for %s",
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

	nm_info ("%s: scan done event for %s",
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

	ethdev_rec->scanning = FALSE;

	nm_info ("%s: 802.11 companion device %s removed.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (NM_DEVICE (ethdev)));
	g_object_unref (ethdev);
	ethdev_rec->dev = NULL;
}

static void
connect_to_device_signals (NMDevice80211MeshOLPC *self, NMDevice *dev)
{
	guint32 act_id, deact_id;

	g_return_if_fail (self != NULL);
	g_return_if_fail (dev != NULL);

	/* If there wasn't enough memory for the activated/deactivated signal
	 * hash tables, don't connect to the signals.
	 */
	if (!self->priv->mpp.activated_ids || !self->priv->mpp.deactivated_ids)
		return;

	/* Attach to the activation success & deactivated signals of every
	 * device known about so far.
	 */
	act_id = g_signal_connect (G_OBJECT (dev),
	                           "activation-success",
	                           G_CALLBACK (mpp_device_activated_cb),
	                           self);
	g_hash_table_insert (self->priv->mpp.activated_ids,
	                     dev,
	                     GUINT_TO_POINTER (act_id));

	deact_id = g_signal_connect (G_OBJECT (dev),
	                             "deactivated",
	                             G_CALLBACK (mpp_device_deactivated_cb),
	                             self);		
	g_hash_table_insert (self->priv->mpp.activated_ids,
	                     dev,
	                     GUINT_TO_POINTER (deact_id));		
}

static void
real_start (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMData * data = nm_device_get_app_data (dev);
	const char * mesh_physdev = nm_device_get_physical_device_udi (dev);
	GSList * elt;

	g_assert (mesh_physdev);

	nm_lock_mutex (data->dev_list_mutex, __FUNCTION__);

	/* Two things to do...  Find the 802.11 non-mesh device if HAL knows about
	 * it already, and attach to the 'activation success' and 'deactivated'
	 * signals of every device to turn on/off the MPP functionality.
	 */
	for (elt = data->dev_list; elt != NULL; elt = g_slist_next (elt)) {
		NMDevice * otherdev = NM_DEVICE (elt->data);

		/* Find and attach to the companion wireless device */
		if (nm_device_is_802_11_wireless (otherdev)) {
			const char * eth_physdev = nm_device_get_physical_device_udi (otherdev);
			if (eth_physdev) {
				if (strcmp (mesh_physdev, eth_physdev) == 0)
					setup_ethdev (self, NM_DEVICE_802_11_WIRELESS (otherdev));
			}
		}

		/* Connect to the devices' activated and deactivated signals */
		if (otherdev != NM_DEVICE (self))
			connect_to_device_signals (self, otherdev);
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
	GSource * source;

	if (dev == added_dev)
		return;

	/* Find and attach to the companion wireless device */
	if (!self->priv->ethdev.dev && nm_device_is_802_11_wireless (added_dev)) {
		const char * eth_physdev = nm_device_get_physical_device_udi (added_dev);
		if (eth_physdev && !strcmp (mesh_physdev, eth_physdev)) {
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
	}

	/* Connect to the devices' activated and deactivated signals */
	connect_to_device_signals (self, added_dev);
}

static gboolean
link_inactive_cb (gpointer user_data)
{
	nm_device_set_active_link (NM_DEVICE (user_data), FALSE);
	return FALSE;
}

static void
dev_removed_disconnect_from_signal (gpointer key, gpointer value, gpointer user_data)
{
	if (NM_DEVICE (key) == NM_DEVICE (user_data))
		g_signal_handler_disconnect (G_OBJECT (key), GPOINTER_TO_UINT (value));
}

static void
real_notify_device_removed (NMDevice *dev,
                            NMDevice *removed_dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	GSource * source;

	if (dev == removed_dev)
		return;

	if (   self->priv->ethdev.dev
	    && self->priv->ethdev.dev == NM_DEVICE_802_11_WIRELESS (removed_dev)) {
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

	/* Disconnect from devices' activated/deactivated signals */
	if (self->priv->mpp.activated_ids) {
		g_hash_table_foreach (self->priv->mpp.activated_ids,
		                      dev_removed_disconnect_from_signal,
		                      removed_dev);
	}
	if (self->priv->mpp.deactivated_ids) {
		g_hash_table_foreach (self->priv->mpp.deactivated_ids,
		                      dev_removed_disconnect_from_signal,
		                      removed_dev);
	}

	/* If we are an MPP and the removed device was the one providing
	 * the primary connection, stop being an MPP.
	 */
	if (removed_dev == self->priv->mpp.primary)
		mpp_cleanup (self);
}

static void
real_deactivate_quickly (NMDevice *dev)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMDHCPManager * dhcp_mgr = nm_dhcp_manager_get (NULL);

	mpp_cleanup (self);
	aipd_cleanup (self);
	assoc_timeout_cleanup (self);

	if (nm_device_get_use_dhcp (dev)) {
		nm_info ("%s: will stop DHCP", nm_device_get_iface (dev));
		nm_dhcp_manager_request_cancel_transaction (dhcp_mgr,
		                                            nm_device_get_iface (dev),
		                                            FALSE);
	}

	/* Remove any dhclient.conf file we may have created for mshX */
	remove (ETC_DHCLIENT_CONF_PATH);
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
get_80211_mode (NMDevice80211Wireless * dev)
{
	NMSock * sk;
	int mode = -1;
	struct iwreq wrq;
	int err;

	g_return_val_if_fail (dev != NULL, -1);

	/* Force the card into Managed/Infrastructure mode */
	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.",
		            nm_device_get_iface (NM_DEVICE (dev)));
		return -1;
	}

	err = iw_get_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (dev)),
	                  SIOCGIWMODE,
	                  &wrq);
	if (err) {
		nm_warning ("%s: failed to get device mode (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (dev)),
		            errno);
		goto out;
	}

	mode = wrq.u.mode;

out:
	nm_dev_sock_close (sk);
	return mode;
}

static gboolean
set_80211_mode (NMDevice80211Wireless *dev,
                int mode)
{
	NMSock * sk;
	gboolean success = FALSE;
	struct iwreq wrqu;
	int err;

	g_return_val_if_fail (dev != NULL, FALSE);
	g_return_val_if_fail ((mode == IW_MODE_INFRA) || (mode == IW_MODE_ADHOC), FALSE);

	if (get_80211_mode (dev) == mode)
		return TRUE;

	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.",
		            nm_device_get_iface (NM_DEVICE (dev)));
		return FALSE;
	}

	wrqu.u.mode = mode;
	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (dev)),
	                  SIOCSIWMODE,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to set device mode to %d (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (dev)),
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
clear_80211_keys (NMDevice80211Wireless *dev)
{
	NMSock * sk;
	struct iwreq wrqu;
	gboolean success = FALSE;
	int err;

	g_return_val_if_fail (dev != NULL, FALSE);

	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.",
		            nm_device_get_iface (NM_DEVICE (dev)));
		return FALSE;
	}

	wrqu.u.data.pointer = (caddr_t) NULL;
	wrqu.u.data.length = 0;
	wrqu.u.data.flags = IW_ENCODE_DISABLED | IW_ENCODE_NOKEY;

	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (dev)),
	                  SIOCSIWENCODE,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to clear encryption keys (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (dev)),
		            errno);
		goto out;
	}

	success = TRUE;

out:
	nm_dev_sock_close (sk);
	return success;
}

static gboolean 
set_80211_ssid (NMDevice80211Wireless *dev,
                const char *ssid,
                const guint32 ssid_len)
{
	NMSock * sk;
	int err;
	struct iwreq wrqu;
	char * safe_ssid = NULL;
	guint32 safe_len = (ssid_len > IW_ESSID_MAX_SIZE) ? IW_ESSID_MAX_SIZE : ssid_len;
	gboolean success = FALSE;

	g_return_val_if_fail (dev != NULL, FALSE);

	/* Make sure the essid we get passed is a valid size */
	if (!ssid || !ssid_len) {
		safe_len = 0;
	} else {
		safe_ssid = g_malloc0 (IW_ESSID_MAX_SIZE + 1);
		if (!safe_ssid) {
			nm_info ("%s(): couldn't allocate memory for SSID.", __func__);
			goto out;
		}
		memcpy (safe_ssid, ssid, safe_len);
	}

	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
	if (!sk) {
		nm_warning ("%s: failed to open device socket.",
		            nm_device_get_iface (NM_DEVICE (dev)));
		goto out;
	}

	wrqu.u.essid.pointer = (caddr_t) safe_ssid;
	wrqu.u.essid.length	 = safe_len;
	wrqu.u.essid.flags	 = 1;	/* Enable essid on card */
	
	err = iw_set_ext (nm_dev_sock_get_fd (sk),
	                  nm_device_get_iface (NM_DEVICE (dev)),
	                  SIOCSIWESSID,
	                  &wrqu);
	if (err) {
		nm_warning ("%s: failed to set SSID (errno: %d).",
		            nm_device_get_iface (NM_DEVICE (dev)),
		            errno);
		goto free_sock;
	}

	success = TRUE;

free_sock:
	nm_dev_sock_close (sk);

out:
	g_free (safe_ssid);
	return success;
}


static int
get_80211_channel (NMDevice80211Wireless *dev, struct iw_range * range)
{
	NMSock * sk;
	int err;
	double freq = 0;
	int channel = 0;
	struct iwreq wrqu;
	const char * iface;

	g_return_val_if_fail (dev != NULL, -1);

	iface = nm_device_get_iface (NM_DEVICE (dev));
	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
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
	channel = iw_freq_to_channel (freq, range);
	if (channel == -1) {
		/* Already a channel # */
		channel = (int) freq;
	}

out:
	nm_dev_sock_close (sk);
	return channel;
}

static gboolean
set_80211_channel (NMDevice80211Wireless *dev,
                   int channel,
                   struct iw_range * range)
{
	NMSock * sk;
	int err;
	struct iwreq wrqu;
	gboolean success = FALSE;
	int skfd;
	const char * iface;

	g_return_val_if_fail (dev != NULL, FALSE);

	if ((channel > 0) && (get_80211_channel (dev, range) == channel))
		return TRUE;

	iface = nm_device_get_iface (NM_DEVICE (dev));

	sk = nm_dev_sock_open (NM_DEVICE (dev), DEV_WIRELESS, __func__, NULL);
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
/* Mesh Portal Pointer service stuff                         */
/*************************************************************/

static gboolean
is_mpp_active (NMDevice80211MeshOLPC *self)
{
	g_return_val_if_fail (self != NULL, FALSE);

	return !!(self->priv->mpp.primary);
}

static void
mpp_cleanup (NMDevice80211MeshOLPC *self)
{
	if (self->priv->mpp.primary) {
		NMData * data = nm_device_get_app_data (NM_DEVICE (self));
		if (self->priv->mpp.associated) {
			nm_dbus_schedule_mesh_device_change_signal (data,
			                                            NM_DEVICE (self),
			                                            NM_DEVICE (self->priv->mpp.primary),
			                                            MESH_DOWN);
		}

		g_object_unref (self->priv->mpp.primary);
		self->priv->mpp.primary = NULL;
	}

	/* Remove any device nameservers and domains */
	if (nm_device_get_ip4_config (NM_DEVICE (self)))
		nm_device_set_ip4_config (NM_DEVICE (self), NULL);

	nm_system_device_flush_routes (NM_DEVICE (self));
	nm_system_device_flush_addresses (NM_DEVICE (self));
	nm_device_update_ip4_address (NM_DEVICE (self));

	self->priv->mpp.associated = FALSE;

	/* Remove any dhclient.conf file we may have created for mshX */
	remove (ETC_DHCLIENT_CONF_PATH);
}

static void
mpp_aipd_timeout (NMDevice80211MeshOLPC *self)
{
	g_return_if_fail (self != NULL);

	aipd_cleanup (self);
	mpp_cleanup (self);	
}

static void
mpp_autoip_success (NMDevice80211MeshOLPC *self)
{
	NMIP4Config * config;
	NMData * data;

	g_return_if_fail (self != NULL);
	g_return_if_fail (self->priv->mpp.primary != NULL);

	config = nm_ip4_config_new ();
	nm_ip4_config_set_address (config, self->priv->aipd.ip4_addr);
	nm_ip4_config_set_netmask (config, (guint32)(ntohl (IPV4LL_NETMASK)));
	nm_ip4_config_set_broadcast (config, (guint32)(ntohl (IPV4LL_BROADCAST)));
	nm_ip4_config_set_gateway (config, 0);
	nm_device_set_ip4_config (NM_DEVICE (self), config);

	nm_system_device_set_from_ip4_config (NM_DEVICE (self), TRUE);

	data = nm_device_get_app_data (NM_DEVICE (self));
	nm_dbus_schedule_mesh_device_change_signal (data,
	                                            NM_DEVICE (self),
	                                            NM_DEVICE (self->priv->mpp.primary),
	                                            MESH_READY);
}

static gboolean
mpp_autoip_start (NMDevice80211MeshOLPC *self)
{
	const char * iface = nm_device_get_iface (NM_DEVICE (self));
	gboolean success = FALSE;

	/* Get an autoip address */
	if (aipd_exec (self)) {
		if (aipd_monitor_start (self)) {
			success = TRUE;
		} else {
			aipd_cleanup (self);
			nm_warning ("%s: MPP couldn't monitor avahi-autoipd.", iface);
		}
	} else {
		nm_warning ("%s: MPP couldn't start avahi-autoipd.", iface);
	}
	return success;
}

static void
mpp_device_activated_cb (GObject * obj,
                         gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMDevice * primary_dev = NM_DEVICE (obj);
	const char * iface;

	g_return_if_fail (self != NULL);
	g_return_if_fail (primary_dev != NULL);

	if (self->priv->mpp.primary)
		mpp_cleanup (self);

	self->priv->mpp.primary = primary_dev;
	g_object_ref (self->priv->mpp.primary);

	iface = nm_device_get_iface (NM_DEVICE (self));

	/* If the primary device isn't the companion ethernet device of this
	 * mesh device, then we need to set up other 802.11 things like
	 * channel, ssid, and mode that the eth device normally sets up itself.
	 */
	if (primary_dev == NM_DEVICE (self->priv->ethdev.dev)) {
		self->priv->mpp.associated = TRUE;
		if (!mpp_autoip_start (self))
			goto out;
	} else {
		if (!clear_80211_keys (self->priv->ethdev.dev))
			goto out;

		if (!set_80211_mode (self->priv->ethdev.dev, IW_MODE_ADHOC))
			goto out;

		self->priv->channel = MPP_DEFAULT_CHANNEL;
		if (!set_80211_channel (self->priv->ethdev.dev,
		                        self->priv->channel,
		                        &self->priv->range))
			goto out;

		if (!set_80211_ssid (self->priv->ethdev.dev, MESH_SSID, strlen (MESH_SSID)))
			goto out;

		assoc_timeout_start (self);
	}

	nm_info ("%s: will become MPP for %s on channel %d",
	         iface,
	         nm_device_get_iface (self->priv->mpp.primary),
	         self->priv->channel);

out:
	return;
}

static void
mpp_device_deactivated_cb (GObject * obj,
                           gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);
	NMDevice * primary_dev = NM_DEVICE (obj);

	g_return_if_fail (self != NULL);
	g_return_if_fail (primary_dev != NULL);

	if (primary_dev != self->priv->mpp.primary)
		return;

	nm_info ("%s: will stop being an MPP for %s",
	         nm_device_get_iface (NM_DEVICE (self)),
	         nm_device_get_iface (self->priv->mpp.primary));

	mpp_cleanup (self);
	aipd_cleanup (self);
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

	nm_info ("%s(): called", __func__);

	if (self->priv->aipd.pid > 0) {
		nm_info ("%s(): Will kill avahi-autoipd pid %d",
		         __func__,
		         self->priv->aipd.pid);
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

	return FALSE;
}

static void
aipd_child_setup (gpointer user_data G_GNUC_UNUSED)
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
	char * argv[5];
	GError * error = NULL;
	GPid pid = -1;
	gboolean success = FALSE;

	argv[0] = "/usr/sbin/avahi-autoipd";
	argv[1] = "--script";
	argv[2] = SYSCONFDIR "/NetworkManager/callouts/nm-avahi-autoipd.action";
	argv[3] = (char *) nm_device_get_iface (NM_DEVICE (self));
	argv[4] = NULL;

	success = g_spawn_async ("/", argv, NULL, 0,
	                         &aipd_child_setup, NULL, &pid, &error);
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
	if (!self->priv->aipd.watch) {
		nm_info ("%s(): returning failure pid %d",
		         __func__,
		         self->priv->aipd.pid);
		return FALSE;
	}
	g_source_set_callback (self->priv->aipd.watch,
						   (GSourceFunc) aipd_watch_cb,
						   self,
						   NULL);
	g_source_attach (self->priv->aipd.watch, nm_device_get_main_context (NM_DEVICE (self)));
	g_source_unref (self->priv->aipd.watch);

	nm_info ("%s(): returning success pid %d", __func__, self->priv->aipd.pid);
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
	if (nm_device_is_activating (NM_DEVICE (self))) {
		channel_failure_handler (self, nm_device_get_act_request (dev));
	} else if (is_mpp_active (self)) {
		/* Trying to get IP address for starting MPP */
		mpp_aipd_timeout (self);
	}
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


/*************************************************************/

#define CHAN_MAX 14

static const long frequency_list[CHAN_MAX] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
			2447, 2452, 2457, 2462, 2467, 2472, 2484 };

static double
chan_to_freq (int chan)
{
	g_return_val_if_fail (chan > 0, -1.0);
	g_return_val_if_fail (chan <= CHAN_MAX, -1.0);

	return (double) frequency_list[chan - 1] / 1000.0;
}

static int
get_next_channel (NMDevice80211MeshOLPC * self)
{
	NMAccessPointList * ap_list;
	int inc = 0;
	int ret_chan = -1;
	int next = self->priv->channel;
	gboolean fallback = FALSE;

	switch (self->priv->step) {
		case MESH_S1_SCHOOL_MPP:
			inc = 5;
			break;
		case MESH_S3_XO_MPP:
			inc = 1;
			break;
		default:
			nm_info ("%s: %s():%d unhandled step %d",
			         nm_device_get_iface (NM_DEVICE (self)),
			         __func__,
			         __LINE__,
			         self->priv->step);
			g_assert_not_reached ();
			goto out;
			break;
	}

	/* Unfortunately we can't do the mesh check reliably, because we don't
	 * have good enough scan behavior in the libertas driver, so we're likely
	 * to miss channels that have meshes on them.  We need null packet support
	 * in the firmware so that scanning can use power-save mode tricks to do
	 * full scans, which it currently doesn't do.
	 */
	if (!self->priv->use_mesh_beacons) {
		fallback = TRUE;
		goto out;
	}

	ap_list = nm_device_802_11_wireless_ap_list_get (self->priv->ethdev.dev);
	if (!ap_list) {
		nm_info ("%s: %s():%d no scan results, falling back to next channel",
		         nm_device_get_iface (NM_DEVICE (self)), __func__, __LINE__);
		fallback = TRUE;
		goto out;
	}

	/* Find the next channel with a mesh on it */
	while ((next < CHAN_MAX) && (ret_chan < 1)) {
		NMAPListIter * iter;
		NMAccessPoint * ap;

		next += inc;

		/* Look for a mesh on this channel */
		if (!(iter = nm_ap_list_iter_new (ap_list))) {
			nm_info ("%s: %s():%d couldn't lock scan list, falling back to next channel",
			         nm_device_get_iface (NM_DEVICE (self)), __func__, __LINE__);
			fallback = TRUE;
			goto out;
		}

		while ((ap = nm_ap_list_iter_next (iter))) {
			double freq = nm_ap_get_freq (ap);

			/* If there's a BSS on this channel that's mesh capable, use it */
			if (   (freq > 0)
			    && (chan_to_freq (next) == freq)
			    && (nm_ap_get_capabilities (ap) & NM_802_11_CAP_MESH_OLPC)) {
				ret_chan = next;
nm_info ("%s: %s():%d found mesh on channel %d",
nm_device_get_iface (NM_DEVICE (self)), __func__, __LINE__, ret_chan);
				break;
			}
		}

if (ret_chan < 0) {
nm_info ("%s: %s():%d no mesh on channel %d",
nm_device_get_iface (NM_DEVICE (self)), __func__, __LINE__, next);
		nm_ap_list_iter_free (iter);
}
	}

out:
	/* If there was an error, just pick the next channel */
	if (fallback)
		ret_chan = self->priv->channel += inc;

	if (ret_chan > CHAN_MAX)
		ret_chan = -1;
nm_info ("%s: returning channel %d\n", nm_device_get_iface (NM_DEVICE (self)), ret_chan);
	return ret_chan;
}

static void
channel_failure_handler (NMDevice80211MeshOLPC *self,
                         NMActRequest *req)
{
	gboolean fail = FALSE;
	gboolean reinit_state = FALSE;
	int next_chan = -1;

	g_return_if_fail (self != NULL);
	g_return_if_fail (req != NULL);

	switch (self->priv->step) {
		case MESH_S1_SCHOOL_MPP:
			/* If the last channel we tried was 11 for school server, we failed */
			if (self->priv->channel >= 11) {
				fail = TRUE;
				break;
			}
			next_chan = get_next_channel (self);
			break;
		case MESH_S3_XO_MPP:
			next_chan = get_next_channel (self);
			break;
		case MESH_S4_P2P_MESH:
			fail = TRUE;
			reinit_state = TRUE;
			break;
		default:
			nm_info ("%s: %s():%d unhandled step %d",
			         nm_device_get_iface (NM_DEVICE (self)),
			         __func__,
			         __LINE__,
			         self->priv->step);
			g_assert_not_reached ();
			break;
	}

	/* If the ethdev happened to go away or the new channel is invalid,
	 * break the chain
	 */
nm_info ("%s: step %d, channel %d, next channel %d\n", __func__, self->priv->step, self->priv->channel, next_chan);
	if (!self->priv->ethdev.dev || (next_chan < 1))
{
nm_info ("   will fail");
		fail = TRUE;
}

	if (fail) {
nm_info ("%s: failing activation", __func__);
		nm_policy_schedule_activation_failed (req);
		/* It's now up to the user to pick their connectivity; set device
		 * link state down so it's not chosen automatically.
		 */
		nm_device_set_active_link (NM_DEVICE (self), FALSE);
		if (reinit_state) {
			self->priv->step = self->priv->default_first_step;
			self->priv->channel = 1;
		}
	} else {
		nm_info ("Activation (%s/mesh) failed to find a mesh on channel %d.",
		         nm_device_get_iface (NM_DEVICE (self)),
		         self->priv->channel);
		self->priv->channel = next_chan;
		real_deactivate_quickly (NM_DEVICE (self));
		nm_device_activate_schedule_stage2_device_config (req);
	}
}

static NMActStageReturn
real_act_stage1_prepare (NMDevice *dev, NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	nm_device_set_active_link (dev, TRUE);

	/* If the user requested reassociation on the mesh device,
	 * go back to the the start of the association process.
	 */
	if (nm_act_request_get_user_requested (req)) {
		self->priv->step = self->priv->default_first_step;
		self->priv->channel = 1;
	}

	/* Stop being an MPP if we currently are one */
	if (self->priv->mpp.primary)
		mpp_cleanup (self);

	/* Have to wait until ethdev is done scanning before continuing */
	if (self->priv->ethdev.scanning)
		return NM_ACT_STAGE_RETURN_POSTPONE;

	return NM_ACT_STAGE_RETURN_SUCCESS;
}


static void
assoc_timeout_cleanup (NMDevice80211MeshOLPC * self)
{
	if (!self->priv->assoc_timeout)
		return;

	g_source_destroy (self->priv->assoc_timeout);
	self->priv->assoc_timeout = NULL;
}

static gboolean
assoc_timeout_cb (gpointer user_data)
{
	NMDevice * dev = NM_DEVICE (user_data);
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActRequest * req;
	NMActStage stage;

	g_return_val_if_fail (self != NULL, FALSE);

	if (!self->priv->assoc_timeout)
		goto out;

	assoc_timeout_cleanup (self);

	if (nm_device_is_activating (NM_DEVICE (self))) {
		req = nm_device_get_act_request (NM_DEVICE (self));
		g_return_val_if_fail (req != NULL, FALSE);

		stage = nm_act_request_get_stage (req);
		if (stage != NM_ACT_STAGE_DEVICE_CONFIG)
			goto out;

		nm_info ("Activation (%s/mesh) Stage 2 of 6 (Device Configure) association "
		         "timed out on channel %d.",
		         nm_device_get_iface (NM_DEVICE (self)),
		         self->priv->channel);

		channel_failure_handler (self, req);
	} else if (is_mpp_active (self)) {
		nm_info ("%s: association timed out on channel %d during MPP setup.",
		         nm_device_get_iface (NM_DEVICE (self)),
		         self->priv->channel);
		mpp_cleanup (self);
	}

out:
	return FALSE;
}

static void
assoc_timeout_done (gpointer user_data)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (user_data);

	self->priv->assoc_timeout = NULL;
}

static gboolean
assoc_timeout_start (NMDevice80211MeshOLPC *self)
{
	gboolean success = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);

	self->priv->assoc_timeout = g_timeout_source_new (6000);
	if (!self->priv->assoc_timeout)
		goto out;

	g_source_set_callback (self->priv->assoc_timeout,
						   assoc_timeout_cb,
						   self,
						   assoc_timeout_done);
	g_source_attach (self->priv->assoc_timeout,
	                 nm_device_get_main_context (NM_DEVICE (self)));
	g_source_unref (self->priv->assoc_timeout);
	success = TRUE;

out:
	return success;
}

static NMActStageReturn
real_act_stage2_config (NMDevice *dev,
                        NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	char * label = NULL;

	switch (self->priv->step) {
		case MESH_S1_SCHOOL_MPP:
			label = "1: School Mesh Portal";
			break;
		case MESH_S2_AP:
			label = "2: Infrastructure AP";
			break;
		case MESH_S3_XO_MPP:
			label = "3: XO Mesh Portal";
			break;
		case MESH_S4_P2P_MESH:
			label = "4: Peer-to-Peer Mesh";
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	nm_info ("Activation (%s/mesh) level %s.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         label ? label : "(invalid)");

	if (!clear_80211_keys (self->priv->ethdev.dev))
		goto out;

	if (!set_80211_mode (self->priv->ethdev.dev, IW_MODE_ADHOC))
		goto out;

	if (!set_80211_channel (self->priv->ethdev.dev, self->priv->channel, &self->priv->range)) {
		/* Just fail this channel, not the entire activation */
		channel_failure_handler (self, req);
		ret = NM_ACT_STAGE_RETURN_POSTPONE;
		goto out;
	}

	if (!set_80211_ssid (self->priv->ethdev.dev, MESH_SSID, strlen (MESH_SSID)))
		goto out;

	nm_info ("Activation (%s/mesh) Stage 2 of 6 (Device Configure) looking for "
	         "a mesh on channel %d.",
	         nm_device_get_iface (NM_DEVICE (self)),
	         self->priv->channel);

	assoc_timeout_start (self);

	ret = NM_ACT_STAGE_RETURN_POSTPONE;

out:
	return ret;
}


#define BUF_SIZE 100

static NMActStageReturn
setup_for_mpp (NMDevice80211MeshOLPC * self,
               NMActRequest * req,
               unsigned char mac[])
{
	NMDevice80211MeshOLPCClass *	klass;
	NMDeviceClass *		parent_class;
	NMActStageReturn	ret = NM_ACT_STAGE_RETURN_FAILURE;
	const char *		iface = nm_device_get_iface (NM_DEVICE (self));
	char				buf[BUF_SIZE];
	int					fd, written;

	snprintf (buf, BUF_SIZE,
	          "interface \"%s\" {\n"
	          "  initial-interval 1;\n"
	          "  anycast-mac ethernet " MAC_FMT ";\n"
	          "}\n",
	          iface,
	          MAC_ARG (mac));

	fd = open (ETC_DHCLIENT_CONF_PATH, O_RDWR | O_CREAT | O_TRUNC);
	if (fd < 0) {
		nm_warning ("Couldn't open /etc/hosts: %s", strerror (errno));
		goto out;
	}

	/* write it out */
	written = write (fd, buf, strlen (buf));
	if (written < 0) {
		nm_warning ("Error writing dhclient.conf: %s", strerror (errno));
		goto close;
	} else if (written != strlen (buf)) {
		nm_warning ("Error writing dhclient.conf: output truncated.");
		goto close;
	}

	/* Chain up to parent */
	nm_act_request_set_dhcp_timeout_wait (req, MESH_DHCP_TIMEOUT);
	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (self);
	parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
	ret = parent_class->act_stage3_ip_config_start (NM_DEVICE (self), req);

close:
	close (fd);
out:
	return ret;
}

static NMActStageReturn
real_act_stage3_ip_config_start (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	const char *			iface = nm_device_get_iface (dev);
	NMData *				data = nm_device_get_app_data (dev);

	switch (self->priv->step) {
		case MESH_S1_SCHOOL_MPP:
			ret = setup_for_mpp (self, req, data->school_mpp_anycast);
			break;
		case MESH_S3_XO_MPP:
			ret = setup_for_mpp (self, req, data->xo_mpp_anycast);
			break;
		case MESH_S4_P2P_MESH:
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
			break;
		default:
			nm_info ("%s: %s():%d unhandled step %d",
			         nm_device_get_iface (NM_DEVICE (self)),
			         __func__,
			         __LINE__,
			         self->priv->step);
			g_assert_not_reached ();
			break;
	}

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

	/* No DHCP reply; next iteration */
	channel_failure_handler (NM_DEVICE_802_11_MESH_OLPC (self), req);
	return NM_ACT_STAGE_RETURN_POSTPONE;
}

static NMActStageReturn
real_act_stage4_get_ip4_config (NMDevice *dev,
                                NMActRequest *req,
                                NMIP4Config **config)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActStageReturn		ret = NM_ACT_STAGE_RETURN_FAILURE;
	NMIP4Config *			real_config = NULL;
	NMDevice80211MeshOLPCClass *	klass;
	NMDeviceClass *			parent_class;
	const char *			iface = nm_device_get_iface (dev);
	NMDHCPManager *		dhcp_manager = nm_dhcp_manager_get (NULL);

	g_return_val_if_fail (config != NULL, NM_ACT_STAGE_RETURN_FAILURE);
	g_return_val_if_fail (*config == NULL, NM_ACT_STAGE_RETURN_FAILURE);

	switch (self->priv->step) {
		case MESH_S1_SCHOOL_MPP:
			/* Chain up to parent */
			klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (self);
			parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
			ret = parent_class->act_stage4_get_ip4_config (dev, req, &real_config);
			if (ret != NM_ACT_STAGE_RETURN_SUCCESS)
				goto out;
			break;
		case MESH_S3_XO_MPP:
			/* Chain up to parent */
			klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (self);
			parent_class = NM_DEVICE_CLASS (g_type_class_peek_parent (klass));
			ret = parent_class->act_stage4_get_ip4_config (dev, req, &real_config);
			/* Kill dhclient; we don't need it anymore after MPP discovery here
			 * because we're ignoring the returned lease.
			 */
			nm_dhcp_manager_request_cancel_transaction (dhcp_manager,
			                                            nm_device_get_iface (NM_DEVICE (self)),
			                                            TRUE);
			if (ret != NM_ACT_STAGE_RETURN_SUCCESS)
				goto out;
			break;
		case MESH_S4_P2P_MESH:
			real_config = nm_ip4_config_new ();
			if (!real_config) {
				nm_warning ("Not enough memory to create ip4 config.");
				goto out;
			}
			nm_ip4_config_set_address (real_config, self->priv->aipd.ip4_addr);
			nm_ip4_config_set_netmask (real_config, (guint32)(ntohl (IPV4LL_NETMASK)));
			nm_ip4_config_set_broadcast (real_config, (guint32)(ntohl (IPV4LL_BROADCAST)));
			nm_ip4_config_set_gateway (real_config, 0);
			break;
		default:
			nm_info ("%s: %s():%d unhandled step %d",
			         nm_device_get_iface (NM_DEVICE (self)),
			         __func__,
			         __LINE__,
			         self->priv->step);
			g_assert_not_reached ();
			break;
	}

	if (self->priv->step == MESH_S3_XO_MPP) {
		/* Step 3 (XO_MPP) ignores the IP address provided in the DHCP response,
		 * so we still have to do autoip.
		 */
		if (!aipd_exec (self)) {
			nm_warning ("Activation (%s/mesh): couldn't start avahi-autoipd.",
				iface);
			ret = NM_ACT_STAGE_RETURN_FAILURE;
			goto out;
		}
		if (!aipd_monitor_start (self)) {
			nm_warning ("Activation (%s/mesh): couldn't monitor avahi-autoipd.",
				iface);
			ret = NM_ACT_STAGE_RETURN_FAILURE;
			goto out;
		}
		/* Save most of the request for later */
		nm_act_request_set_ip4_config (req, real_config);		
		ret = NM_ACT_STAGE_RETURN_POSTPONE;
	} else {
		*config = real_config;
		ret = NM_ACT_STAGE_RETURN_SUCCESS;
	}

out:
	return ret;
}

static void
real_activation_success_handler (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	nm_device_set_active_link (dev, TRUE);
	if (self->priv->step == MESH_S4_P2P_MESH) {
		nm_system_device_add_route_via_device_with_iface (nm_device_get_iface (dev),
		                                                  "224.0.0.0/4");
	}
}


static void
real_activation_failure_handler (NMDevice *dev,
                                 NMActRequest *req)
{
	NMDevice80211MeshOLPC *	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	nm_device_set_active_link (dev, FALSE);

	self->priv->step++;
	self->priv->channel = 1;
}


static gboolean
apply_autoip_address (NMDevice80211MeshOLPC *self, guint32 addr)
{
	NMIP4Config * ip4_config;
	NMActRequest * req;

	g_return_val_if_fail (self != NULL, FALSE);

	req = nm_device_get_act_request (NM_DEVICE (self));
	g_return_val_if_fail (req != NULL, FALSE);

	ip4_config = nm_act_request_get_ip4_config (req);
	g_return_val_if_fail (ip4_config != NULL, FALSE);

	nm_ip4_config_set_address (ip4_config, addr);
	nm_ip4_config_set_netmask (ip4_config, (guint32)(ntohl (IPV4LL_NETMASK)));
	nm_ip4_config_set_broadcast (ip4_config, (guint32)(ntohl (IPV4LL_BROADCAST)));

	return TRUE;
}

static void
handle_activation_autoip_event (NMDevice80211MeshOLPC *self,
                                NMActRequest *req,
                                const char *event,
                                const char *addr)
{
	const char * iface;
	NMActStage stage;

	g_return_if_fail (self != NULL);
	g_return_if_fail (req != NULL);

	iface = nm_device_get_iface (NM_DEVICE (self));
	stage = nm_act_request_get_stage (req);

	if (strcmp (event, "BIND") == 0) {
		struct in_addr ip;
		int ret;

	 	if (!((stage == NM_ACT_STAGE_IP_CONFIG_START) && (self->priv->step == MESH_S4_P2P_MESH))
	 		&& !((stage == NM_ACT_STAGE_IP_CONFIG_GET) && (self->priv->step == MESH_S3_XO_MPP))) {
			nm_warning ("%s: got avahi-autoip event %s for %s at wrong activation stage.",
			            iface, event, addr);
			return;
	 	}

		ret = inet_aton (addr, &ip);
		if (ret) {
			if ((ntohl(ip.s_addr) & IPV4LL_NETMASK) != IPV4LL_NETWORK) {
				nm_warning ("%s: got invalid autoip address %s (0x%X).",
				            iface, addr, ip.s_addr);
				nm_policy_schedule_activation_failed (req);
			} else {
				/* success */
				self->priv->aipd.ip4_addr = ip.s_addr;
				aipd_remove_timeout (self);

				switch (self->priv->step) {
					case MESH_S3_XO_MPP:
						if (!apply_autoip_address (self, ip.s_addr))
							channel_failure_handler (self, req);
						else
							nm_device_activate_schedule_stage5_ip_config_commit (req);
						break;
					case MESH_S4_P2P_MESH:
						nm_device_activate_schedule_stage4_ip_config_get (req);
						break;
					default:
						nm_info ("%s: %s():%d unhandled step %d",
						         nm_device_get_iface (NM_DEVICE (self)),
						         __func__,
						         __LINE__,
						         self->priv->step);
						g_assert_not_reached ();
						break;
				}
			}
		} else {
			nm_warning ("%s: got invalid autoip address %s.", iface, addr);
			nm_policy_schedule_activation_failed (req);
		}		
	} else {
		nm_warning ("%s: autoip address %s no longer valid because '%s'.",
		            iface, addr, event);

		/* The LL address is gone.  Terminate the connection or fail
		 * activation.
		 */
		if (nm_device_is_activating (NM_DEVICE (self))) {
			nm_policy_schedule_activation_failed (req);
		} else {
			nm_device_set_active_link (NM_DEVICE (self), FALSE);
		}
	}
}

static void
handle_mpp_autoip_event (NMDevice80211MeshOLPC *self,
                         const char *event,
                         const char *addr)
{
	const char * iface;
	gboolean success = FALSE;

	g_return_if_fail (self != NULL);

	iface = nm_device_get_iface (NM_DEVICE (self));

	if (strcmp (event, "BIND") == 0) {
		struct in_addr ip;
		int ret;

		ret = inet_aton (addr, &ip);
		if (ret && ((ntohl(ip.s_addr) & IPV4LL_NETMASK) == IPV4LL_NETWORK)) {
			self->priv->aipd.ip4_addr = ip.s_addr;
			aipd_remove_timeout (self);
			success = TRUE;
		} else {
			nm_warning ("%s: got invalid autoip address %s.", iface, addr);
		}		
	} else {
		nm_warning ("%s: autoip address %s no longer valid because '%s'.",
		            iface, addr, event);
	}

	if (success) {
		mpp_autoip_success (self);
	} else {
		mpp_cleanup (self);
	}
}

static gboolean
handle_no_best_device (gpointer user_data)
{
	NMDevice * dev = NM_DEVICE (user_data);
	NMDevice80211MeshOLPC * self;
	gboolean reactivate = FALSE;

	g_return_val_if_fail (dev != NULL, FALSE);

	self = NM_DEVICE_802_11_MESH_OLPC (dev);

	/* If the next stage is step 3, then we set our link to TRUE and
	 * schedule another device change check so that we may continue with
	 * activation at step 3.
	 */
	if (self->priv->step == MESH_S2_AP) {
		self->priv->step = MESH_S3_XO_MPP;
		reactivate = TRUE;
	} else if (self->priv->step == MESH_S4_P2P_MESH) {
		self->priv->channel = 1;
		reactivate = TRUE;
	}

	if (reactivate) {
		NMData * data = nm_device_get_app_data (dev);
		if (data) {
			nm_device_set_active_link (dev, TRUE);
			nm_policy_schedule_device_change_check (data);
		}
	}

	return FALSE;
}

static void
real_notify_no_best_device (NMDevice *dev)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	GSource * source;

	g_return_if_fail (self != NULL);

	source = g_idle_source_new ();
	if (source) {
		g_source_set_callback (source, handle_no_best_device, dev, NULL);
		g_source_attach (source, nm_device_get_main_context (dev));
		g_source_unref (source);
	}
}


static void
real_handle_autoip_event (NMDevice *dev,
                          const char *event,
                          const char *addr)
{
	NMDevice80211MeshOLPC * self = NM_DEVICE_802_11_MESH_OLPC (dev);
	NMActRequest * req = nm_device_get_act_request (dev);

	nm_info ("%s: got autoip event '%s' for '%s'\n", nm_device_get_iface (NM_DEVICE (self)), event, addr);
	if (nm_device_is_activating (NM_DEVICE (self))) {
		if (req) {
			handle_activation_autoip_event (self, req, event, addr);
		} else {
			nm_warning ("%s: not active but got avahi-autoip event %s for %s.",
			         nm_device_get_iface (NM_DEVICE (self)),
			         event,
			         addr);
		}
	} else if (is_mpp_active (self)) {
		handle_mpp_autoip_event (self, event, addr);
	}
}

static void
clear_hash_disconnect_from_signal (gpointer key, gpointer value, gpointer user_data)
{
	g_signal_handler_disconnect (G_OBJECT (key), GPOINTER_TO_UINT (value));
}

static void
mpp_clear_hash_tables (NMDevice80211MeshOLPC *self)
{
	g_return_if_fail (self != NULL);

	if (self->priv->mpp.activated_ids) {
		g_hash_table_foreach (self->priv->mpp.activated_ids,
		                      clear_hash_disconnect_from_signal,
		                      NULL); 
		g_hash_table_unref (self->priv->mpp.activated_ids);
		self->priv->mpp.activated_ids = NULL;
	}
	if (self->priv->mpp.deactivated_ids) {
		g_hash_table_foreach (self->priv->mpp.deactivated_ids,
		                      clear_hash_disconnect_from_signal,
		                      NULL); 
		g_hash_table_unref (self->priv->mpp.deactivated_ids);
		self->priv->mpp.deactivated_ids = NULL;
	}
}

static void
nm_device_802_11_mesh_olpc_dispose (GObject *object)
{
	NMDevice80211MeshOLPC *		self = NM_DEVICE_802_11_MESH_OLPC (object);
	NMDevice80211MeshOLPCClass *	klass = NM_DEVICE_802_11_MESH_OLPC_GET_CLASS (object);
	NMDeviceClass *			parent_class;
	NMData *				data = nm_device_get_app_data (NM_DEVICE (object));

	/* Make sure dispose does not run twice. */
	if (self->priv->dispose_has_run)
		return;

	self->priv->dispose_has_run = TRUE;

	/* Only do this part of the cleanup if the object is initialized */
	if (self->priv->is_initialized)
		mpp_clear_hash_tables (self);

	g_signal_handler_disconnect (G_OBJECT (data->netlink_monitor),
	                             self->priv->wireless_event_id);

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
	parent_class->activation_failure_handler = real_activation_failure_handler;
	parent_class->activation_success_handler = real_activation_success_handler;

	parent_class->handle_autoip_event = real_handle_autoip_event;
	parent_class->notify_no_best_device = real_notify_no_best_device;

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
