/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2014 Red Hat, Inc.
 */

/**
 * SECTION:nmt-page-vpn
 * @short_description: The editor page for VPN connections
 *
 * #NmtPageVpn creates a group of #NmtEditorSections that are linked together to
 * describe the data in a single #NMSettingVpn.
 *
 * This requires a VPN plugin that is linked against libnm, and that implements
 * the new nm_vpn_editor_get_property_xml() method.
 */

#include "config.h"

#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "nmt-page-vpn.h"
#include "nmt-password-fields.h"
#include "nm-editor-utils.h"

G_DEFINE_TYPE (NmtPageVpn, nmt_page_vpn, NMT_TYPE_EDITOR_PAGE)

#define NMT_PAGE_VPN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NMT_TYPE_PAGE_VPN, NmtPageVpnPrivate))

typedef struct {
	NMSettingVpn *s_vpn;
	GArray *bindings;
} NmtPageVpnPrivate;

NmtEditorPage *
nmt_page_vpn_new (NMConnection *conn)
{
	return g_object_new (NMT_TYPE_PAGE_VPN,
	                     "connection", conn,
	                     NULL);
}

typedef enum {
	NMT_PAGE_VPN_BINDING_PROPERTY,
	NMT_PAGE_VPN_BINDING_STRING,
	NMT_PAGE_VPN_BINDING_PASSWORD,
	NMT_PAGE_VPN_BINDING_BOOLEAN,
	NMT_PAGE_VPN_BINDING_MENUITEM
} NmtPageVpnBindingType;

typedef struct {
	NmtPageVpnBindingType type;
	NmtNewtWidget *widget;
	char *widget_property;
	int widget_index;
	char *vpn_property;
	char *value;
	char *false_value;
} NmtPageVpnBinding;

static void
nmt_page_vpn_binding_clear (NmtPageVpnBinding *binding)
{
	g_free (binding->widget_property);
	g_free (binding->vpn_property);
	g_free (binding->value);
	g_free (binding->false_value);
}

static void
nmt_page_vpn_init (NmtPageVpn *vpn)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);

	priv->bindings = g_array_new (FALSE, FALSE, sizeof (NmtPageVpnBinding));
	g_array_set_clear_func (priv->bindings, (GDestroyNotify) nmt_page_vpn_binding_clear);
}

static void widget_changed (GObject *object, GParamSpec *pspec, gpointer user_data);

/**
 * nmt_page_vpn_bind_widget_property:
 * @vpn: the #NmtPageVpn
 * @widget: a widget
 * @widget_property: a property of @widget; either "sensitive" or "visible"
 * @vpn_property: a vpn-data item
 * @vpn_value: (allow-none): the value of @vpn_property corresponding to %TRUE
 *
 * Creates a one-way binding from @vpn_property in the VPN data to
 * @widget_property on @widget, to allow widgets to be made visible/invisible or
 * sensitive/insensitive based on the current VPN settings.
 *
 * If @vpn_value is non-%NULL, then when @vpn_property has that value,
 * @widget_property will be %TRUE, and when @vpn_property is unset or has any
 * other value, @widget_property will be %FALSE.
 *
 * If @vpn_value is %NULL, then when @vpn_property is unset, @widget_property
 * will be %TRUE, and when @vpn_property is set (to any value), @widget_property
 * will be %FALSE.
 */
static void
nmt_page_vpn_bind_widget_property (NmtPageVpn *vpn,
                                   NmtNewtWidget *widget,
                                   const char *widget_property,
                                   const char *vpn_property,
                                   const char *vpn_value)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding binding;
	const char *val;

	g_return_if_fail (   !strcmp (widget_property, "sensitive")
	                  || !strcmp (widget_property, "visible"));

	val = nm_setting_vpn_get_data_item (priv->s_vpn, vpn_property);
	g_object_set (widget,
	              widget_property, !g_strcmp0 (val, vpn_value),
	              NULL);

	memset (&binding, 0, sizeof (binding));
	binding.type = NMT_PAGE_VPN_BINDING_PROPERTY;
	binding.widget = widget;
	binding.widget_property = g_strdup (widget_property);
	binding.vpn_property = g_strdup (vpn_property);
	binding.value = g_strdup (vpn_value);

	g_array_append_val (priv->bindings, binding);
}

static void
nmt_page_vpn_update_widget_property (NmtPageVpn *vpn,
                                     NmtPageVpnBinding *binding)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	const char *val;

	val = nm_setting_vpn_get_data_item (priv->s_vpn, binding->vpn_property);
	g_object_set (binding->widget,
	              binding->widget_property, !g_strcmp0 (val, binding->value),
	              NULL);
}

