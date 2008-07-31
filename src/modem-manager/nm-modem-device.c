/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include "nm-modem-device.h"
#include "nm-device-private.h"
#include "nm-device-interface.h"
#include "nm-dbus-manager.h"
#include "nm-setting-connection.h"
#include "nm-setting-gsm.h"
#include "nm-setting-cdma.h"
#include "nm-marshal.h"
#include "nm-properties-changed-signal.h"
#include "nm-modem-types.h"
#include "nm-utils.h"
#include "nm-gsm-device-glue.h"

G_DEFINE_TYPE (NMModemDevice, nm_modem_device, NM_TYPE_DEVICE)

#define NM_MODEM_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_MODEM_DEVICE, NMModemDevicePrivate))

enum {
	PROP_0,
	PROP_PATH,
	PROP_TYPE,

	LAST_PROP
};

enum {
	MODEM_STATE_BEGIN,
	MODEM_STATE_ENABLE,
	MODEM_STATE_SET_PIN,
	MODEM_STATE_REGISTER,
	MODEM_STATE_CONNECT
};

typedef struct {
	NMDBusManager *dbus_mgr;
	char *path;
	DBusGProxy *proxy;
	NMPPPManager *ppp_manager;
	NMIP4Config  *pending_ip4_config;

	int modem_state;
	guint state_to_disconnected_id;

	/* PPP stats */
	guint32 in_bytes;
	guint32 out_bytes;
} NMModemDevicePrivate;

