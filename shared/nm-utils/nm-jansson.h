/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright 2018 Red Hat, Inc.
 */

#ifndef __NM_JANSSON_H__
#define __NM_JANSSON_H__

/* you need to include at least "config.h" first, possibly "nm-default.h". */

#if WITH_JANSSON

#include <jansson.h>
#include <dlfcn.h>

/*
 * 'json_object_iter_next' symbol clashes with libjson-glib: as
 * gnome-control-center links both libjson-glib and libnm, we would
 * end up resolving 'json_object_iter_next' in the wrong library when
 * calling libnm-core functions in g-c-c.
 * Expose a wrapper to allow calling 'json_object_iter_next' from
 * libjansson in libnm-core when a program binds both libjson-glib
 * and libnm (leverage dlopen()).
 */
static void __attribute__((used))
*nm_json_object_iter_next (json_t *json, void *iter, GError **error)
{
	const char *libjansson = "libjansson.so.4";
	const char *jsymbol = "json_object_iter_next";
	void *handle;
	void *retval = NULL;
	void *(*iter_next)(json_t *json, void *iter);
	char *dl_error;
	gboolean already_open = TRUE;

	g_return_val_if_fail (!error || !*error, NULL);

	handle = dlopen (libjansson, RTLD_NOLOAD | RTLD_LAZY);
	if (!handle) {
		dlerror ();
		handle = dlopen (libjansson, RTLD_LAZY);
		if (!handle) {
			g_set_error (error, NM_UTILS_ERROR, NM_UTILS_ERROR_UNKNOWN,
			             _("cannot dlopen '%s': %s"), libjansson, dlerror ());
			goto done;
		}
		already_open = FALSE;
	}

	dlerror ();
	*(void **) (&iter_next) = dlsym (handle, jsymbol);
	dl_error = dlerror ();

	if (error != NULL) {
		g_set_error (error, NM_UTILS_ERROR, NM_UTILS_ERROR_UNKNOWN,
		             _("cannot dlsym symbol '%s':%s"), jsymbol, dl_error);
		goto done;
	}

	retval = iter_next (json, iter);
done:
	if (!already_open)
		dlclose (handle);
	return retval;
}

/* Clone of 'json_object_foreach' skipping 'json_object_iter_next' in
 * favor of the 'nm_json_object_iter_next' wrapper */
#define nm_json_object_foreach(object, key, value) \
    for(key = json_object_iter_key(json_object_iter(object)); \
        key && (value = json_object_iter_value(json_object_iter_at (object, key) )); \
        key = json_object_iter_key(nm_json_object_iter_next(object, json_object_iter_at (object, key), NULL)))
/* Clone of 'json_object_foreach_safe' skipping 'json_object_iter_next'
 * in favor of the 'nm_json_object_iter_next' wrapper */
#if JANSSON_VERSION_HEX < 0x020300
#define nm_json_object_foreach_safe(object, n, key, value)     \
    for (key = json_object_iter_key (json_object_iter (object)), \
         n = nm_json_object_iter_next (object, json_object_iter_at (object, key), NULL); \
         key && (value = json_object_iter_value (json_object_iter_at (object, key))); \
         key = json_object_iter_key (n), \
         n = nm_json_object_iter_next (object, json_object_iter_at (object, key), NULL))
#else
#define nm_json_object_foreach_safe(object, n, key, value)     \
    for(key = json_object_iter_key(json_object_iter(object)), \
            n = nm_json_object_iter_next(object, json_object_key_to_iter(key), NULL); \
        key && (value = json_object_iter_value(json_object_key_to_iter(key))); \
        key = json_object_iter_key(n), \
            n = nm_json_object_iter_next(object, json_object_key_to_iter(key), NULL))
#endif

/* Added in Jansson v2.3 (released Jan 27 2012) */
#ifndef json_object_foreach
#define json_object_foreach(object, key, value) \
    for(key = json_object_iter_key(json_object_iter(object)); \
        key && (value = json_object_iter_value(json_object_iter_at (object, key) )); \
        key = json_object_iter_key(json_object_iter_next(object, json_object_iter_at (object, key))))
#endif

/* Added in Jansson v2.4 (released Sep 23 2012), but travis.ci has v2.2. */
#ifndef json_boolean
#define json_boolean(val) ((val) ? json_true() : json_false())
#endif

/* Added in Jansson v2.5 (released Sep 19 2013), but travis.ci has v2.2. */
#ifndef json_array_foreach
#define json_array_foreach(array, index, value) \
    for(index = 0; \
        index < json_array_size(array) && (value = json_array_get(array, index)); \
        index++)
#endif

/* Added in Jansson v2.7 */
#ifndef json_boolean_value
#define json_boolean_value json_is_true
#endif

/* Added in Jansson v2.8 */
#ifndef json_object_foreach_safe
#if JANSSON_VERSION_HEX < 0x020300
#define json_object_foreach_safe(object, n, key, value)     \
    for (key = json_object_iter_key (json_object_iter (object)), \
         n = json_object_iter_next (object, json_object_iter_at (object, key)); \
         key && (value = json_object_iter_value (json_object_iter_at (object, key))); \
         key = json_object_iter_key (n), \
         n = json_object_iter_next (object, json_object_iter_at (object, key)))
#else
#define json_object_foreach_safe(object, n, key, value)     \
    for(key = json_object_iter_key(json_object_iter(object)), \
            n = json_object_iter_next(object, json_object_key_to_iter(key)); \
        key && (value = json_object_iter_value(json_object_key_to_iter(key))); \
        key = json_object_iter_key(n), \
            n = json_object_iter_next(object, json_object_key_to_iter(key)))
#endif
#endif

#endif /* WITH_JANSON */

#endif  /* __NM_JANSSON_H__ */