/**
 * nmt_page_vpn_bind_string:
 * @vpn: the #NmtPageVpn
 * @widget: a widget
 * @widget_property: a string-valued property of @widget
 * @vpn_property: a vpn-data item
 *
 * Creates a bidirectional binding between @widget_property on @widget and
 * @vpn_property in the VPN data.
 *
 * On construction, @widget_property will be initialized from the value of
 * @vpn_property. (If @vpn_property is unset, @widget_property will be set to
 * "".)
 *
 * When @widget_property changes, its value will be copied to @vpn_property. (If
 * @widget_property has the value "", then @vpn_property will be unset.)
 */
static void
nmt_page_vpn_bind_string (NmtPageVpn *vpn,
                          NmtNewtWidget *widget,
                          const char *widget_property,
                          const char *vpn_property)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding binding;
	const char *val;

	val = nm_setting_vpn_get_data_item (priv->s_vpn, vpn_property);
	if (!val)
		val = "";
	g_object_set (widget, widget_property, val, NULL);

	memset (&binding, 0, sizeof (binding));
	binding.type = NMT_PAGE_VPN_BINDING_STRING;
	binding.widget = widget;
	binding.widget_property = g_strdup (widget_property);
	binding.vpn_property = g_strdup (vpn_property);

	g_array_append_val (priv->bindings, binding);
}

static void
nmt_page_vpn_update_string_setting (NmtPageVpn *vpn,
                                    NmtPageVpnBinding *binding)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	char *val;

	g_object_get (binding->widget, binding->widget_property, &val, NULL);
	if (val && *val)
		nm_setting_vpn_add_data_item (priv->s_vpn, binding->vpn_property, val);
	g_free (val);
}

/**
 * nmt_page_vpn_bind_password:
 * @vpn: the #NmtPageVpn
 * @widget: an #NmtPasswordFields widget
 * @vpn_property: a vpn-data item
 *
 * Creates a bidirectional binding between @widget (an #NmtPasswordFields) and
 * @vpn_property in the VPN data.
 *
 * On construction, @widget's #NmtPasswordFields:password property will be
 * initialized from the value of @vpn_property, and its
 * #NmtPasswordFields:always-ask property will be initialized from the
 * associated secret flags on the #NMSettingVpn.
 *
 * When the widget changes, the corresponding #NMSettingVpn properties will be
 * updated. (If the password field is blank, then @vpn_property will be unset.)
 */
static void
nmt_page_vpn_bind_password (NmtPageVpn *vpn,
                            NmtNewtWidget *widget,
                            const char *vpn_property)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding binding;
	NMSettingSecretFlags flags;
	const char *password;

	password = nm_setting_vpn_get_secret (priv->s_vpn, vpn_property);
	if (!password)
		password = "";
	g_object_set (widget, "password", password, NULL);

	if (!nm_setting_get_secret_flags (NM_SETTING (priv->s_vpn), vpn_property, &flags, NULL))
		flags = 0;
	g_object_set (widget,
	              "always-ask", (flags & NM_SETTING_SECRET_FLAG_NOT_SAVED) != 0,
	              NULL);

	memset (&binding, 0, sizeof (binding));
	binding.type = NMT_PAGE_VPN_BINDING_PASSWORD;
	binding.widget = widget;
	binding.vpn_property = g_strdup (vpn_property);

	g_array_append_val (priv->bindings, binding);
}

static void
nmt_page_vpn_update_password_setting (NmtPageVpn *vpn,
                                      NmtPageVpnBinding *binding)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	gboolean always_ask;
	char *password;

	g_object_get (binding->widget,
	              "password", &password,
	              "always-ask", &always_ask,
	              NULL);
	if (password && *password) {
		nm_setting_vpn_add_secret (priv->s_vpn, binding->vpn_property, password);
		nm_setting_set_secret_flags (NM_SETTING (priv->s_vpn), binding->vpn_property,
		                             always_ask ? NM_SETTING_SECRET_FLAG_NOT_SAVED : 0,
		                             NULL);
	}
	g_free (password);
}

/**
 * nmt_page_vpn_bind_boolean:
 * @vpn: the #NmtPageVpn
 * @widget: a widget
 * @widget_property: a boolean-valued property of @widget
 * @vpn_property: a vpn-data item
 * @true_value: (allow-none): the value of @vpn_property corresponding to %TRUE
 * @false_value: (allow-none): the value of @vpn_property corresponding to %FALSE
 *
 * Creates a bidirectional binding between @widget_property on @widget and
 * @vpn_property in the VPN data.
 *
 * On construction, @widget_property will be initialized from the value of
 * @vpn_property; if it is @true_value (or if @true_value is %NULL and
 * @vpn_property is unset), then @widget_property will be set %TRUE. If
 * @vpn_property is @false_value (or if @false_value is %NULL and @vpn_property
 * is unset), then @widget_property will be set %FALSE.
 *
 * When @widget_property changes, @vpn_property will be set to either
 * @true_value or @false_value, as appropriate (or will be unset if the
 * corresponding value is %NULL).
 */
