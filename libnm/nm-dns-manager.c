// SPDX-License-Identifier: LGPL-2.1+
/*
 * Copyright (C) 2016 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-dns-manager.h"

#include "nm-dbus-interface.h"
#include "nm-connection.h"
#include "nm-client.h"
#include "nm-object-private.h"
#include "nm-dbus-helpers.h"
#include "nm-core-internal.h"

/*****************************************************************************
 * NMDnsEntry
 *****************************************************************************/

G_DEFINE_BOXED_TYPE (NMDnsEntry, nm_dns_entry, nm_dns_entry_ref, nm_dns_entry_unref)

struct NMDnsEntry {
	const char **nameservers;
	const char **domains;

	int ref_count;

	int priority;
	bool vpn:1;
	bool has_interface:1;
	bool has_nameservers:1;
	bool has_domains:1;

	char interface[0];
};

/**
 * nm_dns_entry_new:
 *
 * Creates a new #NMDnsEntry object.
 *
 * Returns: (transfer full): the new #NMDnsEntry object, or %NULL on error
 **/
NMDnsEntry *
nm_dns_entry_new (const char *interface,
                  const char *const*nameservers,
                  const char *const*domains,
                  int priority,
                  gboolean vpn)
{
	NMDnsEntry *entry;
	gsize l;

	l = interface ? strlen (interface) + 1u : 0u;

	entry = g_malloc (sizeof (NMDnsEntry) + l);
	entry->ref_count       = 1,
	entry->priority        = priority,
	entry->vpn             = vpn,
	entry->nameservers     = nm_utils_strv_dup_inline (nameservers, -1);
	entry->domains         = nm_utils_strv_dup_inline (domains, -1);

	/* for empty arrays, we don't allocation a strv array. However, we
	 * need to remember whether the array was NULL or empty. */
	entry->has_nameservers = (!!nameservers);
	entry->has_domains     = (!!domains);

	entry->has_interface   = (!!interface);
	if (interface)
		memcpy (entry->interface, interface, l);

	return entry;
}

/**
 * nm_dns_entry_ref:
 * @entry: the #NMDnsEntry
 *
 * Increase the reference count of the entry.
 *
 * Returns: (transfer full): returns @entry.
 **/
NMDnsEntry *
nm_dns_entry_ref (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry != NULL, NULL);
	g_return_val_if_fail (entry->ref_count > 0, NULL);

	g_atomic_int_inc (&entry->ref_count);
	return entry;
}

/**
 * nm_dns_entry_unref:
 * @entry: the #NMDnsEntry
 *
 * Decreases the reference count of the object.  If the reference count
 * reaches zero, the object will be destroyed.
 *
 * Since: 1.6
 **/
void
nm_dns_entry_unref (NMDnsEntry *entry)
{
	g_return_if_fail (entry != NULL);
	g_return_if_fail (entry->ref_count > 0);

	if (!g_atomic_int_dec_and_test (&entry->ref_count))
		return;

	g_free (entry->nameservers);
	g_free (entry->domains);
	g_free (entry);
}

/**
 * nm_dns_entry_get_interface:
 * @entry: the #NMDnsEntry
 *
 * Gets the interface on which name servers are contacted.
 *
 * Returns: (transfer none): the interface name
 *
 * Since: 1.6
 **/
const char *
nm_dns_entry_get_interface (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry, 0);
	g_return_val_if_fail (entry->ref_count > 0, 0);

	return entry->has_interface ? entry->interface : NULL;
}

/**
 * nm_dns_entry_get_nameservers:
 * @entry: the #NMDnsEntry
 *
 * Gets the list of name servers for this entry.
 *
 * Returns: (transfer none): the list of name servers
 *
 * Since: 1.6
 **/
const char *const*
nm_dns_entry_get_nameservers (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry, 0);
	g_return_val_if_fail (entry->ref_count > 0, 0);

	return    entry->nameservers
	       ?: (  entry->has_nameservers
	           ? (const char *const*) &entry->nameservers
	           : NULL);
}

/**
 * nm_dns_entry_get_domains:
 * @entry: the #NMDnsEntry
 *
 * Gets the list of DNS domains.
 *
 * Returns: (transfer none): the list of DNS domains
 *
 * Since: 1.6
 **/
const char *const*
nm_dns_entry_get_domains (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry, 0);
	g_return_val_if_fail (entry->ref_count > 0, 0);

	return    entry->domains
	       ?: (  entry->has_domains
	           ? (const char *const*) &entry->domains
	           : NULL);
}

/**
 * nm_dns_entry_get_vpn:
 * @entry: the #NMDnsEntry
 *
 * Gets whether the entry refers to VPN name servers.
 *
 * Returns: %TRUE if the entry refers to VPN name servers
 *
 * Since: 1.6
 **/
gboolean
nm_dns_entry_get_vpn (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry, 0);
	g_return_val_if_fail (entry->ref_count > 0, 0);

	return entry->vpn;
}

/**
 * nm_dns_entry_get_priority:
 * @entry: the #NMDnsEntry
 *
 * Gets the priority of the entry
 *
 * Returns: the priority of the entry
 *
 * Since: 1.6
 **/
int
nm_dns_entry_get_priority (NMDnsEntry *entry)
{
	g_return_val_if_fail (entry, 0);
	g_return_val_if_fail (entry->ref_count > 0, 0);

	return entry->priority;
}