enum {
	SIGNAL_QUALITY,
	PPP_STATS,
	PROPERTIES_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

NMModemDevice *
nm_modem_device_new (const char *path,
				 const char *data_device,
				 const char *driver,
				 int device_type)
{
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (data_device != NULL, NULL);
	g_return_val_if_fail (driver != NULL, NULL);


	return (NMModemDevice *) g_object_new (NM_TYPE_MODEM_DEVICE,
								    NM_DEVICE_INTERFACE_UDI, path,
								    NM_DEVICE_INTERFACE_IFACE, data_device,
								    NM_DEVICE_INTERFACE_DRIVER, driver,
								    NM_DEVICE_INTERFACE_MANAGED, TRUE,
								    NM_MODEM_DEVICE_PATH, path,
								    NM_MODEM_DEVICE_TYPE, device_type,
								    NULL);
}

NMPPPManager *
nm_modem_device_get_ppp_manager (NMModemDevice *device)
{
	g_return_val_if_fail (NM_IS_MODEM_DEVICE (device), NULL);

	return NM_MODEM_DEVICE_GET_PRIVATE (device)->ppp_manager;
}

static NMSetting *
modem_device_get_setting (NMModemDevice *device, GType setting_type)
{
	NMActRequest *req;
	NMSetting *setting = NULL;

	req = nm_device_get_act_request (NM_DEVICE (device));
	if (req) {
		NMConnection *connection;

		connection = nm_act_request_get_connection (req);
		if (connection)
			setting = nm_connection_get_setting (connection, setting_type);
	}

	return setting;
}

static void
state_machine_cdma (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMModemDevice *modem = NM_MODEM_DEVICE (user_data);
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (modem);
	NMSettingCdma *setting;
	GError *error = NULL;

	setting = NM_SETTING_CDMA (modem_device_get_setting (modem, NM_TYPE_SETTING_CDMA));

	if (call_id)
		dbus_g_proxy_end_call (proxy, call_id, &error, G_TYPE_INVALID);

	if (error) {
		nm_warning ("CDMA device connection failed: %s", error->message);
		nm_device_state_changed (NM_DEVICE (modem), NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_NONE);
		return;
	}

	switch (priv->modem_state) {
	case MODEM_STATE_BEGIN:
		priv->modem_state = MODEM_STATE_ENABLE;
		dbus_g_proxy_begin_call (proxy, "Enable", state_machine_cdma,
							modem, NULL,
							G_TYPE_BOOLEAN, TRUE,
							G_TYPE_INVALID);
		break;
	case MODEM_STATE_ENABLE:
		priv->modem_state = MODEM_STATE_CONNECT;
		dbus_g_proxy_begin_call (proxy, "Connect", state_machine_cdma,
							modem, NULL,
							G_TYPE_STRING, setting->number,
							G_TYPE_STRING, "",
							G_TYPE_INVALID);
		break;
	case MODEM_STATE_CONNECT:
		nm_device_activate_schedule_stage2_device_config (NM_DEVICE (modem));
		break;
	default:
		nm_warning ("Invalid modem state %d", priv->modem_state);
		nm_device_state_changed (NM_DEVICE (modem), NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_NONE);
		break;
	}
}

static void
state_machine_gsm (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMModemDevice *modem = NM_MODEM_DEVICE (user_data);
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (modem);
	NMSettingGsm *setting;
	const char *secret = NULL;
	const char *secret_name = NULL;
	gboolean retry_secret = FALSE;
	GError *error = NULL;

	setting = NM_SETTING_GSM (modem_device_get_setting (modem, NM_TYPE_SETTING_GSM));

	if (call_id)
		dbus_g_proxy_end_call (proxy, call_id, &error, G_TYPE_INVALID);

	if (error) {
		if (dbus_g_error_has_name (error, MM_MODEM_ERROR_PIN_NEEDED)) {
			secret = setting->pin;
			secret_name = NM_SETTING_GSM_PIN;
			priv->modem_state = MODEM_STATE_SET_PIN;
		} else if (dbus_g_error_has_name (error, MM_MODEM_ERROR_PUK_NEEDED)) {
			secret = setting->puk;
			secret_name = NM_SETTING_GSM_PUK;
			priv->modem_state = MODEM_STATE_SET_PIN;
		} else if (dbus_g_error_has_name (error, MM_MODEM_ERROR_INVALID_SECRET)) {
			g_free (setting->pin);
			setting->pin = NULL;
			secret_name = NM_SETTING_GSM_PIN;
			retry_secret = TRUE;
			priv->modem_state = MODEM_STATE_SET_PIN;
		} else {
			nm_warning ("GSM device connection failed: %s", error->message);
			nm_device_state_changed (NM_DEVICE (modem), NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_NONE);
			return;
		}
	}

	switch (priv->modem_state) {
	case MODEM_STATE_BEGIN:
		priv->modem_state = MODEM_STATE_ENABLE;
		dbus_g_proxy_begin_call (proxy, "Enable", state_machine_gsm,
							modem, NULL,
							G_TYPE_BOOLEAN, TRUE,
							G_TYPE_INVALID);
		break;
	case MODEM_STATE_ENABLE:
		priv->modem_state = MODEM_STATE_REGISTER;
		dbus_g_proxy_begin_call (proxy, "Register", state_machine_gsm,
							modem, NULL,
							G_TYPE_STRING, setting->network_id ? setting->network_id : "",
							G_TYPE_INVALID);
		break;
	case MODEM_STATE_SET_PIN:
		if (secret) {
			priv->modem_state = MODEM_STATE_ENABLE;
			dbus_g_proxy_begin_call (proxy, "SetPin", state_machine_gsm,
								modem, NULL,
								G_TYPE_STRING, secret,
								G_TYPE_INVALID);
		} else {
			nm_device_state_changed (NM_DEVICE (modem), NM_DEVICE_STATE_NEED_AUTH, NM_DEVICE_STATE_REASON_NONE);
			nm_act_request_request_connection_secrets (nm_device_get_act_request (NM_DEVICE (modem)),
											   NM_SETTING_GSM_SETTING_NAME,
											   retry_secret,
											   SECRETS_CALLER_GSM,
											   secret_name,
											   NULL);

		}
		break;
	case MODEM_STATE_REGISTER:
		priv->modem_state = MODEM_STATE_CONNECT;
		dbus_g_proxy_begin_call (proxy, "Connect", state_machine_gsm,
							modem, NULL,
							G_TYPE_STRING, setting->number,
							G_TYPE_STRING, setting->apn ? setting->apn : "",
							G_TYPE_INVALID);
		break;
	case MODEM_STATE_CONNECT:
		nm_device_activate_schedule_stage2_device_config (NM_DEVICE (modem));
		break;
	default:
		nm_warning ("Invalid modem state %d", priv->modem_state);
		nm_device_state_changed (NM_DEVICE (modem), NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_NONE);
		break;
	}
}

static NMActStageReturn
real_act_stage1_prepare (NMDevice *device, NMDeviceStateReason *reason)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (device);

	priv->modem_state = MODEM_STATE_BEGIN;