static void
nmt_page_vpn_bind_boolean (NmtPageVpn *vpn,
                           NmtNewtWidget *widget,
                           const char *widget_property,
                           const char *vpn_property,
                           const char *true_value,
                           const char *false_value)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding binding;
	const char *val;

	g_return_if_fail (true_value != NULL || false_value != NULL);

	val = nm_setting_vpn_get_data_item (priv->s_vpn, vpn_property);
	if (!g_strcmp0 (val, true_value)) {
		/* val == true_value, or val is NULL and the property is unset when TRUE */
		g_object_set (widget, widget_property, TRUE, NULL);
	} else if (!g_strcmp0 (val, false_value)) {
		/* val == false_value, or val is NULL and the property is unset when FALSE */
		g_object_set (widget, widget_property, FALSE, NULL);
	}

	memset (&binding, 0, sizeof (binding));
	binding.type = NMT_PAGE_VPN_BINDING_BOOLEAN;
	binding.widget = widget;
	binding.widget_property = g_strdup (widget_property);
	binding.vpn_property = g_strdup (vpn_property);
	binding.value = g_strdup (true_value);
	binding.false_value = g_strdup (false_value);

	g_array_append_val (priv->bindings, binding);
}

static void
nmt_page_vpn_update_boolean_setting (NmtPageVpn *vpn,
                                     NmtPageVpnBinding *binding)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	gboolean val;

	g_object_get (binding->widget, binding->widget_property, &val, NULL);
	if (val && binding->value)
		nm_setting_vpn_add_data_item (priv->s_vpn, binding->vpn_property, binding->value);
	else if (!val && binding->false_value)
		nm_setting_vpn_add_data_item (priv->s_vpn, binding->vpn_property, binding->false_value);
}

/**
 * nmt_page_vpn_bind_menuitem:
 * @vpn: the #NmtPageVpn
 * @widget: a widget
 * @widget_property: an integer-valued property of @widget
 * @index: the value of @widget_property this binding corresponds to
 * @vpn_property: a vpn-data item
 * @value: (allow-none): the value of @vpn_property for this binding
 *
 * Creates one part of a bidirectional binding between @widget_property on
 * @widget and @vpn_property in the VPN data. Each item in the "menu" (ie, each
 * possible @index value of @widget_property) has its own binding, and the
 * different indexes can have the same or different @vpn_property values.
 *
 * On construction, if @vpn_property has the value @value (or if @value is %NULL
 * and @vpn_property is unset), then @widget_property will be set to @index.
 *
 * When @widget_property changes, all associated menuitem bindings will be
 * updated; The @vpn_property bound to the now-active @index value will be set
 * to @value, and any other properties specified as the @vpn_property of
 * inactive indexes will be unset.
 *
 * So, if all of the bindings on a given menu have the same @vpn_property, then
 * the binding will be used to select what value that property has. If each of
 * the bindings has a different @vpn_property, then the binding will be used to
 * select which of those VPN properties is set.
 */
static void
nmt_page_vpn_bind_menuitem (NmtPageVpn *vpn,
                            NmtNewtWidget *widget,
                            const char *widget_property,
                            int index,
                            const char *vpn_property,
                            const char *value)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding binding;
	const char *val;

	val = nm_setting_vpn_get_data_item (priv->s_vpn, vpn_property);
	if (!g_strcmp0 (val, value))
		g_object_set (widget, widget_property, index, NULL);

	memset (&binding, 0, sizeof (binding));
	binding.type = NMT_PAGE_VPN_BINDING_MENUITEM;
	binding.widget = widget;
	binding.widget_property = g_strdup (widget_property);
	binding.widget_index = index;
	binding.vpn_property = g_strdup (vpn_property);
	binding.value = g_strdup (value);

	g_array_append_val (priv->bindings, binding);
}

static void
nmt_page_vpn_update_menuitem_setting (NmtPageVpn *vpn,
                                      NmtPageVpnBinding *binding)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	int val;

	g_object_get (binding->widget, binding->widget_property, &val, NULL);
	if (val == binding->widget_index)
		nm_setting_vpn_add_data_item (priv->s_vpn, binding->vpn_property, binding->value);
}

static void
remove_item (const char *key, const char *value, gpointer user_data)
{
	NMSettingVpn *s_vpn = user_data;

	nm_setting_vpn_remove_data_item (s_vpn, key);
}

