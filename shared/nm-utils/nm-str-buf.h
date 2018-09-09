/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
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
 * (C) Copyright 2018 Red Hat, Inc.
 */

#include "nm-shared-utils.h"

/*****************************************************************************/

/* NMStrBuf is not unlike GString. The main difference is that it can use
 * nm_explicit_bzero() when growing the buffer. */
typedef struct {
	char *str;
	gsize len;
	gsize allocated;
	bool do_bzero_mem;
} NMStrBuf;

static inline void
_nm_str_buf_assert (NMStrBuf *strbuf)
{
	nm_assert (strbuf);
	nm_assert (strbuf->str);
	nm_assert (strbuf->allocated > 0);
	nm_assert (strbuf->len <= strbuf->allocated);
}

static inline void
nm_str_buf_init (NMStrBuf *strbuf,
                 gsize len,
                 bool do_bzero_mem)
{
	strbuf->do_bzero_mem = do_bzero_mem;
	strbuf->allocated = len;
	strbuf->str = g_malloc (len);
	strbuf->len = 0;

	_nm_str_buf_assert (strbuf);
}

static inline void
nm_str_buf_grow (NMStrBuf *strbuf,
                 gsize new_len)
{
	_nm_str_buf_assert (strbuf);
	nm_assert (new_len > strbuf->allocated);

	strbuf->str = nm_secret_mem_realloc (strbuf->str, strbuf->do_bzero_mem, strbuf->allocated, new_len);
	strbuf->allocated = new_len;
}

static inline void
nm_str_buf_ensure_space (NMStrBuf *strbuf,
                         gsize reserve)
{
	_nm_str_buf_assert (strbuf);
	nm_assert (reserve > 0);

	if (G_UNLIKELY (reserve > strbuf->allocated - strbuf->len)) {
		nm_str_buf_grow (strbuf,
		                 nm_utils_get_next_realloc_size (!strbuf->do_bzero_mem,
		                                                 strbuf->len + reserve));
	}
}

static inline void
nm_str_buf_append_c (NMStrBuf *strbuf,
                     char ch)
{
	nm_str_buf_ensure_space (strbuf, 2);
	strbuf->str[strbuf->len++] = ch;
}

static inline void
nm_str_buf_append_c2 (NMStrBuf *strbuf,
                      char ch0,
                      char ch1)
{
	nm_str_buf_ensure_space (strbuf, 3);
	strbuf->str[strbuf->len++] = ch0;
	strbuf->str[strbuf->len++] = ch1;
}

static inline void
nm_str_buf_append_c4 (NMStrBuf *strbuf,
                      char ch0,
                      char ch1,
                      char ch2,
                      char ch3)
{
	nm_str_buf_ensure_space (strbuf, 5);
	strbuf->str[strbuf->len++] = ch0;
	strbuf->str[strbuf->len++] = ch1;
	strbuf->str[strbuf->len++] = ch2;
	strbuf->str[strbuf->len++] = ch3;
}

static inline void
nm_str_buf_append_len (NMStrBuf *strbuf,
                       const char *str,
                       gsize len)
{
	if (len > 0) {
		nm_str_buf_ensure_space (strbuf, len + 1);
		memcpy (&strbuf->str[strbuf->len], str, len);
		strbuf->len += len;
	}
}

static inline void
nm_str_buf_append (NMStrBuf *strbuf,
                   const char *str)
{
	nm_str_buf_append_len (strbuf, str, strlen (str));
}

/**
 * nm_str_buf_finalize:
 * @strbuf: an initilized #NMStrBuf
 * @out_len: (out): (allow-none): optional output
 *   argument with the length of the returned string.
 *
 * Returns: (transfer full): the string of the buffer
 *   which must be freed by the caller. The @strbuf
 *   is afterwards in undefined state, though it can be
 *   reused after nm_str_buf_init(). */
static inline char *
nm_str_buf_finalize (NMStrBuf *strbuf,
                     gsize *out_len)
{
	_nm_str_buf_assert (strbuf);

	NM_SET_OUT (out_len, strbuf->len);
	if (G_UNLIKELY (strbuf->allocated == strbuf->len))
		nm_str_buf_grow (strbuf, strbuf->allocated + 1);
	strbuf->str[strbuf->len] = '\0';

	/* the buffer is in invalid state afterwards, however, we clear it
	 * so far, that nm_auto_str_buf is happy.  */
	return g_steal_pointer (&strbuf->str);
}

/**
 * nm_str_buf_destroy:
 * @strbuf: an initialized #NMStrBuf
 *
 * Frees the associated memory of @strbuf. The buffer
 * afterwards is in undefined state, but can be re-initialized
 * with nm_str_buf_init().
 */
static inline void
nm_str_buf_destroy (NMStrBuf *strbuf)
{
	if (!strbuf->str)
		return;
	_nm_str_buf_assert (strbuf);
	if (strbuf->do_bzero_mem)
		nm_explicit_bzero (strbuf->str, strbuf->allocated);
	g_free (strbuf->str);

	/* the buffer is in invalid state afterwards, however, we clear it
	 * so far, that nm_auto_str_buf is happy when calling
	 * nm_str_buf_destroy() again.  */
	strbuf->str = NULL;
}

#define nm_auto_str_buf    nm_auto (nm_str_buf_destroy)
