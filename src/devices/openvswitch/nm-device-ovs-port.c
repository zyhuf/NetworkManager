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
 * Copyright 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-device-ovs-port.h"
#include "nm-ovsdb.h"

#include "devices/nm-device-private.h"
#include "nm-active-connection.h"
#include "nm-setting-connection.h"
#include "nm-setting-ovs-port.h"
#include "nm-setting-ovs-port.h"

#include "introspection/org.freedesktop.NetworkManager.Device.OvsPort.h"

#include "devices/nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceOvsPort);

/*****************************************************************************/

struct _NMDeviceOvsPort {
	NMDevice parent;
};

struct _NMDeviceOvsPortClass {
	NMDeviceClass parent;
};

G_DEFINE_TYPE (NMDeviceOvsPort, nm_device_ovs_port, NM_TYPE_DEVICE)

/*****************************************************************************/

static void
link_changed (NMDevice *device, const NMPlatformLink *pllink)
{
#if 0
	NMDeviceOvsPort *self = NM_DEVICE_OVS_PORT (device);

	NM_DEVICE_CLASS (nm_device_ovs_port_parent_class)->link_changed (device, pllink);

	if (pllink && nm_device_get_state (device) == NM_DEVICE_STATE_CONFIG) {
		_LOGD (LOGD_DEVICE, "the link appeared, continuing activation");
		nm_device_activate_schedule_stage2_device_config (device);
	}
#else
	g_printerr ("PORT: LINK CHANGED\n");
#endif
}