static void
widget_changed (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
	NmtPageVpn *vpn = user_data;
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NmtPageVpnBinding *binding;
	int i;

	if (   !strcmp (pspec->name, "visible")
	    || !strcmp (pspec->name, "sensitive"))
		return;

	/* Rebuild the VPN data from scratch */
	nm_setting_vpn_foreach_data_item (priv->s_vpn, remove_item, priv->s_vpn);
	for (i = 0; i < priv->bindings->len; i++) {
		binding = &g_array_index (priv->bindings, NmtPageVpnBinding, i);

		if (binding->type == NMT_PAGE_VPN_BINDING_STRING)
			nmt_page_vpn_update_string_setting (vpn, binding);
		else if (binding->type == NMT_PAGE_VPN_BINDING_PASSWORD)
			nmt_page_vpn_update_password_setting (vpn, binding);
		else if (binding->type == NMT_PAGE_VPN_BINDING_BOOLEAN)
			nmt_page_vpn_update_boolean_setting (vpn, binding);
		else if (binding->type == NMT_PAGE_VPN_BINDING_MENUITEM)
			nmt_page_vpn_update_menuitem_setting (vpn, binding);
	}

	/* Update widget sensitive/visible properties for any s_vpn changes */
	for (i = 0; i < priv->bindings->len; i++) {
		binding = &g_array_index (priv->bindings, NmtPageVpnBinding, i);

		if (binding->type == NMT_PAGE_VPN_BINDING_PROPERTY)
			nmt_page_vpn_update_widget_property (vpn, binding);
	}
}

static gboolean
certificate_file_filter (NmtNewtFilePicker *picker,
                         const char        *path,
                         GFileInfo         *info,
                         gpointer           user_data)
{
	return nm_utils_file_is_certificate (path);
}

static gboolean
key_file_filter (NmtNewtFilePicker *picker,
                 const char        *path,
                 GFileInfo         *info,
                 gpointer           user_data)
{
	return nm_utils_file_is_private_key (path, NULL);
}

typedef struct {
	NmtPageVpn *vpn;
	NMConnection *conn;
	char *gettext_domain;

	NmtEditorSection *section;

	NmtEditorGrid *grid;

	char *label;
	NmtNewtWidget *widget;

	/* Widget-specific data */
	GPtrArray *checkbox_widgets;
	int item;
} NmtPageVpnParseData;

static void parse_widget_tag  (GMarkupParseContext  *context,
                               const char           *element_name,
                               const char          **attribute_names,
                               const char          **attribute_values,
                               gpointer              user_data,
                               GError              **error);
static void finish_widget_tag (GMarkupParseContext  *context,
                               const char           *element_name,
                               gpointer              user_data,
                               GError              **error);

static const char *
find_attribute (const char  *attribute,
                const char **attribute_names,
                const char **attribute_values)
{
	int i;

	for (i = 0; attribute_names[i]; i++) {
		if (!strcmp (attribute, attribute_names[i]))
			return attribute_values[i];
	}
	return NULL;
}

/* parses a <property> element:
 *
 *   <property name="NAME"/>
 *       For string/password-valued bindings, indicates the property name that
 *       the widget's value will be bound to.
 *
 *   <property name="NAME" value="VALUE"/>
 *       For menuitem bindings; binds an item to a specific value of a property
 *
 *   <property name="NAME" true-value="TRUE-VALUE"/>
 *   <property name="NAME" false-value="FALSE-VALUE"/>
 *   <property name="NAME" true-value="TRUE-VALUE" false-value="FALSE-VALUE"/>
 *       For boolean bindings; sets an associated string value for at least one
 *       state of the widget, and either a string value or %NULL for the other
 *       state.
 */
static void
parse_property_tag (GMarkupParseContext  *context,
                    const char           *element_name,
                    const char          **attribute_names,
                    const char          **attribute_values,
                    gpointer              user_data,
                    GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *name, *value, *true_value, *false_value;

	name = find_attribute ("name", attribute_names, attribute_values);
	if (!name) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag had no '%s' attribute."),
		             "property", "name");
		return;
	}

	value = find_attribute ("value", attribute_names, attribute_values);
	true_value = find_attribute ("true-value", attribute_names, attribute_values);
	false_value = find_attribute ("false-value", attribute_names, attribute_values);

	if (NMT_IS_NEWT_ENTRY (vpd->widget)) {
		nmt_page_vpn_bind_string (vpd->vpn,
		                          vpd->widget, "text",
		                          name);
	} else if (NMT_IS_PASSWORD_FIELDS (vpd->widget)) {
		nmt_page_vpn_bind_password (vpd->vpn,
		                            vpd->widget,
		                            name);
	} else if (NMT_IS_NEWT_FILE_BUTTON (vpd->widget)) {
		nmt_page_vpn_bind_string (vpd->vpn,
		                          vpd->widget, "selection",
		                          name);
	} else if (NMT_IS_NEWT_CHECKBOX (vpd->widget)) {
		nmt_page_vpn_bind_boolean (vpd->vpn,
		                           vpd->widget, "active",
		                           name, true_value, false_value);
	} else if (NMT_IS_NEWT_POPUP (vpd->widget)) {
		nmt_page_vpn_bind_menuitem (vpd->vpn,
		                            vpd->widget, "active", vpd->item,
		                            name, value);
	} else
		g_warning ("don't know how to bind property to %s", G_OBJECT_TYPE_NAME (vpd->widget));
}

