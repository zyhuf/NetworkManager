// SPDX-License-Identifier: LGPL-2.1+

#ifndef __NM_BRIDGE_UITLS_H__
#define __NM_BRIDGE_UITLS_H__

#if !((NETWORKMANAGER_COMPILATION) & NM_NETWORKMANAGER_COMPILATION_WITH_LIBNM_CORE_PRIVATE)
#error Cannot use this header.
#endif

#include "nm-glib-aux/nm-value-type.h"
#include "nm-core-internal.h"

typedef enum {
	NM_PROP_SETTING_TYPE_BRIDGE_MASTER,
	NM_PROP_SETTING_TYPE_BRIDGE_PORT,
} NMPropSettingType;

typedef enum {

	/* PROP_0 is always reserved. That is what g_object_class_install_properties() requires. */
	_NM_PROP_ATTR_0 = 0,

	_NM_PROP_ATTR_START = 1,

	NM_PROP_ATTR_BRIDGE_MASTER_MAC_ADDRESS = _NM_PROP_ATTR_START,
	NM_PROP_ATTR_BRIDGE_MASTER_STP,
	NM_PROP_ATTR_BRIDGE_MASTER_PRIORITY,
	NM_PROP_ATTR_BRIDGE_MASTER_FORWARD_DELAY,
	NM_PROP_ATTR_BRIDGE_MASTER_HELLO_TIME,
	NM_PROP_ATTR_BRIDGE_MASTER_MAX_AGE,
	NM_PROP_ATTR_BRIDGE_MASTER_AGEING_TIME,
	NM_PROP_ATTR_BRIDGE_MASTER_GROUP_ADDRESS,
	NM_PROP_ATTR_BRIDGE_MASTER_GROUP_FORWARD_MASK,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_HASH_MAX,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_LAST_MEMBER_COUNT,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_LAST_MEMBER_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_MEMBERSHIP_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_ROUTER,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERIER,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERIER_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_RESPONSE_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_USE_IFADDR,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_SNOOPING,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_STARTUP_QUERY_COUNT,
	NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_STARTUP_QUERY_INTERVAL,
	NM_PROP_ATTR_BRIDGE_MASTER_VLAN_FILTERING,
	NM_PROP_ATTR_BRIDGE_MASTER_VLAN_DEFAULT_PVID,
	NM_PROP_ATTR_BRIDGE_MASTER_VLAN_PROTOCOL,
	NM_PROP_ATTR_BRIDGE_MASTER_VLAN_STATS_ENABLED,
	NM_PROP_ATTR_BRIDGE_MASTER_VLANS,
	_NM_PROP_ATTR_BRIDGE_MASTER_NUM,

	NM_PROP_ATTR_BRIDGE_PORT_QUEUE_ID = _NM_PROP_ATTR_START,
	NM_PROP_ATTR_BRIDGE_PORT_PRIO,
	_NM_PROP_ATTR_BRIDGE_PORT_NUM,

} NMPropAttr;

typedef struct {
	NMPropSettingType setting_type;
	GPtrArray *vlans;
	char *group_address;
	char *mac_address;
	char *multicast_router;
	char *vlan_protocol;
	guint64 multicast_last_member_interval;
	guint64 multicast_membership_interval;
	guint64 multicast_querier_interval;
	guint64 multicast_query_interval;
	guint64 multicast_query_response_interval;
	guint64 multicast_startup_query_interval;
	guint   ageing_time;
	guint   forward_delay;
	guint   group_forward_mask;
	guint   hello_time;
	guint   max_age;
	guint   multicast_hash_max;
	guint   multicast_last_member_count;
	guint   multicast_startup_query_count;
	guint   priority;
	guint   vlan_default_pvid;
	bool    multicast_querier;
	bool    multicast_query_use_ifaddr;
	bool    multicast_snooping;
	bool    stp;
	bool    vlan_filtering;
	bool    vlan_stats_enabled;
} NMPropSettingDataBridgeMaster;

typedef struct {
	NMPropSettingType setting_type;
} NMPropSettingDataBridgePort;

typedef union {
	union {
		NMPropSettingType             setting_type;
		NMPropSettingDataBridgeMaster bridge_master;
		NMPropSettingDataBridgePort   bridge_port;
	};
} NMPropSettingData;

typedef struct {

	/* The GObject property name */
	const char *property_name;

	union {
		struct {
			bool def;
		} boolean;
		struct {
			guint min;
			guint max;
			guint def;
			bool gobject_min_zero;
		} uint;
		struct {
			guint64 min;
			guint64 max;
			guint64 def;
		} uint64;
		struct {
			GType (*get_gtype_fcn) (void);
		} boxed;
	} property_typ;

	NMPropSettingType setting_type;
	NMPropAttr property_attribute;

	/* The GObject property type. */
	GType property_type;

	/* The G_STRUCT_OFFSET() of the corresponding field into NMPropSettingData. */
	guint8 prop_offset;

} NMPropAttrType;

/*****************************************************************************/

extern const NMPropAttrType _nm_prop_attr_types_bridge_master[_NM_PROP_ATTR_BRIDGE_MASTER_NUM];
extern const NMPropAttrType _nm_prop_attr_types_bridge_port[_NM_PROP_ATTR_BRIDGE_PORT_NUM];

#define _nm_prop_attr_types_get(array, prop_attr) \
	({ \
		const NMPropAttrType *const _array = (array); \
		const NMPropAttr _prop_attr = (prop_attr); \
		\
		nm_assert (_prop_attr > _NM_PROP_ATTR_0); \
		nm_assert (_prop_attr < G_N_ELEMENTS (array)); \
		\
		&_array[_prop_attr]; \
	})

#define nm_prop_attr_types_bridge_master(prop_attr) _nm_prop_attr_types_get(_nm_prop_attr_types_bridge_master, prop_attr)
#define nm_prop_attr_types_bridge_port(prop_attr)   _nm_prop_attr_types_get(_nm_prop_attr_types_bridge_port, prop_attr)

/*****************************************************************************/

extern GParamSpec *nm_prop_gobject_properties_bridge_master[_NM_PROP_ATTR_BRIDGE_MASTER_NUM];
extern GParamSpec *nm_prop_gobject_properties_bridge_port[_NM_PROP_ATTR_BRIDGE_PORT_NUM];

/*****************************************************************************/

#define nm_prop_setting_data_get_prop_ptr(ctype, data, attr_type) \
	({ \
		const NMPropAttrType *_attr_type = (attr_type); \
		typeof (data) _data = (data); \
		\
		nm_assert (_data); \
		nm_assert (_attr_type); \
		nm_assert (_data->setting_type == _attr_type->setting_type); \
		\
		NM_PROPAGATE_CONST (data, (ctype *) ((gconstpointer) (&(((const char *) _data)[attr_type->prop_offset])))); \
	})

void nm_prop_setting_data_get_property (const NMPropSettingData *data,
                                        const NMPropAttrType *attr_type,
                                        GValue *value);

void nm_prop_setting_data_set_property_clear (NMPropSettingData *data,
                                              const NMPropAttrType *attr_type);

void nm_prop_setting_data_set_property_default (NMPropSettingData *data,
                                                const NMPropAttrType *attr_type);

void nm_prop_setting_data_set_property (NMPropSettingData *data,
                                        const NMPropAttrType *attr_type,
                                        const GValue *value);

#endif /* __NM_BRIDGE_UITLS_H__ */
