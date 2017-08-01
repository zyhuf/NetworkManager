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

#include "nm-device-openvswitch.h"
#include "nm-ovsdb.h"

#include "devices/nm-device-private.h"
#include "nm-setting-connection.h"
#include "nm-setting-ovs-bridge.h"
#include "nm-setting-ovs-port.h"

#include "introspection/org.freedesktop.NetworkManager.Device.Openvswitch.h"

#include "devices/nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceOpenvswitch);

/*****************************************************************************/

struct _NMDeviceOpenvswitch {
	NMDevice parent;
};

struct _NMDeviceOpenvswitchClass {
	NMDeviceClass parent;
};

G_DEFINE_TYPE (NMDeviceOpenvswitch, nm_device_openvswitch, NM_TYPE_DEVICE)

/*****************************************************************************/

static void
link_changed (NMDevice *device, const NMPlatformLink *pllink)
{
	NMDeviceOpenvswitch *self = NM_DEVICE_OPENVSWITCH (device);

	NM_DEVICE_CLASS (nm_device_openvswitch_parent_class)->link_changed (device, pllink);

	if (pllink && nm_device_get_state (device) == NM_DEVICE_STATE_CONFIG) {
		_LOGD (LOGD_DEVICE, "the link appeared, continuing activation");
		nm_device_activate_schedule_stage2_device_config (device);
	}
}

static void
add_br_cb (GError *error, gpointer user_data)
{
	NMDeviceOpenvswitch *self = user_data;

	if (error) {
		_LOGW (LOGD_DEVICE, "%s", error->message);
		nm_device_state_changed (NM_DEVICE (self),
		                         NM_DEVICE_STATE_FAILED,
		                         NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (self);
}

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    const NMPlatformLink **out_plink,
                    GError **error)
{
	const char *connection_type;

	connection_type = nm_connection_get_connection_type (connection);
	g_return_val_if_fail (connection_type, FALSE);

	if (strcmp (connection_type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0) {
		nm_ovsdb_add_br (nm_ovsdb_get (),
		                 nm_device_get_iface (device),
		                 add_br_cb,
		                 g_object_ref (device));

		/* We don't have a plink yet, since the device is eventually instantiated
		 * by ovs-vswitchd asynchronously. Manager knows and manager is fine with that. */
	} else if (strcmp (connection_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		/* This doesn't really exist, not even in the ovsdb, until an interface is
		 * enslaved. */
	} else {
		g_return_val_if_reached (FALSE);
	}

	return TRUE;
}

static void
del_br_cb (GError *error, gpointer user_data)
{
	NMDeviceOpenvswitch *self = user_data;

	if (error) {
	        _LOGW (LOGD_DEVICE, "%s", error->message);
	        nm_device_state_changed (NM_DEVICE (self),
	                                 NM_DEVICE_STATE_FAILED,
	                                 NM_DEVICE_STATE_REASON_UNKNOWN);
	}

	g_object_unref (self);
}

static gboolean
unrealize (NMDevice *device, GError **error)
{
	nm_ovsdb_del_br (nm_ovsdb_get (),
	                 nm_device_get_iface (device),
	                 del_br_cb,
	                 g_object_ref (device));

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

	if (!NM_DEVICE_CLASS (nm_device_openvswitch_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	s_con = nm_connection_get_setting_connection (connection);
	g_assert (s_con);
	connection_type = nm_setting_connection_get_connection_type (s_con);
	if (!connection_type)
		return FALSE;

	// XXX
	if (strcmp (connection_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0)
		return TRUE;
	if (strcmp (connection_type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0)
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

	// XXX
	if (strcmp (slave_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0)
		return TRUE;
	if (strcmp (slave_type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0)
		return TRUE;

	return FALSE;
}

static NMActStageReturn
act_stage2_config (NMDevice *device, NMDeviceStateReason *out_failure_reason)
{
	NMDeviceOpenvswitch *self = NM_DEVICE_OPENVSWITCH (device);
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
}

static void
update_connection (NMDevice *device, NMConnection *connection)
{
	// XXX
#if 0
	NMSettingOpenvswitch *s_openvswitch = nm_connection_get_setting_openvswitch (connection);

	if (!s_openvswitch) {
		s_openvswitch = (NMSettingOpenvswitch *) nm_setting_openvswitch_new ();
		nm_connection_add_setting (connection, (NMSetting *) s_openvswitch);
	}
#endif
}

static void
add_port_cb (GError *error, gpointer user_data)
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

static gboolean
_get_bridge_port (NMDevice *device, NMDevice *slave, NMConnection *connection,
                  NMDevice **bridge, NMDevice **port)
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

	/* Do nothing if we're just enslaving an empty port to a bridge. */
	if (g_strcmp0 (slave_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		*bridge = NULL;
		*port = NULL;
		return g_strcmp0 (device_type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0;
	}

	if (g_strcmp0 (device_type, NM_SETTING_OVS_BRIDGE_SETTING_NAME) == 0) {
		*bridge = device;
		*port = slave;
	} else if (g_strcmp0 (device_type, NM_SETTING_OVS_PORT_SETTING_NAME) == 0) {
		*bridge = nm_device_get_master (device);
		*port = device;
	} else {
		g_return_val_if_reached (FALSE);
	}

	if (!bridge)
		return FALSE;

	return TRUE;
}

static gboolean
enslave_slave (NMDevice *device, NMDevice *slave, NMConnection *connection, gboolean configure)
{
	NMDevice *bridge = NULL;
	NMDevice *port = NULL;

	if (!configure)
		return TRUE;

	if (!_get_bridge_port (device, slave, connection, &bridge, &port))
		return FALSE;

	if (!bridge && !port)
		return TRUE;

	nm_ovsdb_add_port (nm_ovsdb_get (),
	                   nm_device_get_iface (bridge),
	                   nm_device_get_iface (port),
	                   nm_device_get_iface (slave),
	                   add_port_cb,
	                   g_object_ref (slave));

	return TRUE;
}

static void
del_port_cb (GError *error, gpointer user_data)
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
	NMDevice *bridge = NULL;
	NMDevice *port = NULL;

	if (!configure)
		return;

	if (!_get_bridge_port (device, slave, NULL, &bridge, &port))
		return;

	if (!bridge && !port)
		return;

	nm_ovsdb_del_port (nm_ovsdb_get (),
	                   nm_device_get_iface (bridge),
	                   nm_device_get_iface (port),
	                   nm_device_get_iface (slave),
	                   del_port_cb,
	                   g_object_ref (slave));
}

/*****************************************************************************/

static void
nm_device_openvswitch_init (NMDeviceOpenvswitch *self)
{
}

static void
nm_device_openvswitch_class_init (NMDeviceOpenvswitchClass *klass)
{
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

	NM_DEVICE_CLASS_DECLARE_TYPES (klass, NULL, NM_LINK_TYPE_OPENVSWITCH)

	// XXX
	//device_class->connection_type = NM_SETTING_OVS_PORT_SETTING_NAME;
	device_class->is_master = TRUE;

	device_class->link_changed = link_changed;
	device_class->create_and_realize = create_and_realize;
	device_class->unrealize = unrealize;
	device_class->get_generic_capabilities = get_generic_capabilities;
	device_class->check_connection_compatible = check_connection_compatible;
	device_class->check_slave_connection_compatible = check_slave_connection_compatible;
	device_class->act_stage2_config = act_stage2_config;
	device_class->update_connection = update_connection;
	device_class->enslave_slave = enslave_slave;
	device_class->release_slave = release_slave;

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_OPENVSWITCH_SKELETON,
	                                        NULL);
}