	if (nm_device_get_device_type (device) == NM_DEVICE_TYPE_GSM)
		state_machine_gsm (priv->proxy, NULL, device);
	else
		state_machine_cdma (priv->proxy, NULL, device);
 
	return NM_ACT_STAGE_RETURN_POSTPONE;
}

static void
ppp_state_changed (NMPPPManager *ppp_manager, NMPPPStatus status, gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);

	switch (status) {
	case NM_PPP_STATUS_NETWORK:
		nm_device_state_changed (device, NM_DEVICE_STATE_IP_CONFIG, NM_DEVICE_STATE_REASON_NONE);
		break;
	case NM_PPP_STATUS_DISCONNECT:
		nm_device_state_changed (device, NM_DEVICE_STATE_FAILED, NM_DEVICE_STATE_REASON_PPP_DISCONNECT);
		break;
	default:
		break;
	}
}

static void
ppp_ip4_config (NMPPPManager *ppp_manager,
			 const char *iface,
			 NMIP4Config *config,
			 gpointer user_data)
{
	NMDevice *device = NM_DEVICE (user_data);

	nm_device_set_ip_iface (device, iface);
	NM_MODEM_DEVICE_GET_PRIVATE (device)->pending_ip4_config = g_object_ref (config);
	nm_device_activate_schedule_stage4_ip_config_get (device);
}

static void
ppp_stats (NMPPPManager *ppp_manager,
		 guint32 in_bytes,
		 guint32 out_bytes,
		 gpointer user_data)
{
	NMModemDevice *device = NM_MODEM_DEVICE (user_data);
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (device);

	if (priv->in_bytes != in_bytes || priv->out_bytes != out_bytes) {
		priv->in_bytes = in_bytes;
		priv->out_bytes = out_bytes;

		g_signal_emit (device, signals[PPP_STATS], 0, in_bytes, out_bytes);
	}
}

static NMConnection *
real_get_best_auto_connection (NMDevice *dev,
                               GSList *connections,
                               char **specific_object)
{
	GSList *iter;

	for (iter = connections; iter; iter = g_slist_next (iter)) {
		NMConnection *connection = NM_CONNECTION (iter->data);
		NMSettingConnection *s_con;

		s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
		g_assert (s_con);

		if (!s_con->autoconnect)
			continue;

		if (strcmp (s_con->type, NM_SETTING_GSM_SETTING_NAME))
			continue;

		return connection;
	}
	return NULL;
}

static NMActStageReturn
real_act_stage2_config (NMDevice *device, NMDeviceStateReason *reason)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (device);
	NMActRequest *req;
	GError *err = NULL;
	NMActStageReturn ret;

	req = nm_device_get_act_request (device);
	g_assert (req);

	priv->ppp_manager = nm_ppp_manager_new ();

	if (nm_ppp_manager_start (priv->ppp_manager,
						 nm_device_get_iface (device),
						 req,
						 &err)) {
		g_signal_connect (priv->ppp_manager, "state-changed",
					   G_CALLBACK (ppp_state_changed),
					   device);
		g_signal_connect (priv->ppp_manager, "ip4-config",
					   G_CALLBACK (ppp_ip4_config),
					   device);
		g_signal_connect (priv->ppp_manager, "stats",
					   G_CALLBACK (ppp_stats),
					   device);

		ret = NM_ACT_STAGE_RETURN_POSTPONE;
	} else {
		nm_warning ("%s", err->message);
		g_error_free (err);

		g_object_unref (priv->ppp_manager);
		priv->ppp_manager = NULL;

		*reason = NM_DEVICE_STATE_REASON_PPP_START_FAILED;
		ret = NM_ACT_STAGE_RETURN_FAILURE;
	}

	return ret;
}

static NMActStageReturn
real_act_stage4_get_ip4_config (NMDevice *device,
                                NMIP4Config **config,
                                NMDeviceStateReason *reason)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (device);

	*config = priv->pending_ip4_config;
	priv->pending_ip4_config = NULL;

	return NM_ACT_STAGE_RETURN_SUCCESS;
}

