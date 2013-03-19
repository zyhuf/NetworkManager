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

#ifndef NM_DNS_MANAGER_H
#define NM_DNS_MANAGER_H

#include "nm-object.h"

G_BEGIN_DECLS

#define NM_TYPE_DNS_MANAGER            (nm_dns_manager_get_type ())
#define NM_DNS_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DNS_MANAGER, NMDnsManager))
#define NM_DNS_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_DNS_MANAGER, NMDnsManagerClass))
#define NM_IS_DNS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DNS_MANAGER))
#define NM_IS_DNS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_DNS_MANAGER))
#define NM_DNS_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_DNS_MANAGER, NMDnsManagerClass))

#define NM_DNS_MANAGER_NAMESERVERS  "nameservers"
#define NM_DNS_MANAGER_DOMAIN       "domain"
#define NM_DNS_MANAGER_SEARCHES     "searches"
#define NM_DNS_MANAGER_NIS_DOMAIN   "nis-domain"
#define NM_DNS_MANAGER_NIS_SERVERS  "nis-servers"
#define NM_DNS_MANAGER_WINS_SERVERS "wins-servers"
#define NM_DNS_MANAGER_SPLIT_DNS    "split-dns"

typedef struct {
	NMObject parent;
} NMDnsManager;

typedef struct {
	NMObjectClass parent;

	/* Padding for future expansion */
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
	void (*_reserved5) (void);
	void (*_reserved6) (void);
} NMDnsManagerClass;

GType nm_dns_manager_get_type (void);

NMDnsManager *nm_dns_manager_new        (void);

void          nm_dns_manager_new_async  (GCancellable         *cancellable,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
NMDnsManager *nm_dns_manager_new_finish (GAsyncResult         *result,
                                         GError              **error);

const char **nm_dns_manager_get_nameservers  (NMDnsManager *mgr);
const char  *nm_dns_manager_get_domain       (NMDnsManager *mgr);
const char **nm_dns_manager_get_searches     (NMDnsManager *mgr);
const char **nm_dns_manager_get_nis_servers  (NMDnsManager *mgr);
const char  *nm_dns_manager_get_nis_domain   (NMDnsManager *mgr);
const char **nm_dns_manager_get_wins_servers (NMDnsManager *mgr);

typedef struct {
	char  *interface;
	char **nameservers;
	char **domains;
	char **ip_ranges;
} NMDnsManagerNetwork;

const GPtrArray *nm_dns_manager_get_split_dns (NMDnsManager *mgr);

G_END_DECLS

#endif /* NM_DNS_MANAGER_H */