/* parses a <modifier> element:
 *
 *   <[visible|sensitive] if="PROPERTY-NAME" value="PROPERTY-VALUE"/>
 *       Indicates that the widget is only visible/sensitive if the indicated
 *       property has the indicated value.
 */
static void
parse_modifier_tag (GMarkupParseContext  *context,
                    const char           *element_name,
                    const char          **attribute_names,
                    const char          **attribute_values,
                    gpointer              user_data,
                    GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *vpn_property, *value;

	vpn_property = find_attribute ("if", attribute_names, attribute_values);
	value = find_attribute ("value", attribute_names, attribute_values);
	if (!vpn_property || !value) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag lacked '%s' or '%s' attribute."),
		             element_name, "if", "value");
		return;
	}

	nmt_page_vpn_bind_widget_property (vpd->vpn,
	                                   /* element_name is either "sensitive" or "visible" */
	                                   vpd->widget, element_name,
	                                   vpn_property, value);
}

/* <item> contents subparser
 *
 * <item>s can contain only <property> elements.
 */
static void
vpn_menu_item_contents_start_element (GMarkupParseContext  *context,
                                      const char           *element_name,
                                      const char          **attribute_names,
                                      const char          **attribute_values,
                                      gpointer              user_data,
                                      GError              **error)
{
	if (!strcmp (element_name, "property")) {
		parse_property_tag (context,
		                    element_name, attribute_names, attribute_values,
		                    user_data, error);
	} else {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <widget> contained unexpected tag <%s>."),
		             element_name);
		return;
	}
}

static GMarkupParser vpn_menu_item_contents_parser = {
	vpn_menu_item_contents_start_element,
	NULL /* end_element */,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* <item> subparser
 *
 *   <item label="LABEL" [default="1"]>
 *       Adds a menu item with the given label. If "default" is set, the label
 *       will have " (Default)" appended to it.
 */
static void
vpn_menu_item_start_element (GMarkupParseContext  *context,
                             const char           *element_name,
                             const char          **attribute_names,
                             const char          **attribute_values,
                             gpointer              user_data,
                             GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *label_attr, *default_attr;
	char *label;

	if (strcmp (element_name, "item") != 0) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <widget type='menu'> contained unexpected tag <%s>."),
		             element_name);
		return;
	}

	label_attr = find_attribute ("label", attribute_names, attribute_values);
	if (!label_attr) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag had no '%s' attribute."),
		             "item", "label");
		return;
	}
	label_attr = g_dgettext (vpd->gettext_domain, label_attr);

	default_attr = find_attribute ("default", attribute_names, attribute_values);
	if (!g_strcmp0 (default_attr, "1")) {
		label = g_strdup_printf (_("%s (default)"), label_attr);
		nmt_newt_popup_append (NMT_NEWT_POPUP (vpd->widget), label, NULL);
		g_free (label);
	} else
		nmt_newt_popup_append (NMT_NEWT_POPUP (vpd->widget), label_attr, NULL);


	g_markup_parse_context_push (context, &vpn_menu_item_contents_parser, user_data);
}

static void
vpn_menu_item_end_element (GMarkupParseContext  *context,
                           const char           *element_name,
                           gpointer              user_data,
                           GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;

	g_markup_parse_context_pop (context);
	vpd->item++;
}

static GMarkupParser vpn_menu_item_parser = {
	vpn_menu_item_start_element,
	vpn_menu_item_end_element,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* generic <widget> contents subparser
 *
 * <widget>s can contain <property>, <sensitive>, and <visible> elements
 */
static void
vpn_widget_contents_start_element (GMarkupParseContext  *context,
                                   const char           *element_name,
                                   const char          **attribute_names,
                                   const char          **attribute_values,
                                   gpointer              user_data,
                                   GError              **error)
{
	if (!strcmp (element_name, "property")) {
		parse_property_tag (context,
		                    element_name, attribute_names, attribute_values,
		                    user_data, error);
	} else if (   !strcmp (element_name, "visible")
	           || !strcmp (element_name, "sensitive")) {
		parse_modifier_tag (context,
		                    element_name, attribute_names, attribute_values,
		                    user_data, error);
	} else {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <widget> contained unexpected tag <%s>."),
		             element_name);
		return;
	}
}