static void
real_connection_secrets_updated (NMDevice *dev,
                                 NMConnection *connection,
                                 GSList *updated_settings,
                                 RequestSecretsCaller caller)
{
	NMActRequest *req;
	gboolean found = FALSE;
	GSList *iter;

	if (caller == SECRETS_CALLER_PPP) {
		NMPPPManager *ppp_manager;
		NMSettingGsm *s_gsm = NULL;

		ppp_manager = nm_modem_device_get_ppp_manager (NM_MODEM_DEVICE (dev));
		g_return_if_fail (ppp_manager != NULL);

		s_gsm = (NMSettingGsm *) nm_connection_get_setting (connection, NM_TYPE_SETTING_GSM);
		if (!s_gsm) {
			/* Shouldn't ever happen */
			nm_ppp_manager_update_secrets (ppp_manager,
			                               nm_device_get_iface (dev),
			                               NULL,
			                               NULL,
			                               "missing GSM setting; no secrets could be found.");
		} else {
			nm_ppp_manager_update_secrets (ppp_manager,
			                               nm_device_get_iface (dev),
			                               s_gsm->username ? s_gsm->username : "",
			                               s_gsm->password ? s_gsm->password : "",
			                               NULL);
		}
		return;
	}

	g_return_if_fail (caller == SECRETS_CALLER_GSM);
	g_return_if_fail (nm_device_get_state (dev) == NM_DEVICE_STATE_NEED_AUTH);

	for (iter = updated_settings; iter; iter = g_slist_next (iter)) {
		const char *setting_name = (const char *) iter->data;

		if (!strcmp (setting_name, NM_SETTING_GSM_SETTING_NAME))
			found = TRUE;
		else
			nm_warning ("Ignoring updated secrets for setting '%s'.", setting_name);
	}

	if (!found)
		return;

	req = nm_device_get_act_request (dev);
	g_assert (req);

	g_return_if_fail (nm_act_request_get_connection (req) == connection);

	nm_device_activate_schedule_stage1_device_prepare (dev);
}

static void
real_deactivate_quickly (NMDevice *device)
{
	//NMModemDevice *self = NM_MODEM_DEVICE (device);
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (device);

	nm_device_set_ip_iface (device, NULL);

	if (priv->pending_ip4_config) {
		g_object_unref (priv->pending_ip4_config);
		priv->pending_ip4_config = NULL;
	}

	priv->in_bytes = priv->out_bytes = 0;

	if (priv->ppp_manager) {
		g_object_unref (priv->ppp_manager);
		priv->ppp_manager = NULL;
	}

	//nm_modem_device_close (self);
}

static guint32
real_get_generic_capabilities (NMDevice *dev)
{
	return NM_DEVICE_CAP_NM_SUPPORTED;
}

static gboolean
unavailable_to_disconnected (gpointer user_data)
{
	nm_device_state_changed (NM_DEVICE (user_data),
	                         NM_DEVICE_STATE_DISCONNECTED,
	                         NM_DEVICE_STATE_REASON_NONE);
	return FALSE;
}

static void
device_state_changed (NMDeviceInterface *device,
                      NMDeviceState new_state,
                      NMDeviceState old_state,
                      NMDeviceStateReason reason,
                      gpointer user_data)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (user_data);

	/* Remove any previous delayed transition to disconnected */
	if (priv->state_to_disconnected_id) {
		g_source_remove (priv->state_to_disconnected_id);
		priv->state_to_disconnected_id = 0;
	}

	/* If transitioning to UNAVAILBLE and we have a carrier, transition to
	 * DISCONNECTED because the device is ready to use.  Otherwise the carrier-on
	 * handler will handle the transition to DISCONNECTED when the carrier is detected.
	 */
	if (new_state == NM_DEVICE_STATE_UNAVAILABLE)
		priv->state_to_disconnected_id = g_idle_add (unavailable_to_disconnected, user_data);

	/* Make sure we don't leave the serial device open */
	switch (new_state) {
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_UNMANAGED:
	case NM_DEVICE_STATE_UNAVAILABLE:
	case NM_DEVICE_STATE_FAILED:
	case NM_DEVICE_STATE_DISCONNECTED:
		dbus_g_proxy_call_no_reply (priv->proxy, "Disconnect", G_TYPE_INVALID);
		break;
	default:
		break;
	}
}

static void
signal_quality_proxy (DBusGProxy *proxy, guint32 quality, gpointer user_data)
{
	g_signal_emit (NM_MODEM_DEVICE (user_data), signals[SIGNAL_QUALITY], 0, quality);
}

/*****************************************************************************/

