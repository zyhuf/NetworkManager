/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NM_MODEM_TYPES_H
#define NM_MODEM_TYPES_H

#define MM_DBUS_SERVICE         "org.freedesktop.ModemManager"
#define MM_DBUS_PATH            "/org/freedesktop/ModemManager"
#define MM_DBUS_INTERFACE       "org.freedesktop.ModemManager"
#define MM_DBUS_INTERFACE_MODEM "org.freedesktop.ModemManager.Modem"

#define MM_MODEM_TYPE_UNKNOWN  0
#define MM_MODEM_TYPE_GSM      1
#define MM_MODEM_TYPE_CDMA     2

#define MM_MODEM_ERROR_GENERAL        MM_DBUS_INTERFACE_MODEM ".GeneralError"
#define MM_MODEM_ERROR_PIN_NEEDED     MM_DBUS_INTERFACE_MODEM ".PINNeeded"
#define MM_MODEM_ERROR_PUK_NEEDED     MM_DBUS_INTERFACE_MODEM ".PUKNeeded"
#define MM_MODEM_ERROR_INVALID_SECRET MM_DBUS_INTERFACE_MODEM ".InvalidSecret"

#endif /* NM_MODEM_TYPES_H */