static GMarkupParser vpn_widget_contents_parser = {
	vpn_widget_contents_start_element,
	NULL /* end_element */,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* <widget type="checkbox"> content subparser */
static void
vpn_checkbox_contents_start_element (GMarkupParseContext  *context,
                                     const char           *element_name,
                                     const char          **attribute_names,
                                     const char          **attribute_values,
                                     gpointer              user_data,
                                     GError              **error)
{
	if (strcmp (element_name, "widget") == 0) {
		parse_widget_tag (context,
		                  element_name, attribute_names, attribute_values,
		                  user_data, error);
	} else if (strcmp (element_name, "property") == 0) {
		parse_property_tag (context,
		                    element_name, attribute_names, attribute_values,
		                    user_data, error);
	}
}

static void
vpn_checkbox_contents_end_element (GMarkupParseContext  *context,
                                   const char           *element_name,
                                   gpointer              user_data,
                                   GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;

	if (strcmp (element_name, "widget") == 0) {
		finish_widget_tag (context, element_name, user_data, error);

		if (vpd->label) {
			g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
			             _("Invalid VPN UI data: <%s> in <%s> had a '%s' attribute."),
			             "widget", "checkbox", "label");
			return;
		}

		g_ptr_array_add (vpd->checkbox_widgets, vpd->widget);

		/* Reset vpd->widget to the checkbox widget itself */
		vpd->widget = vpd->checkbox_widgets->pdata[0];
	} else if (strcmp (element_name, "property") == 0) {
		;
	} else
		g_assert_not_reached ();
}

static GMarkupParser vpn_checkbox_contents_parser = {
	vpn_checkbox_contents_start_element,
	vpn_checkbox_contents_end_element,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* parses a <widget> element; used by the <section> and <checkbox> contents subparsers
 *
 *   <widget type="TYPE" [label="LABEL"] ...>
 *       type is mandatory. label is mandatory except for labels, and subwidgets
 *       of checkboxes.  other properties are type-specific
 *
 *   <widget type="separator">
 *       Separator (ie, blank line)
 *
 *   <widget type="string">
 *       Generic string-valued property
 *
 *   <widget type="number" min="MIN" max="MAX">
 *       Numeric property
 *
 *   <widget type="host">
 *       Hostname property (currently identical to type="string")
 *
 *   <widget type="password">
 *       Password property
 *
 *   <widget type="menu">
 *       Creates a pop-up menu, specified by <item> child elements
 *
 *   <widget type="certificate-file">
 *   <widget type="key-file">
 *       Creates a file picker widget for picking a certificate/key
 *
 *   <widget type="docstring">
 *       Creates a textbox providing documentation. (FIXME, not implemented)
 *
 *   <widget type="checkbox">
 *       Creates a checkbox.  Checkboxes can contain sub-<widget>s, which will
 *       end up being displayed to the right of the checkbox and are
 *       automatically only sensitive if the checkbox is checked. Eg,
 *       "[ ] Use custom port: [____]"
 */
static void
parse_widget_tag (GMarkupParseContext  *context,
                  const char           *element_name,
                  const char          **attribute_names,
                  const char          **attribute_values,
                  gpointer              user_data,
                  GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *label, *type;
	GMarkupParser *sub_parser = &vpn_widget_contents_parser;

	type = find_attribute ("type", attribute_names, attribute_values);
	if (!type) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag had no '%s' attribute."),
		             element_name, "type");
		return;
	}

	label = find_attribute ("label", attribute_names, attribute_values);
	if (!label && strcmp (type, "separator") != 0) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag had no '%s' attribute."),
		             element_name, "label");
		return;
	}
	vpd->label = g_strdup (g_dgettext (vpd->gettext_domain, label));

	if (strcmp (type, "separator") == 0) {
		vpd->widget = nmt_newt_separator_new ();
	} else if (strcmp (type, "string") == 0) {
		vpd->widget = nmt_newt_entry_new (40, 0);
	} else if (strcmp (type, "number") == 0) {
		const char *min = find_attribute ("min", attribute_names, attribute_values);
		const char *max = find_attribute ("max", attribute_names, attribute_values);

		if (!min || !max) {
			g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
			             _("Invalid VPN UI data: '%s' widget lacked '%s' or '%s' attribute."),
			             type, "min", "max");
			return;
		}

		vpd->widget = nmt_newt_entry_numeric_new (10, atoi (min), atoi (max));
	} else if (strcmp (type, "host") == 0) {
		vpd->widget = nmt_newt_entry_new (40, 0);
#if 0
		nmt_newt_entry_set_filter (NMT_NEWT_ENTRY (vpd->widget), host_filter, NULL);
		nmt_newt_entry_set_validator (NMT_NEWT_ENTRY (vpd->widget), host_validator, NULL);
#endif
	} else if (strcmp (type, "password") == 0) {
		vpd->widget = nmt_password_fields_new (40, (NMT_PASSWORD_FIELDS_ALWAYS_ASK |
		                                            NMT_PASSWORD_FIELDS_SHOW_PASSWORD));
	} else if (strcmp (type, "menu") == 0) {
		vpd->widget = nmt_newt_popup_new (NULL);
		vpd->item = 0;
		sub_parser = &vpn_menu_item_parser;
	} else if (strcmp (type, "certificate-file") == 0) {
		vpd->widget = nmt_newt_file_button_new (_("Select a certificate file"));
		nmt_newt_file_picker_set_filter (NMT_NEWT_FILE_PICKER (vpd->widget),
		                                 certificate_file_filter, NULL, NULL);
	} else if (strcmp (type, "key-file") == 0) {
		vpd->widget = nmt_newt_file_button_new (_("Select a private key file"));
		nmt_newt_file_picker_set_filter (NMT_NEWT_FILE_PICKER (vpd->widget),
		                                 key_file_filter, NULL, NULL);
#if 0
	} else if (strcmp (type, "docstring") == 0) {
		vpd->widget = nmt_newt_textbox_new (0, 0);
#endif
	} else if (strcmp (type, "checkbox") == 0) {
		if (vpd->checkbox_widgets) {
			g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
			             _("Invalid VPN UI data: tried to nest <%s> widgets."),
			             "checkbox");
			return;
		}

		vpd->widget = nmt_newt_checkbox_new (vpd->label);
		g_clear_pointer (&vpd->label, g_free);

		vpd->checkbox_widgets = g_ptr_array_new ();
		g_ptr_array_add (vpd->checkbox_widgets, vpd->widget);
		sub_parser = &vpn_checkbox_contents_parser;
	} else {
#if 0
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: Unrecognized widget type '%s'."),
		             type);
		return;