static void
nm_modem_device_init (NMModemDevice *self)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (self);

	priv->dbus_mgr = nm_dbus_manager_get ();
}

static GObject*
constructor (GType type,
		   guint n_construct_params,
		   GObjectConstructParam *construct_params)
{
	GObject *object;
	NMModemDevicePrivate *priv;

	object = G_OBJECT_CLASS (nm_modem_device_parent_class)->constructor (type,
														    n_construct_params,
														    construct_params);
	if (!object)
		return NULL;

	priv = NM_MODEM_DEVICE_GET_PRIVATE (object);

	if (!priv->path) {
		g_warning ("DBus path not provided");
		goto err;
	}

	if (nm_device_get_device_type (NM_DEVICE (object)) != NM_DEVICE_TYPE_GSM &&
	    nm_device_get_device_type (NM_DEVICE (object)) != NM_DEVICE_TYPE_CDMA) {
		g_warning ("Invalid modem type");
		goto err;
	}

	priv->proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
									 MM_DBUS_SERVICE, priv->path, MM_DBUS_INTERFACE_MODEM);

	dbus_g_proxy_add_signal (priv->proxy, "SignalQuality", G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (priv->proxy, "SignalQuality",
						    G_CALLBACK (signal_quality_proxy),
						    object,
						    NULL);

	g_signal_connect (object, "state-changed", G_CALLBACK (device_state_changed), object);

	return object;

 err:
	g_object_unref (object);
	return NULL;
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PATH:
		g_value_set_string (value, priv->path);
		break;
	case PROP_TYPE:
		g_value_set_uint (value, nm_device_get_device_type (NM_DEVICE (object)));
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
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PATH:
		/* Construct only */
		priv->path = g_value_dup_string (value);
		break;
	case PROP_TYPE:
		nm_device_set_device_type (NM_DEVICE (object), g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	NMModemDevicePrivate *priv = NM_MODEM_DEVICE_GET_PRIVATE (object);

	if (priv->state_to_disconnected_id) {
		g_source_remove (priv->state_to_disconnected_id);
		priv->state_to_disconnected_id = 0;
	}

	if (priv->proxy)
		g_object_unref (priv->proxy);

	g_object_unref (priv->dbus_mgr);

	G_OBJECT_CLASS (nm_modem_device_parent_class)->finalize (object);
}

static void
nm_modem_device_class_init (NMModemDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMModemDevicePrivate));

	/* Virtual methods */
	object_class->constructor = constructor;
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	device_class->get_best_auto_connection = real_get_best_auto_connection;
	device_class->get_generic_capabilities = real_get_generic_capabilities;
	device_class->connection_secrets_updated = real_connection_secrets_updated;
	device_class->act_stage1_prepare = real_act_stage1_prepare;
	device_class->act_stage2_config = real_act_stage2_config;
	device_class->act_stage4_get_ip4_config = real_act_stage4_get_ip4_config;
	device_class->deactivate_quickly = real_deactivate_quickly;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_PATH,
		 g_param_spec_string (NM_MODEM_DEVICE_PATH,
						  "DBus path",
						  "DBus path",
						  NULL,
						  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_TYPE,
		 g_param_spec_uint (NM_MODEM_DEVICE_TYPE,
						"Type",
						"NMDeviceType",
						NM_DEVICE_TYPE_UNKNOWN, NM_DEVICE_TYPE_CDMA, NM_DEVICE_TYPE_UNKNOWN,
						G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/* Signals */
	signals[SIGNAL_QUALITY] =
		g_signal_new ("signal-quality",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMModemDeviceClass, signal_quality),
				    NULL, NULL,
				    g_cclosure_marshal_VOID__UINT,
				    G_TYPE_NONE, 1,
				    G_TYPE_UINT);

	signals[PPP_STATS] =
		g_signal_new ("ppp-stats",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMModemDeviceClass, ppp_stats),
				    NULL, NULL,
				    nm_marshal_VOID__UINT_UINT,
				    G_TYPE_NONE, 2,
				    G_TYPE_UINT, G_TYPE_UINT);

	signals[PROPERTIES_CHANGED] = 
		nm_properties_changed_signal_new (object_class,
								    G_STRUCT_OFFSET (NMModemDeviceClass, properties_changed));

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass),
							   &dbus_glib_nm_gsm_device_object_info);
}
