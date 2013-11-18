/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * libnm_glib -- Access network status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2013 Red Hat, Inc.
 */

#ifndef NM_NETWORK_MONITOR_H
#define NM_NETWORK_MONITOR_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define NM_TYPE_NETWORK_MONITOR            (nm_network_monitor_get_type ())
#define NM_NETWORK_MONITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_NETWORK_MONITOR, NMNetworkMonitor))
#define NM_NETWORK_MONITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_NETWORK_MONITOR, NMNetworkMonitorClass))
#define NM_IS_NETWORK_MONITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_NETWORK_MONITOR))
#define NM_IS_NETWORK_MONITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_NETWORK_MONITOR))
#define NM_NETWORK_MONITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_NETWORK_MONITOR, NMNetworkMonitorClass))

typedef struct {
	GNetworkMonitorBase parent;
} NMNetworkMonitor;

typedef struct {
	GNetworkMonitorBaseClass parent;
} NMNetworkMonitorClass;

GType nm_network_monitor_get_type (void);
void  nm_network_monitor_register (GIOModule *module);

G_END_DECLS

#endif /* __G_NETWORK_MONITOR_NM_H__ */
