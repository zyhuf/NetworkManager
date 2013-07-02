/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Thomas Graf <tgraf@redhat.com>
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
 * (C) Copyright 2011 - 2013 Red Hat, Inc.
 */

#ifndef NM_SETTING_BOND_H
#define NM_SETTING_BOND_H

#include <nm-setting.h>

G_BEGIN_DECLS

#define NM_TYPE_SETTING_BOND            (nm_setting_bond_get_type ())
#define NM_SETTING_BOND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_BOND, NMSettingBond))
#define NM_SETTING_BOND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_BOND, NMSettingBondClass))
#define NM_IS_SETTING_BOND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_BOND))
#define NM_IS_SETTING_BOND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_SETTING_BOND))
#define NM_SETTING_BOND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_BOND, NMSettingBondClass))

#define NM_SETTING_BOND_SETTING_NAME "bond"

/**
 * NMSettingBondError:
 * @NM_SETTING_BOND_ERROR_UNKNOWN: unknown or unclassified error
 * @NM_SETTING_BOND_ERROR_INVALID_PROPERTY: the property was invalid
 * @NM_SETTING_BOND_ERROR_MISSING_PROPERTY: the property was missing and is
 * required
 */
typedef enum {
	NM_SETTING_BOND_ERROR_UNKNOWN = 0,      /*< nick=UnknownError >*/
	NM_SETTING_BOND_ERROR_INVALID_PROPERTY, /*< nick=InvalidProperty >*/
	NM_SETTING_BOND_ERROR_MISSING_PROPERTY, /*< nick=MissingProperty >*/
	NM_SETTING_BOND_ERROR_INVALID_OPTION,   /*< nick=InvalidOption >*/
	NM_SETTING_BOND_ERROR_MISSING_OPTION,   /*< nick=MissingOption >*/
} NMSettingBondError;

#define NM_SETTING_BOND_ERROR nm_setting_bond_error_quark ()
GQuark nm_setting_bond_error_quark (void);

#define NM_SETTING_BOND_INTERFACE_NAME   "interface-name"
#define NM_SETTING_BOND_MODE             "mode"
#define NM_SETTING_BOND_PRIMARY          "primary"
#define NM_SETTING_BOND_MIIMON           "miimon"
#define NM_SETTING_BOND_DOWNDELAY        "downdelay"
#define NM_SETTING_BOND_UPDELAY          "updelay"
#define NM_SETTING_BOND_ARP_INTERVAL     "arp-interval"
#define NM_SETTING_BOND_ARP_IP_TARGET    "arp-ip-target"
#define NM_SETTING_BOND_ARP_VALIDATE     "arp-validate"
#define NM_SETTING_BOND_PRIMARY          "primary"
#define NM_SETTING_BOND_PRIMARY_RESELECT "primary-reselect"
#define NM_SETTING_BOND_FAIL_OVER_MAC    "fail-over-mac"
#define NM_SETTING_BOND_USE_CARRIER      "use-carrier"
#define NM_SETTING_BOND_AD_SELECT        "ad-select"
#define NM_SETTING_BOND_XMIT_HASH_POLICY "xmit-hash-policy"
#define NM_SETTING_BOND_RESEND_IGMP      "resend-igmp"

/* Deprecated */
#define NM_SETTING_BOND_OPTIONS          "options"

/* Valid options as named by the kernal */
#define NM_SETTING_BOND_OPTION_MODE             "mode"
#define NM_SETTING_BOND_OPTION_MIIMON           "miimon"
#define NM_SETTING_BOND_OPTION_DOWNDELAY        "downdelay"
#define NM_SETTING_BOND_OPTION_UPDELAY          "updelay"
#define NM_SETTING_BOND_OPTION_ARP_INTERVAL     "arp_interval"
#define NM_SETTING_BOND_OPTION_ARP_IP_TARGET    "arp_ip_target"
#define NM_SETTING_BOND_OPTION_ARP_VALIDATE     "arp_validate"
#define NM_SETTING_BOND_OPTION_PRIMARY          "primary"
#define NM_SETTING_BOND_OPTION_PRIMARY_RESELECT "primary_reselect"
#define NM_SETTING_BOND_OPTION_FAIL_OVER_MAC    "fail_over_mac"
#define NM_SETTING_BOND_OPTION_USE_CARRIER      "use_carrier"
#define NM_SETTING_BOND_OPTION_AD_SELECT        "ad_select"
#define NM_SETTING_BOND_OPTION_XMIT_HASH_POLICY "xmit_hash_policy"
#define NM_SETTING_BOND_OPTION_RESEND_IGMP      "resend_igmp"

