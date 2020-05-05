// SPDX-License-Identifier: LGPL-2.1+

#include "nm-default.h"

#include "nm-prop-meta.h"

/*****************************************************************************/

GParamSpec *nm_prop_gobject_properties_bridge_master[_NM_PROP_ATTR_BRIDGE_MASTER_NUM] = { NULL, };
GParamSpec *nm_prop_gobject_properties_bridge_port[_NM_PROP_ATTR_BRIDGE_PORT_NUM]     = { NULL, };

/*****************************************************************************/

#define ATTR(_property_attribute, _property_name, ...) \
	[_property_attribute] = { \
		.setting_type       = CURRENT_SETTING_TYPE, \
		.property_attribute = _property_attribute, \
		.property_name      = ""_property_name"", \
		__VA_ARGS__ \
	}

#define ATTR_TYPE_STRING(_prop_field) \
	.property_type = G_TYPE_STRING, \
	.prop_offset   = NM_STRUCT_OFFSET_ENSURE_TYPE (char *, NMPropSettingData, CURRENT_STRUCT_BASE_FIELD._prop_field)

#define ATTR_TYPE_BOOL(_prop_field, _def) \
	.property_type = G_TYPE_BOOLEAN, \
	.property_typ.boolean = { \
		.def = (_def), \
	}, \
	.prop_offset = NM_STRUCT_OFFSET_ENSURE_TYPE (bool, NMPropSettingData, CURRENT_STRUCT_BASE_FIELD._prop_field)

#define ATTR_TYPE_UINT(_prop_field, _min, _max, _def, ...) \
	.property_type = G_TYPE_UINT, \
	.property_typ.uint = { \
		.min = (_min), \
		.max = (_max), \
		.def = (_def), \
		__VA_ARGS__ \
	}, \
	.prop_offset = NM_STRUCT_OFFSET_ENSURE_TYPE (guint, NMPropSettingData, CURRENT_STRUCT_BASE_FIELD._prop_field)

#define ATTR_TYPE_UINT64(_prop_field, _min, _max, _def) \
	.property_type = G_TYPE_UINT64, \
	.property_typ.uint64 = { \
		.min = (_min), \
		.max = (_max), \
		.def = (_def), \
	}, \
	.prop_offset = NM_STRUCT_OFFSET_ENSURE_TYPE (guint64, NMPropSettingData, CURRENT_STRUCT_BASE_FIELD._prop_field)

#define _ATTR_TYPE_BOXED(_prop_field, _get_gtype_fcn, _prop_offset) \
	.property_type = G_TYPE_BOXED, \
	.property_typ.boxed = { \
		.get_gtype_fcn = _get_gtype_fcn, \
	}, \
	.prop_offset = _prop_offset

#define ATTR_TYPE_BOXED_PTR_ARRAY(_prop_field) \
	_ATTR_TYPE_BOXED (_prop_field, g_ptr_array_get_type, NM_STRUCT_OFFSET_ENSURE_TYPE (GPtrArray *, NMPropSettingData, CURRENT_STRUCT_BASE_FIELD._prop_field))