#else
		vpd->widget = nmt_newt_label_new (type);
#endif
	}

	g_markup_parse_context_push (context, sub_parser, user_data);
}

static void
finish_widget_tag (GMarkupParseContext  *context,
                   const char           *element_name,
                   gpointer              user_data,
                   GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;

	g_markup_parse_context_pop (context);

	if (vpd->checkbox_widgets) {
		if (vpd->checkbox_widgets->len > 1) {
			NmtNewtGrid *grid;
			NmtNewtWidget *child;
			int i;

			vpd->widget = nmt_newt_grid_new ();
			grid = NMT_NEWT_GRID (vpd->widget);

			for (i = 0; i < vpd->checkbox_widgets->len; i++) {
				child = vpd->checkbox_widgets->pdata[i];
				if (i != 0)
					nmt_newt_widget_set_padding (child, 1, 0, 0, 0);
				nmt_newt_grid_add (grid, child, i, 0);
			}
		}
		g_clear_pointer (&vpd->checkbox_widgets, g_ptr_array_unref);
	}
}

/* <section> content subparser
 *
 * <section>s can currently only contain <widget>s
 */
static void
vpn_section_body_start_element (GMarkupParseContext  *context,
                                const char           *element_name,
                                const char          **attribute_names,
                                const char          **attribute_values,
                                gpointer              user_data,
                                GError              **error)
{
	if (strcmp (element_name, "widget") == 0) {
		parse_widget_tag (context,
		                  element_name, attribute_names, attribute_values,
		                  user_data, error);
	} else {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: expected <%s> tag."),
		             "widget");
		return;
	}
}

static void
vpn_section_body_end_element (GMarkupParseContext  *context,
                              const char           *element_name,
                              gpointer              user_data,
                              GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;

	if (strcmp (element_name, "widget") == 0) {
		finish_widget_tag (context, element_name, user_data, error);

		nmt_editor_grid_append (vpd->grid, vpd->label, vpd->widget, NULL);
		g_clear_pointer (&vpd->label, g_free);
		vpd->widget = NULL;
	} else
		g_assert_not_reached ();
}

static GMarkupParser vpn_section_body_parser = {
	vpn_section_body_start_element,
	vpn_section_body_end_element,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* <section> subparser
 *
 *   <section label="LABEL" [advanced="1"]>
 *       Creates a section with the given name. If "advanced" is set, then
 *       the section is considered "advanced" (eg, it is not displayed by
 *       default).
 */
static void
vpn_section_start_element (GMarkupParseContext  *context,
                           const char           *element_name,
                           const char          **attribute_names,
                           const char          **attribute_values,
                           gpointer              user_data,
                           GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *label, *advanced;
	char *upper_label, *title;

	if (strcmp (element_name, "section") != 0) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: expected <%s> tag."),
		             "section");
		return;
	}

	label = find_attribute ("label", attribute_names, attribute_values);
	if (!label) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: <%s> tag had no '%s' attribute."),
		             "section", "label");
		return;
	}
	label = g_dgettext (vpd->gettext_domain, label);

	upper_label = g_utf8_strup (label, -1);
	title = g_strdup_printf (_("VPN - %s"), upper_label);

	advanced = find_attribute ("advanced", attribute_names, attribute_values);

	vpd->section = g_object_new (NMT_TYPE_EDITOR_SECTION,
	                             "title", title,
	                             "show-by-default", g_strcmp0 (advanced, "1") != 0,
	                             NULL);
	vpd->grid = nmt_editor_section_get_body (vpd->section);

	g_free (title);
	g_free (upper_label);

	g_markup_parse_context_push (context, &vpn_section_body_parser, user_data);
}

