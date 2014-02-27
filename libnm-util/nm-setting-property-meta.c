/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Dan Williams <dcbw@redhat.com>
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
 * (C) Copyright 2014 Red Hat, Inc.
 */

#include "nm-setting-property-meta.h"



G_DEFINE_ABSTRACT_TYPE (NMSettingPropertyMeta, nm_setting_property_meta, G_TYPE_OBJECT)

#define NM_SETTING_PROPERTY_META_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_PROPERTY_META, NMSettingPropertyMetaPrivate))

typedef struct {
} NMSettingPropertyMetaPrivate;

enum {
	PROP_0,
	PROP_LAST
};




static void
nm_setting_property_meta_class_init (NMSettingPropertyMetaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMSettingPropertyMetaPrivate));

	/* virtual methods */

	/* Properties */

}



