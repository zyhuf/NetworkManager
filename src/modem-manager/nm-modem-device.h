/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NM_MODEM_DEVICE_H
#define NM_MODEM_DEVICE_H

#include <nm-device.h>
#include "ppp-manager/nm-ppp-manager.h"

G_BEGIN_DECLS

#define NM_TYPE_MODEM_DEVICE			(nm_modem_device_get_type ())
#define NM_MODEM_DEVICE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_MODEM_DEVICE, NMModemDevice))
#define NM_MODEM_DEVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  NM_TYPE_MODEM_DEVICE, NMModemDeviceClass))
#define NM_IS_MODEM_DEVICE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_MODEM_DEVICE))
#define NM_IS_MODEM_DEVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  NM_TYPE_MODEM_DEVICE))
#define NM_MODEM_DEVICE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  NM_TYPE_MODEM_DEVICE, NMModemDeviceClass))

#define NM_MODEM_DEVICE_PATH "path"
#define NM_MODEM_DEVICE_TYPE "type"

typedef struct {
	NMDevice parent;
} NMModemDevice;

typedef struct {
	NMDeviceClass parent;

	/* Signals */
	void (*signal_quality) (NMModemDevice *device, guint32 quality);
	void (*ppp_stats) (NMModemDevice *device, guint32 in_bytes, guint32 out_bytes);
	void (*properties_changed) (NMModemDevice *device, GHashTable *properties);
} NMModemDeviceClass;

GType nm_modem_device_get_type (void);

NMModemDevice *nm_modem_device_new (const char *path,
							 const char *data_device,
							 const char *driver,
							 int device_type);

NMPPPManager *nm_modem_device_get_ppp_manager (NMModemDevice *device);

G_END_DECLS

#endif /* NM_MODEM_DEVICE_H */
