// SPDX-License-Identifier: GPL-2.0+
/* NetworkManager -- Network link manager
 *
 * Copyright (C) 2007 - 2008 Novell, Inc.
 * Copyright (C) 2007 - 2013 Red Hat, Inc.
 */

#ifndef __NM_RFKILL_MANAGER_H__
#define __NM_RFKILL_MANAGER_H__

#define NM_TYPE_RFKILL_MANAGER            (nm_rfkill_manager_get_type ())
#define NM_RFKILL_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_RFKILL_MANAGER, NMRfkillManager))
#define NM_RFKILL_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_RFKILL_MANAGER, NMRfkillManagerClass))
#define NM_IS_RFKILL_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_RFKILL_MANAGER))
#define NM_IS_RFKILL_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_RFKILL_MANAGER))
#define NM_RFKILL_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_RFKILL_MANAGER, NMRfkillManagerClass))

#define NM_RFKILL_MANAGER_SIGNAL_RFKILL_CHANGED "rfkill-changed"

typedef struct _NMRfkillManagerClass NMRfkillManagerClass;

GType nm_rfkill_manager_get_type (void);

NMRfkillManager *nm_rfkill_manager_new (void);

int nm_rfkill_manager_get_rfkill_state (NMRfkillManager *manager, int rtype);

#endif  /* __NM_RFKILL_MANAGER_H__ */