#define CURRENT_SETTING_TYPE       NM_PROP_SETTING_TYPE_BRIDGE_MASTER
#define CURRENT_STRUCT_BASE_FIELD  bridge_master
const NMPropAttrType _nm_prop_attr_types_bridge_master[_NM_PROP_ATTR_BRIDGE_MASTER_NUM] = {
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MAC_ADDRESS,
	      NM_SETTING_BRIDGE_MAC_ADDRESS,
	      ATTR_TYPE_STRING (mac_address),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_STP,
	      NM_SETTING_BRIDGE_STP,
	      ATTR_TYPE_BOOL (stp, NM_BRIDGE_STP_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_PRIORITY,
	      NM_SETTING_BRIDGE_PRIORITY,
	      ATTR_TYPE_UINT (priority, NM_BRIDGE_PRIORITY_MIN, NM_BRIDGE_PRIORITY_MAX, NM_BRIDGE_PRIORITY_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_FORWARD_DELAY,
	      NM_SETTING_BRIDGE_FORWARD_DELAY,
	      ATTR_TYPE_UINT (forward_delay, NM_BRIDGE_FORWARD_DELAY_MIN, NM_BRIDGE_FORWARD_DELAY_MAX, NM_BRIDGE_FORWARD_DELAY_DEF, .gobject_min_zero = TRUE),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_HELLO_TIME,
	      NM_SETTING_BRIDGE_HELLO_TIME,
	      ATTR_TYPE_UINT (hello_time, NM_BRIDGE_HELLO_TIME_MIN, NM_BRIDGE_HELLO_TIME_MAX, NM_BRIDGE_HELLO_TIME_DEF, .gobject_min_zero = TRUE),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MAX_AGE,
	      NM_SETTING_BRIDGE_MAX_AGE,
	      ATTR_TYPE_UINT (max_age, NM_BRIDGE_MAX_AGE_MIN, NM_BRIDGE_MAX_AGE_MAX, NM_BRIDGE_MAX_AGE_DEF, .gobject_min_zero = TRUE),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_AGEING_TIME,
	      NM_SETTING_BRIDGE_AGEING_TIME,
	      ATTR_TYPE_UINT (ageing_time, NM_BRIDGE_AGEING_TIME_MIN, NM_BRIDGE_AGEING_TIME_MAX, NM_BRIDGE_AGEING_TIME_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_GROUP_ADDRESS,
	      NM_SETTING_BRIDGE_GROUP_ADDRESS,
	      ATTR_TYPE_STRING (group_address),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_GROUP_FORWARD_MASK,
	      NM_SETTING_BRIDGE_GROUP_FORWARD_MASK,
	      ATTR_TYPE_UINT (group_forward_mask, 0, 0xFFFF, 0),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_HASH_MAX,
	      NM_SETTING_BRIDGE_MULTICAST_HASH_MAX,
	      ATTR_TYPE_UINT (multicast_hash_max, NM_BRIDGE_MULTICAST_HASH_MAX_MIN, NM_BRIDGE_MULTICAST_HASH_MAX_MAX, NM_BRIDGE_MULTICAST_HASH_MAX_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_LAST_MEMBER_COUNT,
	      NM_SETTING_BRIDGE_MULTICAST_LAST_MEMBER_COUNT,
	      ATTR_TYPE_UINT (multicast_last_member_count, NM_BRIDGE_MULTICAST_LAST_MEMBER_COUNT_MIN, NM_BRIDGE_MULTICAST_LAST_MEMBER_COUNT_MAX, NM_BRIDGE_MULTICAST_LAST_MEMBER_COUNT_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_LAST_MEMBER_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_LAST_MEMBER_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_last_member_interval, NM_BRIDGE_MULTICAST_LAST_MEMBER_INTERVAL_MIN, NM_BRIDGE_MULTICAST_LAST_MEMBER_INTERVAL_MAX, NM_BRIDGE_MULTICAST_LAST_MEMBER_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_MEMBERSHIP_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_MEMBERSHIP_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_membership_interval, NM_BRIDGE_MULTICAST_MEMBERSHIP_INTERVAL_MIN, NM_BRIDGE_MULTICAST_MEMBERSHIP_INTERVAL_MAX, NM_BRIDGE_MULTICAST_MEMBERSHIP_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_ROUTER,
	      NM_SETTING_BRIDGE_MULTICAST_ROUTER,
	      ATTR_TYPE_STRING (multicast_router),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERIER,
	      NM_SETTING_BRIDGE_MULTICAST_QUERIER,
	      ATTR_TYPE_BOOL (multicast_querier, NM_BRIDGE_MULTICAST_QUERIER_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERIER_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_QUERIER_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_querier_interval, NM_BRIDGE_MULTICAST_QUERIER_INTERVAL_MIN, NM_BRIDGE_MULTICAST_QUERIER_INTERVAL_MAX, NM_BRIDGE_MULTICAST_QUERIER_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_QUERY_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_query_interval, NM_BRIDGE_MULTICAST_QUERY_INTERVAL_MIN, NM_BRIDGE_MULTICAST_QUERY_INTERVAL_MAX, NM_BRIDGE_MULTICAST_QUERY_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_RESPONSE_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_QUERY_RESPONSE_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_query_response_interval, NM_BRIDGE_MULTICAST_QUERY_RESPONSE_INTERVAL_MIN, NM_BRIDGE_MULTICAST_QUERY_RESPONSE_INTERVAL_MAX, NM_BRIDGE_MULTICAST_QUERY_RESPONSE_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_QUERY_USE_IFADDR,
	      NM_SETTING_BRIDGE_MULTICAST_QUERY_USE_IFADDR,
	      ATTR_TYPE_BOOL (multicast_query_use_ifaddr, NM_BRIDGE_MULTICAST_QUERY_USE_IFADDR_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_SNOOPING,
	      NM_SETTING_BRIDGE_MULTICAST_SNOOPING,
	      ATTR_TYPE_BOOL (multicast_snooping, NM_BRIDGE_MULTICAST_SNOOPING_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_STARTUP_QUERY_COUNT,
	      NM_SETTING_BRIDGE_MULTICAST_STARTUP_QUERY_COUNT,
	      ATTR_TYPE_UINT (multicast_startup_query_count, NM_BRIDGE_MULTICAST_STARTUP_QUERY_COUNT_MIN, NM_BRIDGE_MULTICAST_STARTUP_QUERY_COUNT_MAX, NM_BRIDGE_MULTICAST_STARTUP_QUERY_COUNT_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_MULTICAST_STARTUP_QUERY_INTERVAL,
	      NM_SETTING_BRIDGE_MULTICAST_STARTUP_QUERY_INTERVAL,
	      ATTR_TYPE_UINT64 (multicast_startup_query_interval, NM_BRIDGE_MULTICAST_STARTUP_QUERY_INTERVAL_MIN, NM_BRIDGE_MULTICAST_STARTUP_QUERY_INTERVAL_MAX, NM_BRIDGE_MULTICAST_STARTUP_QUERY_INTERVAL_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_VLAN_FILTERING,
	      NM_SETTING_BRIDGE_VLAN_FILTERING,
	      ATTR_TYPE_BOOL (vlan_filtering, FALSE),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_VLAN_DEFAULT_PVID,
	      NM_SETTING_BRIDGE_VLAN_DEFAULT_PVID,
	      ATTR_TYPE_UINT (vlan_default_pvid, 0, NM_BRIDGE_VLAN_VID_MAX, NM_BRIDGE_VLAN_DEFAULT_PVID_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_VLAN_PROTOCOL,
	      NM_SETTING_BRIDGE_VLAN_PROTOCOL,
	      ATTR_TYPE_STRING (vlan_protocol),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_VLAN_STATS_ENABLED,
	      NM_SETTING_BRIDGE_VLAN_STATS_ENABLED,
	      ATTR_TYPE_BOOL (vlan_stats_enabled, NM_BRIDGE_VLAN_STATS_ENABLED_DEF),
	),
	ATTR (NM_PROP_ATTR_BRIDGE_MASTER_VLANS,
	      NM_SETTING_BRIDGE_VLANS,
	      ATTR_TYPE_BOXED_PTR_ARRAY (vlans),
	),
};

#undef CURRENT_SETTING_TYPE
#undef CURRENT_STRUCT_BASE_FIELD

#define CURRENT_SETTING_TYPE       NM_PROP_SETTING_TYPE_BRIDGE_PORT
#define CURRENT_STRUCT_BASE_FIELD  bridge_port
const NMPropAttrType _nm_prop_attr_types_bridge_port[_NM_PROP_ATTR_BRIDGE_PORT_NUM] = {
};

/*****************************************************************************/

void
nm_prop_setting_data_get_property (const NMPropSettingData *data,
                                   const NMPropAttrType *attr_type,
                                   GValue *value)
{
	nm_assert (data);
	nm_assert (attr_type);
	nm_assert (value);
	nm_assert (data->setting_type == attr_type->setting_type);

	switch (attr_type->property_type) {
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (value, *nm_prop_setting_data_get_prop_ptr (bool, data, attr_type));
		break;
	case G_TYPE_UINT:
		g_value_set_uint (value, *nm_prop_setting_data_get_prop_ptr (guint, data, attr_type));
		break;
	case G_TYPE_UINT64:
		g_value_set_uint64 (value, *nm_prop_setting_data_get_prop_ptr (guint64, data, attr_type));
		break;
	case G_TYPE_STRING: {
		g_value_set_string (value, *nm_prop_setting_data_get_prop_ptr (const char *, data, attr_type));
		break;
	}
	default:
		nm_assert_not_reached ();
	}
}

void
nm_prop_setting_data_set_property_clear (NMPropSettingData *data,
                                         const NMPropAttrType *attr_type)
{
	nm_assert (data);
	nm_assert (attr_type);
	nm_assert (data->setting_type == attr_type->setting_type);

	switch (attr_type->property_type) {
	case G_TYPE_BOOLEAN:
		*nm_prop_setting_data_get_prop_ptr (bool, data, attr_type) = FALSE;
		break;
	case G_TYPE_UINT:
		*nm_prop_setting_data_get_prop_ptr (guint, data, attr_type) = 0;
		break;
	case G_TYPE_UINT64:
		*nm_prop_setting_data_get_prop_ptr (guint64, data, attr_type) = 0;
		break;
	case G_TYPE_STRING:
		nm_clear_g_free (nm_prop_setting_data_get_prop_ptr (char *, data, attr_type));
		break;
	case G_TYPE_BOXED:
		if (attr_type->property_typ.boxed.get_gtype_fcn == g_ptr_array_get_type) {
			nm_clear_pointer (nm_prop_setting_data_get_prop_ptr (GPtrArray *, data, attr_type),
			                  g_ptr_array_unref);
		} else
			nm_assert_not_reached ();
		break;
	default:
		nm_assert_not_reached ();
		break;
	}
}

void
nm_prop_setting_data_set_property_default (NMPropSettingData *data,
                                           const NMPropAttrType *attr_type)
{
	nm_assert (data);
	nm_assert (attr_type);
	nm_assert (data->setting_type == attr_type->setting_type);

	switch (attr_type->property_type) {
	case G_TYPE_BOOLEAN:
		*nm_prop_setting_data_get_prop_ptr (bool, data, attr_type) = attr_type->property_typ.boolean.def;
		break;
	case G_TYPE_UINT:
		*nm_prop_setting_data_get_prop_ptr (guint, data, attr_type) = attr_type->property_typ.uint.def;
		break;
	case G_TYPE_UINT64:
		*nm_prop_setting_data_get_prop_ptr (guint64, data, attr_type) = attr_type->property_typ.uint64.def;
		break;
	case G_TYPE_STRING:
		nm_clear_g_free (nm_prop_setting_data_get_prop_ptr (char *, data, attr_type));
		break;
	case G_TYPE_BOXED:
		if (attr_type->property_typ.boxed.get_gtype_fcn == g_ptr_array_get_type) {
			nm_clear_pointer (nm_prop_setting_data_get_prop_ptr (GPtrArray *, data, attr_type),
			                  g_ptr_array_unref);
		} else
			nm_assert_not_reached ();
		break;
	default:
		nm_assert_not_reached ();
		break;
	}
}

void
nm_prop_setting_data_set_property (NMPropSettingData *data,
                                   const NMPropAttrType *attr_type,
                                   const GValue *value)
{
	nm_assert (data);
	nm_assert (attr_type);
	nm_assert (value);
	nm_assert (data->setting_type == attr_type->setting_type);

	switch (attr_type->property_type) {
	case G_TYPE_BOOLEAN:
		*nm_prop_setting_data_get_prop_ptr (bool, data, attr_type) = g_value_get_boolean (value);
		break;
	case G_TYPE_UINT:
		*nm_prop_setting_data_get_prop_ptr (guint, data, attr_type) = g_value_get_uint (value);
		break;
	case G_TYPE_UINT64:
		*nm_prop_setting_data_get_prop_ptr (guint64, data, attr_type) = g_value_get_uint64 (value);
		break;
	case G_TYPE_STRING:
		nm_utils_strdup_reset (nm_prop_setting_data_get_prop_ptr (char *, data, attr_type),
		                       g_value_get_string (value));
		break;
	default:
		nm_assert_not_reached ();
	}
}