static void
vpn_section_end_element (GMarkupParseContext  *context,
                         const char           *element_name,
                         gpointer              user_data,
                         GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;

	g_markup_parse_context_pop (context);

	nmt_editor_page_add_section (NMT_EDITOR_PAGE (vpd->vpn), vpd->section);
	vpd->section = NULL;
}

static GMarkupParser vpn_section_parser = {
	vpn_section_start_element,
	vpn_section_end_element,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/* Top-level parser */
static void
vpn_toplevel_start_element (GMarkupParseContext  *context,
                            const char           *element_name,
                            const char          **attribute_names,
                            const char          **attribute_values,
                            gpointer              user_data,
                            GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	const char *domain;

	if (strcmp (element_name, "nm-vpn-properties") != 0) {
		g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
		             _("Invalid VPN UI data: expected <%s> tag."),
		             "nm-vpn-properties");
		return;
	}

	domain = find_attribute ("gettext-domain", attribute_names, attribute_values);
	if (domain)
		vpd->gettext_domain = g_strdup (domain);

	g_markup_parse_context_push (context, &vpn_section_parser, user_data);
}

static void
vpn_toplevel_end_element (GMarkupParseContext  *context,
                          const char           *element_name,
                          gpointer              user_data,
                          GError              **error)
{
	NmtPageVpnParseData *vpd = user_data;
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpd->vpn);
	NmtPageVpnBinding *binding;
	char *signal;
	int i;

	g_markup_parse_context_pop (context);

	/* Hook up bindings */
	for (i = 0; i < priv->bindings->len; i++) {
		binding = &g_array_index (priv->bindings, NmtPageVpnBinding, i);
		if (binding->type == G_TYPE_NONE)
			continue;

		signal = g_strdup_printf ("notify::%s", binding->widget_property);
		g_signal_connect (binding->widget, signal, G_CALLBACK (widget_changed), vpd->vpn);
		g_free (signal);
	}

	g_free (vpd->gettext_domain);
}

static GMarkupParser vpn_toplevel_parser = {
	vpn_toplevel_start_element,
	vpn_toplevel_end_element,
	NULL /* text */,
	NULL /* passthrough */,
	NULL /* error */
};

/**********/

static void
nmt_page_vpn_constructed (GObject *object)
{
	NmtPageVpn *vpn = NMT_PAGE_VPN (object);
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (vpn);
	NMConnection *conn;
	NmtPageVpnParseData vpd;
	GMarkupParseContext *ctx;
	char *text;
	GError *error = NULL;

	conn = nmt_editor_page_get_connection (NMT_EDITOR_PAGE (vpn));
	priv->s_vpn = nm_connection_get_setting_vpn (conn);
	if (!priv->s_vpn) {
		nm_connection_add_setting (conn, nm_setting_vpn_new ());
		priv->s_vpn = nm_connection_get_setting_vpn (conn);
	}

	memset (&vpd, 0, sizeof (vpd));
	vpd.vpn = vpn;
	vpd.conn = conn;

	g_file_get_contents ("/home/danw/gnome/network-manager-vpnc/props.xml", &text, NULL, &error);
	g_assert_no_error (error);

	ctx = g_markup_parse_context_new (&vpn_toplevel_parser, 0, &vpd, NULL);
	g_markup_parse_context_parse (ctx, text, -1, &error);
	if (error) {
		int line, charpos;

		g_markup_parse_context_get_position (ctx, &line, &charpos);
		g_error ("Parse failed at line %d char %d: %s", line, charpos, error->message);
	}
	g_free (text);
	g_markup_parse_context_free (ctx);

	G_OBJECT_CLASS (nmt_page_vpn_parent_class)->constructed (object);
}

static void
nmt_page_vpn_finalize (GObject *object)
{
	NmtPageVpnPrivate *priv = NMT_PAGE_VPN_GET_PRIVATE (object);

	g_array_unref (priv->bindings);

	G_OBJECT_CLASS (nmt_page_vpn_parent_class)->finalize (object);
}

static void
nmt_page_vpn_class_init (NmtPageVpnClass *vpn_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (vpn_class);

	g_type_class_add_private (vpn_class, sizeof (NmtPageVpnPrivate));

	object_class->constructed = nmt_page_vpn_constructed;
	object_class->finalize = nmt_page_vpn_finalize;
}