#if 0
static void
add_br_cb (GError *error, gpointer user_data)
{
	NMDeviceOvsPort *self = user_data;

	if (error) {
		_LOGW (LOGD_DEVICE, "%s", error->message);
		nm_device_state_changed (NM_DEVICE (self),
		                         NM_DEVICE_STATE_FAILED,
		                         NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (self);
}
#endif

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    const NMPlatformLink **out_plink,
                    GError **error)
{
#if 0
	const char *connection_type;

	connection_type = nm_connection_get_connection_type (connection);
	g_return_val_if_fail (connection_type, FALSE);

	if (strcmp (connection_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		nm_ovsdb_transact (nm_ovsdb_get (), NM_OVSDB_ADD_BR,
		                   nm_device_get_iface (device), connection, NULL,
		                   add_br_cb, g_object_ref (device));

		/* We don't have a plink yet, since the device is eventually instantiated
		 * by ovs-vswitchd asynchronously. Manager knows and manager is fine with that. */
	} else if (strcmp (connection_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		/* This doesn't really exist, not even in the ovsdb, until an interface is
		 * enslaved. */
	} else {
		g_return_val_if_reached (FALSE);
	}
#endif
	g_printerr ("PORT: CREATE AND REALIZE\n");

	return TRUE;
}

#if 0
static void
del_br_cb (GError *error, gpointer user_data)
{
	NMDeviceOvsPort *self = user_data;

	if (error) {
	        _LOGW (LOGD_DEVICE, "%s", error->message);
	        nm_device_state_changed (NM_DEVICE (self),
	                                 NM_DEVICE_STATE_FAILED,
	                                 NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (self);
}
#endif

static gboolean
unrealize (NMDevice *device, GError **error)
{
#if 0
	nm_ovsdb_transact (nm_ovsdb_get (), NM_OVSDB_DEL_BR,
	                   nm_device_get_iface (device), NULL, NULL,
	                   del_br_cb, g_object_ref (device));
#endif
	g_printerr ("PORT: UNREALIZE\n");

	return TRUE;
}

static NMDeviceCapabilities
get_generic_capabilities (NMDevice *device)
{
	return NM_DEVICE_CAP_CARRIER_DETECT | NM_DEVICE_CAP_IS_SOFTWARE;
}


static gboolean
check_connection_compatible (NMDevice *device, NMConnection *connection)
{
	NMSettingConnection *s_con;
	const char *connection_type;

	if (!NM_DEVICE_CLASS (nm_device_ovs_port_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!connection_type)
		return FALSE;

	if (strcmp (connection_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0)
		return TRUE;

	return FALSE;
}

static gboolean
check_slave_connection_compatible (NMDevice *device, NMConnection *slave)
{
	NMSettingConnection *s_con;
	const char *slave_type;

	s_con = nm_connection_get_setting_connection (slave);
	g_assert (s_con);
	slave_type = nm_setting_connection_get_slave_type (s_con);
	if (!slave_type)
		return FALSE;

	if (strcmp (slave_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0)
		return TRUE;

//	g_printerr ("PORT: CHECK SLAVE COMPAT\n");

	return FALSE;
}

static NMActStageReturn
act_stage3_ip4_config_start (NMDevice *device,
                             NMIP4Config **out_config,
                             NMDeviceStateReason *out_failure_reason)
{
	g_printerr ("PORT: ACT3v4\n");
	return NM_ACT_STAGE_RETURN_IP_FAIL;
}

static NMActStageReturn
act_stage3_ip6_config_start (NMDevice *device,
                             NMIP6Config **out_config,
                             NMDeviceStateReason *out_failure_reason)
{
	g_printerr ("PORT: ACT3v6\n");
	return NM_ACT_STAGE_RETURN_IP_FAIL;
}

#if 0
static NMActStageReturn
act_stage2_config (NMDevice *device, NMDeviceStateReason *out_failure_reason)
{
#if 0
	NMDeviceOvsPort *self = NM_DEVICE_OVS_PORT (device);
	NMConnection *applied_connection;

	applied_connection = nm_device_get_applied_connection (device);
	if (   applied_connection
	    && strcmp (nm_connection_get_connection_type (applied_connection),
	               NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		return NM_ACT_STAGE_RETURN_SUCCESS;
	}

	if (nm_device_get_ifindex (device)) {
		return NM_ACT_STAGE_RETURN_SUCCESS;
	} else {
		_LOGD (LOGD_DEVICE, "the link is not there, waiting for it to appear");
		return NM_ACT_STAGE_RETURN_POSTPONE;
	}
#else
	g_printerr ("PORT: ACT2\n");
	return NM_ACT_STAGE_RETURN_POSTPONE;
#endif
}
#endif


static void
add_iface_cb (GError *error, gpointer user_data)
{
	NMDevice *slave = user_data;

	if (error) {
	        nm_log_warn (LOGD_DEVICE, "device %s could not be added to a ovs port: %s",
		             nm_device_get_iface (slave), error->message);
	        nm_device_state_changed (slave,
	                                 NM_DEVICE_STATE_FAILED,
	                                 NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (slave);
}

#if 0
static gboolean
_get_port_port (NMDevice *device, NMDevice *slave, NMConnection *connection,
                  NMDevice **port, NMDevice **port)
{
	NMConnection *applied_connection;
	const char *device_type;
	const char *slave_type = NULL;

	if (!connection)
		connection = nm_device_get_applied_connection (slave);
	if (connection)
		slave_type = nm_connection_get_connection_type (connection);

	applied_connection = nm_device_get_applied_connection (device);
	if (!applied_connection)
		return FALSE;
	device_type = nm_connection_get_connection_type (applied_connection);

	/* Do nothing if we're just enslaving an empty port to a port. */
	if (g_strcmp0 (slave_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		*port = NULL;
		*port = NULL;
		return g_strcmp0 (device_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0;
	}

	if (g_strcmp0 (device_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		*port = device;
		*port = slave;
	} else if (g_strcmp0 (device_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		*port = nm_device_get_master (device);
		*port = device;
	} else {
		g_return_val_if_reached (FALSE);
	}

	if (!*port)
		return FALSE;

	return TRUE;
}
#endif

static gboolean
enslave_slave (NMDevice *device, NMDevice *slave, NMConnection *connection, gboolean configure)
{
        NMActiveConnection *ac_port = NULL;
        NMActiveConnection *ac_bridge = NULL;

	g_printerr ("PORT: ENSLAVE SLAVE\n");


	if (!configure)
		return TRUE;


        ac_port = NM_ACTIVE_CONNECTION (nm_device_get_act_request (device));
        ac_bridge = nm_active_connection_get_master (ac_port);
        if (!ac_bridge)
                ac_bridge = ac_port;

        nm_ovsdb_add_interface (nm_ovsdb_get (),
                                nm_active_connection_get_applied_connection (ac_bridge),
                                nm_device_get_applied_connection (device),
                                nm_device_get_applied_connection (slave),
                                add_iface_cb, g_object_ref (device));

	return TRUE;
}

static void
del_iface_cb (GError *error, gpointer user_data)
{
	NMDevice *slave = user_data;

	if (error) {
	        nm_log_warn (LOGD_DEVICE, "device %s could not be removed from a ovs port: %s",
		             nm_device_get_iface (slave), error->message);
	        nm_device_state_changed (slave,
	                                 NM_DEVICE_STATE_FAILED,
	                                 NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (slave);
}

static void
release_slave (NMDevice *device, NMDevice *slave, gboolean configure)
{
	g_printerr ("PORT: RELEASE SLAVE\n");

	if (!configure)
		return;

	nm_ovsdb_del_interface (nm_ovsdb_get (), nm_device_get_iface (slave),
				del_iface_cb, g_object_ref (slave));
}

/*****************************************************************************/

static void
nm_device_ovs_port_init (NMDeviceOvsPort *self)
{
}

static void
nm_device_ovs_port_class_init (NMDeviceOvsPortClass *klass)
{
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

//	NM_DEVICE_CLASS_DECLARE_TYPES (klass, NULL, NM_LINK_TYPE_OVS_PORT)

	device_class->is_master = TRUE;
	device_class->link_changed = link_changed;
	device_class->create_and_realize = create_and_realize;
	device_class->unrealize = unrealize;
	device_class->get_generic_capabilities = get_generic_capabilities;
	device_class->check_connection_compatible = check_connection_compatible;
	device_class->check_slave_connection_compatible = check_slave_connection_compatible;
//	device_class->act_stage2_config = act_stage2_config;
	device_class->act_stage3_ip4_config_start = act_stage3_ip4_config_start;
	device_class->act_stage3_ip6_config_start = act_stage3_ip6_config_start;
	device_class->enslave_slave = enslave_slave;
	device_class->release_slave = release_slave;

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_OVS_PORT_SKELETON,
	                                        NULL);
}
