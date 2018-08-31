/*
 * Copyright (C) 2008-2010, Nokia
 * Copyright (C) 2018, Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"
#include "tracker-uuid.h"

#if GLIB_CHECK_VERSION (2, 52, 0)
#include <uuid/uuid.h>
#endif

gchar *
tracker_generate_uuid (void)
{
	gchar *result;
#if GLIB_CHECK_VERSION (2, 52, 0)
	gchar *uuid = g_uuid_string_random ();
	result = g_strdup_printf ("urn:uuid:%s", uuid);
	g_free (uuid);
#else
	uuid_t base = { 0, };
	gchar uuid[37];

	uuid_generate (base);
	uuid_unparse_lower (base, uuid);
	result = g_strdup_printf ("urn:uuid:%s", uuid);
#endif

	return result;
}
