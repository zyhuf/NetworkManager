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

#ifndef __NM_SETTING_PROPERTY_META_H__
#define __NM_SETTING_PROPERTY_META_H__


#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define NM_TYPE_SETTING_PROPERTY_META            (nm_setting_get_type ())
#define NM_SETTING_PROPERTY_META(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_PROPERTY_META, NMSetting))
#define NM_SETTING_PROPERTY_META_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_PROPERTY_META, NMSettingClass))
#define NM_IS_SETTING_PROPERTY_META(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_PROPERTY_META))
#define NM_IS_SETTING_PROPERTY_META_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_SETTING_PROPERTY_META))
#define NM_SETTING_PROPERTY_META_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_PROPERTY_META, NMSettingClass))

/**
 * NMSettingPropertyMeta:
 *
 * The NMSettingPropertyMeta struct contains only private data.
 * It should only be accessed through the functions described below.
 */
typedef struct {
	GObject parent;
} NMSettingPropertyMeta;



typedef struct {
	GObjectClass parent;

	/* Padding for future expansion */
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
	void (*_reserved5) (void);
	void (*_reserved6) (void);
	void (*_reserved7) (void);
	void (*_reserved8) (void);
	void (*_reserved9) (void);
	void (*_reserved10) (void);
	void (*_reserved11) (void);
	void (*_reserved12) (void);
	void (*_reserved13) (void);
	void (*_reserved14) (void);
	void (*_reserved15) (void);
	void (*_reserved16) (void);
	void (*_reserved17) (void);
	void (*_reserved18) (void);
	void (*_reserved19) (void);
	void (*_reserved20) (void);
} NMSettingPropertyMetaClass;


GType nm_setting_property_meta_get_type (void);

G_END_DECLS

#endif /* __NM_SETTING_PROPERTY_META_H__ */