#define __NM_SETTING_BOND_MODE_IS_balance_rr(mode)      ((mode) && (!strcmp ((mode), "0") || !strcmp ((mode), "balance-rr")))
#define __NM_SETTING_BOND_MODE_IS_active_backup(mode)   ((mode) && (!strcmp ((mode), "1") || !strcmp ((mode), "active-backup")))
#define __NM_SETTING_BOND_MODE_IS_balance_xor(mode)     ((mode) && (!strcmp ((mode), "2") || !strcmp ((mode), "balance-xor")))
#define __NM_SETTING_BOND_MODE_IS_broadcast(mode)       ((mode) && (!strcmp ((mode), "3") || !strcmp ((mode), "broadcast")))
#define __NM_SETTING_BOND_MODE_IS_802_3ad(mode)         ((mode) && (!strcmp ((mode), "4") || !strcmp ((mode), "802.3ad")))
#define __NM_SETTING_BOND_MODE_IS_balance_tlb(mode)     ((mode) && (!strcmp ((mode), "5") || !strcmp ((mode), "balance-tlb")))
#define __NM_SETTING_BOND_MODE_IS_balance_alb(mode)     ((mode) && (!strcmp ((mode), "6") || !strcmp ((mode), "balance-alb")))

typedef struct {
	NMSetting parent;
} NMSettingBond;

typedef struct {
	NMSettingClass parent;

	/* Padding for future expansion */
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
} NMSettingBondClass;

GType nm_setting_bond_get_type (void);

NMSetting *  nm_setting_bond_new                  (void);

const char * nm_setting_bond_get_interface_name   (const NMSettingBond *setting);

const char * nm_setting_bond_get_mode             (const NMSettingBond *setting);
const char * nm_setting_bond_get_primary          (const NMSettingBond *setting);
guint        nm_setting_bond_get_miimon           (const NMSettingBond *setting);
guint        nm_setting_bond_get_downdelay        (const NMSettingBond *setting);
guint        nm_setting_bond_get_updelay          (const NMSettingBond *setting);
guint        nm_setting_bond_get_arp_interval     (const NMSettingBond *setting);
const char *const* nm_setting_bond_get_arp_ip_target (const NMSettingBond *setting);
const char * nm_setting_bond_get_arp_validate     (const NMSettingBond *setting);
const char * nm_setting_bond_get_primary_reselect (const NMSettingBond *setting);
const char * nm_setting_bond_get_fail_over_mac    (const NMSettingBond *setting);
gboolean     nm_setting_bond_get_use_carrier      (const NMSettingBond *setting);
const char * nm_setting_bond_get_ad_select        (const NMSettingBond *setting);
const char * nm_setting_bond_get_xmit_hash_policy (const NMSettingBond *setting);
guint        nm_setting_bond_get_resend_igmp      (const NMSettingBond *setting);

const char * nm_setting_bond_get_property_name    (const char *name);
const char * nm_setting_bond_get_kernel_name      (const char *name);
const char *const* nm_setting_bond_get_kernel_names (void);

const char * nm_setting_bond_get_string           (const NMSettingBond *setting, const char *name);
gboolean     nm_setting_bond_is_default           (const NMSettingBond *setting, const char *name, GValue *default_value);
gboolean     nm_setting_bond_validate_string      (const char *name, const char *value, GError **error);
gboolean     nm_setting_bond_set_string           (NMSettingBond *setting, const char *name, const char *value);
void         nm_setting_bond_set_default          (NMSettingBond *setting, const char *name);


/* Deprecated */
G_GNUC_DEPRECATED guint32      nm_setting_bond_get_num_options    (NMSettingBond *setting);
G_GNUC_DEPRECATED gboolean     nm_setting_bond_get_option         (NMSettingBond *setting,
                                                                   guint32 idx,
                                                                   const char **out_name,
                                                                   const char **out_value);
G_GNUC_DEPRECATED const char * nm_setting_bond_get_option_by_name (NMSettingBond *setting,
                                                                   const char *name);
G_GNUC_DEPRECATED gboolean     nm_setting_bond_add_option         (NMSettingBond *setting,
                                                                   const char *name,
                                                                   const char *value);
G_GNUC_DEPRECATED gboolean     nm_setting_bond_remove_option      (NMSettingBond *setting,
                                                                   const char *name);

G_GNUC_DEPRECATED const char **nm_setting_bond_get_valid_options  (NMSettingBond *setting);

G_GNUC_DEPRECATED const char * nm_setting_bond_get_option_default (NMSettingBond *setting,
                                                                   const char *name);

G_END_DECLS

#endif /* NM_SETTING_BOND_H */
