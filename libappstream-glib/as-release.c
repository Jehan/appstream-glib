/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:as-release
 * @short_description: Object representing a single upstream release
 * @include: appstream-glib.h
 * @stability: Stable
 *
 * This object represents a single upstream release, typically a minor update.
 * Releases can contain a localized description of paragraph and list elements
 * and also have a version number and timestamp.
 *
 * Releases can be automatically generated by parsing upstream ChangeLogs or
 * .spec files, or can be populated using AppData files.
 *
 * See also: #AsApp
 */

#include "config.h"

#include <stdlib.h>

#include "as-checksum-private.h"
#include "as-node-private.h"
#include "as-ref-string.h"
#include "as-release-private.h"
#include "as-tag.h"
#include "as-utils-private.h"
#include "as-yaml.h"

typedef struct
{
	AsUrgencyKind		 urgency;
	AsReleaseKind		 kind;
	AsReleaseState		 state;
	guint64			*sizes;
	AsRefString		*version;
	GHashTable		*blobs;		/* of AsRefString:GBytes */
	GHashTable		*descriptions;
	guint64			 timestamp;
	GPtrArray		*locations;	/* of AsRefString, lazy */
	GPtrArray		*checksums;	/* of AsChecksum, lazy */
} AsReleasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (AsRelease, as_release, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (as_release_get_instance_private (o))

static void
as_release_finalize (GObject *object)
{
	AsRelease *release = AS_RELEASE (object);
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->sizes);
	if (priv->version != NULL)
		as_ref_string_unref (priv->version);
	if (priv->blobs != NULL)
		g_hash_table_unref (priv->blobs);
	if (priv->checksums != NULL)
		g_ptr_array_unref (priv->checksums);
	if (priv->locations != NULL)
		g_ptr_array_unref (priv->locations);
	if (priv->descriptions != NULL)
		g_hash_table_unref (priv->descriptions);

	G_OBJECT_CLASS (as_release_parent_class)->finalize (object);
}

static void
as_release_init (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->urgency = AS_URGENCY_KIND_UNKNOWN;
	priv->kind = AS_RELEASE_KIND_UNKNOWN;
	priv->state = AS_RELEASE_STATE_UNKNOWN;
}

static void
as_release_ensure_checksums  (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->checksums != NULL)
		return;
	priv->checksums = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
as_release_ensure_sizes  (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->sizes != NULL)
		return;
	priv->sizes = g_new0 (guint64, AS_SIZE_KIND_LAST);
}

static void
as_release_ensure_locations  (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->locations != NULL)
		return;
	priv->locations = g_ptr_array_new_with_free_func ((GDestroyNotify) as_ref_string_unref);
}

static void
as_release_ensure_blobs  (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->blobs != NULL)
		return;
	priv->blobs = g_hash_table_new_full (g_str_hash, g_str_equal,
					     (GDestroyNotify) as_ref_string_unref,
					     (GDestroyNotify) g_bytes_unref);
}

static void
as_release_class_init (AsReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = as_release_finalize;
}
/**
 * as_release_kind_to_string:
 * @kind: the #AsReleaseKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.7.6
 **/
const gchar*
as_release_kind_to_string (AsReleaseKind kind)
{
	if (kind == AS_RELEASE_KIND_STABLE)
		return "stable";
	if (kind == AS_RELEASE_KIND_DEVELOPMENT)
		return "development";
	return "unknown";
}

/**
 * as_release_kind_from_string:
 * @kind_str: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: an #AsReleaseKind or %AS_RELEASE_KIND_UNKNOWN for unknown
 *
 * Since: 0.7.6
 **/
AsReleaseKind
as_release_kind_from_string (const gchar *kind_str)
{
	if (g_strcmp0 (kind_str, "stable") == 0)
		return AS_RELEASE_KIND_STABLE;
	if (g_strcmp0 (kind_str, "development") == 0)
		return AS_RELEASE_KIND_DEVELOPMENT;
	return AS_RELEASE_KIND_UNKNOWN;
}

/**
 * as_release_state_from_string:
 * @state: a string
 *
 * Converts the text representation to an enumerated value.
 *
 * Return value: A #AsReleaseState, e.g. %AS_RELEASE_STATE_INSTALLED.
 *
 * Since: 0.6.6
 **/
AsReleaseState
as_release_state_from_string (const gchar *state)
{
	if (g_strcmp0 (state, "installed") == 0)
		return AS_RELEASE_STATE_INSTALLED;
	if (g_strcmp0 (state, "available") == 0)
		return AS_RELEASE_STATE_AVAILABLE;
	return AS_APP_MERGE_KIND_NONE;
}

/**
 * as_release_state_to_string:
 * @state: the #AsReleaseState, e.g. %AS_RELEASE_STATE_INSTALLED
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @state, or %NULL for unknown
 *
 * Since: 0.6.6
 **/
const gchar *
as_release_state_to_string (AsReleaseState state)
{
	if (state == AS_RELEASE_STATE_INSTALLED)
		return "installed";
	if (state == AS_RELEASE_STATE_AVAILABLE)
		return "available";
	return NULL;
}

/**
 * as_release_vercmp:
 * @rel1: a #AsRelease instance.
 * @rel2: a #AsRelease instance.
 *
 * Compares two release.
 *
 * Returns: -1 if rel1 > rel2, +1 if rel1 < rel2, 0 otherwise
 *
 * Since: 0.4.2
 **/
gint
as_release_vercmp (AsRelease *rel1, AsRelease *rel2)
{
	AsReleasePrivate *priv1 = GET_PRIVATE (rel1);
	AsReleasePrivate *priv2 = GET_PRIVATE (rel2);
	gint val;

	/* prefer the timestamp */
	if (priv1->timestamp > priv2->timestamp)
		return -1;
	if (priv1->timestamp < priv2->timestamp)
		return 1;

	/* fall back to the version strings */
	val = as_utils_vercmp (priv2->version, priv1->version);
	if (val != G_MAXINT)
		return val;

	return 0;
}

/**
 * as_release_get_size:
 * @release: a #AsRelease instance
 * @kind: a #AsSizeKind, e.g. #AS_SIZE_KIND_DOWNLOAD
 *
 * Gets the release size.
 *
 * Returns: The size in bytes, or 0 for unknown.
 *
 * Since: 0.5.2
 **/
guint64
as_release_get_size (AsRelease *release, AsSizeKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (kind >= AS_SIZE_KIND_LAST)
		return 0;
	if (priv->sizes == NULL)
		return 0;
	return priv->sizes[kind];
}

/**
 * as_release_set_size:
 * @release: a #AsRelease instance
 * @kind: a #AsSizeKind, e.g. #AS_SIZE_KIND_DOWNLOAD
 * @size: a size in bytes, or 0 for unknown
 *
 * Sets the release size.
 *
 * Since: 0.5.2
 **/
void
as_release_set_size (AsRelease *release, AsSizeKind kind, guint64 size)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (kind >= AS_SIZE_KIND_LAST)
		return;
	as_release_ensure_sizes (release);
	priv->sizes[kind] = size;
}

/**
 * as_release_get_urgency:
 * @release: a #AsRelease instance.
 *
 * Gets the release urgency.
 *
 * Returns: enumberated value, or %AS_URGENCY_KIND_UNKNOWN for not set or invalid
 *
 * Since: 0.5.1
 **/
AsUrgencyKind
as_release_get_urgency (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->urgency;
}

/**
 * as_release_get_state:
 * @release: a #AsRelease instance.
 *
 * Gets the release state.
 *
 * Returns: enumberated value, or %AS_RELEASE_STATE_UNKNOWN for not set or invalid
 *
 * Since: 0.5.8
 **/
AsReleaseState
as_release_get_state (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->state;
}

/**
 * as_release_get_kind:
 * @release: a #AsRelease instance.
 *
 * Gets the type of the release.
 *
 * Returns: enumerated value, e.g. %AS_RELEASE_KIND_STABLE
 *
 * Since: 0.7.6
 **/
AsReleaseKind
as_release_get_kind (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->kind;
}

/**
 * as_release_get_version:
 * @release: a #AsRelease instance.
 *
 * Gets the release version.
 *
 * Returns: string, or %NULL for not set or invalid
 *
 * Since: 0.1.0
 **/
const gchar *
as_release_get_version (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->version;
}

/**
 * as_release_get_blob:
 * @release: a #AsRelease instance.
 * @filename: a filename
 *
 * Gets the release blob, which is typically firmware file data.
 *
 * Returns: a #GBytes, or %NULL for not set
 *
 * Since: 0.5.2
 **/
GBytes *
as_release_get_blob (AsRelease *release, const gchar *filename)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (filename != NULL, NULL);
	if (priv->blobs == NULL)
		return NULL;
	return g_hash_table_lookup (priv->blobs, filename);
}

/**
 * as_release_get_locations:
 * @release: a #AsRelease instance.
 *
 * Gets the release locations, typically URLs.
 *
 * Returns: (transfer none) (element-type utf8): list of locations
 *
 * Since: 0.3.5
 **/
GPtrArray *
as_release_get_locations (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	as_release_ensure_locations (release);
	return priv->locations;
}

/**
 * as_release_get_location_default:
 * @release: a #AsRelease instance.
 *
 * Gets the default release location, typically a URL.
 *
 * Returns: string, or %NULL for not set or invalid
 *
 * Since: 0.3.5
 **/
const gchar *
as_release_get_location_default (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->locations == NULL)
		return NULL;
	if (priv->locations->len == 0)
		return NULL;
	return g_ptr_array_index (priv->locations, 0);
}

/**
 * as_release_get_checksums:
 * @release: a #AsRelease instance.
 *
 * Gets the release checksums.
 *
 * Returns: (transfer none) (element-type AsChecksum): list of checksums
 *
 * Since: 0.4.2
 **/
GPtrArray *
as_release_get_checksums (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	as_release_ensure_checksums (release);
	return priv->checksums;
}

/**
 * as_release_get_checksum_by_fn:
 * @release: a #AsRelease instance
 * @fn: a file basename
 *
 * Gets the checksum for a release.
 *
 * Returns: (transfer none): an #AsChecksum, or %NULL for not found
 *
 * Since: 0.4.2
 **/
AsChecksum *
as_release_get_checksum_by_fn (AsRelease *release, const gchar *fn)
{
	AsChecksum *checksum;
	AsReleasePrivate *priv = GET_PRIVATE (release);
	guint i;

	for (i = 0; i < priv->checksums->len; i++) {
		checksum = g_ptr_array_index (priv->checksums, i);
		if (g_strcmp0 (fn, as_checksum_get_filename (checksum)) == 0)
			return checksum;
	}
	return NULL;
}

/**
 * as_release_get_checksum_by_target:
 * @release: a #AsRelease instance
 * @target: a #AsChecksumTarget, e.g. %AS_CHECKSUM_TARGET_CONTAINER
 *
 * Gets the checksum for a release.
 *
 * Returns: (transfer none): an #AsChecksum, or %NULL for not found
 *
 * Since: 0.4.2
 **/
AsChecksum *
as_release_get_checksum_by_target (AsRelease *release, AsChecksumTarget target)
{
	AsChecksum *checksum;
	AsReleasePrivate *priv = GET_PRIVATE (release);
	guint i;

	if (priv->checksums == NULL)
		return NULL;
	for (i = 0; i < priv->checksums->len; i++) {
		checksum = g_ptr_array_index (priv->checksums, i);
		if (as_checksum_get_target (checksum) == target)
			return checksum;
	}
	return NULL;
}

/**
 * as_release_get_timestamp:
 * @release: a #AsRelease instance.
 *
 * Gets the release timestamp.
 *
 * Returns: timestamp, or 0 for unset
 *
 * Since: 0.1.0
 **/
guint64
as_release_get_timestamp (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	return priv->timestamp;
}

/**
 * as_release_get_description:
 * @release: a #AsRelease instance.
 * @locale: (nullable): the locale. e.g. "en_GB"
 *
 * Gets the release description markup for a given locale.
 *
 * Returns: markup, or %NULL for not set or invalid
 *
 * Since: 0.1.0
 **/
const gchar *
as_release_get_description (AsRelease *release, const gchar *locale)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (priv->descriptions == NULL)
		return NULL;
	return as_hash_lookup_by_locale (priv->descriptions, locale);
}

/**
 * as_release_set_version:
 * @release: a #AsRelease instance.
 * @version: the version string.
 *
 * Sets the release version.
 *
 * Since: 0.1.0
 **/
void
as_release_set_version (AsRelease *release, const gchar *version)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	as_ref_string_assign_safe (&priv->version, version);
}

/**
 * as_release_set_blob:
 * @release: a #AsRelease instance.
 * @filename: a filename
 * @blob: the #GBytes data blob
 *
 * Sets a release blob, which is typically firmware data or a detached signature.
 *
 * NOTE: This is not stored in the XML file, and is only available in-memory.
 *
 * Since: 0.5.2
 **/
void
as_release_set_blob (AsRelease *release, const gchar *filename, GBytes *blob)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (filename != NULL);
	g_return_if_fail (blob != NULL);

	as_release_ensure_blobs (release);
	g_hash_table_insert (priv->blobs,
			     as_ref_string_new (filename),
			     g_bytes_ref (blob));
}

/**
 * as_release_set_urgency:
 * @release: a #AsRelease instance.
 * @urgency: the release urgency, e.g. %AS_URGENCY_KIND_CRITICAL
 *
 * Sets the release urgency.
 *
 * Since: 0.5.1
 **/
void
as_release_set_urgency (AsRelease *release, AsUrgencyKind urgency)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->urgency = urgency;
}

/**
 * as_release_set_kind:
 * @release: a #AsRelease instance.
 * @kind: the #AsReleaseKind
 *
 * Sets the release kind.
 *
 * Since: 0.7.6
 **/
void
as_release_set_kind (AsRelease *release, AsReleaseKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->kind = kind;
}

/**
 * as_release_set_state:
 * @release: a #AsRelease instance.
 * @state: the release state, e.g. %AS_RELEASE_STATE_INSTALLED
 *
 * Sets the release state.
 *
 * Since: 0.5.8
 **/
void
as_release_set_state (AsRelease *release, AsReleaseState state)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->state = state;
}

/**
 * as_release_add_location:
 * @release: a #AsRelease instance.
 * @location: the location string.
 *
 * Adds a release location.
 *
 * Since: 0.3.5
 **/
void
as_release_add_location (AsRelease *release, const gchar *location)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	/* deduplicate */
	as_release_ensure_locations (release);
	if (as_ptr_array_find_string (priv->locations, location))
		return;

	g_ptr_array_add (priv->locations, as_ref_string_new (location));
}

/**
 * as_release_add_checksum:
 * @release: a #AsRelease instance.
 * @checksum: a #AsChecksum instance.
 *
 * Adds a release checksum.
 *
 * Since: 0.4.2
 **/
void
as_release_add_checksum (AsRelease *release, AsChecksum *checksum)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	as_release_ensure_checksums (release);
	g_ptr_array_add (priv->checksums, g_object_ref (checksum));
}

/**
 * as_release_set_timestamp:
 * @release: a #AsRelease instance.
 * @timestamp: the timestamp value.
 *
 * Sets the release timestamp.
 *
 * Since: 0.1.0
 **/
void
as_release_set_timestamp (AsRelease *release, guint64 timestamp)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	priv->timestamp = timestamp;
}

/**
 * as_release_set_description:
 * @release: a #AsRelease instance.
 * @locale: (nullable): the locale. e.g. "en_GB"
 * @description: the description markup.
 *
 * Sets the description release markup.
 *
 * Since: 0.1.0
 **/
void
as_release_set_description (AsRelease *release,
			    const gchar *locale,
			    const gchar *description)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	if (locale == NULL)
		locale = "C";
	if (priv->descriptions == NULL) {
		priv->descriptions = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    (GDestroyNotify) as_ref_string_unref,
							    (GDestroyNotify) as_ref_string_unref);
	}
	g_hash_table_insert (priv->descriptions,
			     as_ref_string_new (locale),
			     as_ref_string_new (description));
}

/**
 * as_release_node_insert: (skip)
 * @release: a #AsRelease instance.
 * @parent: the parent #GNode to use..
 * @ctx: the #AsNodeContext
 *
 * Inserts the release into the DOM tree.
 *
 * Returns: (transfer none): A populated #GNode
 *
 * Since: 0.1.1
 **/
GNode *
as_release_node_insert (AsRelease *release, GNode *parent, AsNodeContext *ctx)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	AsChecksum *checksum;
	GNode *n;

	n = as_node_insert (parent, "release", NULL,
			    AS_NODE_INSERT_FLAG_NONE,
			    NULL);
	if (priv->timestamp > 0) {
		g_autofree gchar *timestamp_str = NULL;
		timestamp_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
						 priv->timestamp);
		as_node_add_attribute (n, "timestamp", timestamp_str);
	}
	if (priv->urgency != AS_URGENCY_KIND_UNKNOWN) {
		as_node_add_attribute (n, "urgency",
				       as_urgency_kind_to_string (priv->urgency));
	}
	if (priv->kind != AS_RELEASE_KIND_UNKNOWN) {
		as_node_add_attribute (n, "type",
				       as_release_kind_to_string (priv->kind));
	}
	if (as_node_context_get_output_trusted (ctx) &&
	    priv->state != AS_RELEASE_STATE_UNKNOWN) {
		as_node_add_attribute (n, "state",
				       as_release_state_to_string (priv->state));
	}
	if (priv->version != NULL)
		as_node_add_attribute (n, "version", priv->version);
	for (guint i = 0; priv->locations != NULL && i < priv->locations->len; i++) {
		const gchar *tmp = g_ptr_array_index (priv->locations, i);
		as_node_insert (n, "location", tmp,
				AS_NODE_INSERT_FLAG_NONE, NULL);
	}
	for (guint i = 0; priv->checksums != NULL && i < priv->checksums->len; i++) {
		checksum = g_ptr_array_index (priv->checksums, i);
		as_checksum_node_insert (checksum, n, ctx);
	}
	if (priv->descriptions != NULL) {
		as_node_insert_localized (n, "description", priv->descriptions,
					  AS_NODE_INSERT_FLAG_PRE_ESCAPED |
					  AS_NODE_INSERT_FLAG_DEDUPE_LANG);
	}

	/* add sizes */
	if (priv->sizes != NULL) {
		for (guint i = 0; i < AS_SIZE_KIND_LAST; i++) {
			g_autofree gchar *size_str = NULL;
			if (priv->sizes[i] == 0)
				continue;
			size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->sizes[i]);
			as_node_insert (n, "size", size_str,
					AS_NODE_INSERT_FLAG_NONE,
					"type", as_size_kind_to_string (i),
					NULL);
		}
	}
	return n;
}

/**
 * as_release_node_parse:
 * @release: a #AsRelease instance.
 * @node: a #GNode.
 * @ctx: a #AsNodeContext.
 * @error: A #GError or %NULL.
 *
 * Populates the object from a DOM node.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.1.0
 **/
gboolean
as_release_node_parse (AsRelease *release, GNode *node,
		       AsNodeContext *ctx, GError **error)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	GNode *n;
	const gchar *tmp;

	tmp = as_node_get_attribute (node, "timestamp");
	if (tmp != NULL)
		as_release_set_timestamp (release, g_ascii_strtoull (tmp, NULL, 10));
	tmp = as_node_get_attribute (node, "date");
	if (tmp != NULL) {
		g_autoptr(GDateTime) dt = NULL;
		dt = as_utils_iso8601_to_datetime (tmp);
		if (dt != NULL)
			as_release_set_timestamp (release, (guint64) g_date_time_to_unix (dt));
	}
	tmp = as_node_get_attribute (node, "urgency");
	if (tmp != NULL)
		as_release_set_urgency (release, as_urgency_kind_from_string (tmp));
	tmp = as_node_get_attribute (node, "type");
	if (tmp != NULL)
		as_release_set_kind (release, as_release_kind_from_string (tmp));
	tmp = as_node_get_attribute (node, "version");
	if (tmp != NULL)
		as_release_set_version (release, tmp);

	/* get optional locations */
	if (priv->locations != NULL)
		g_ptr_array_set_size (priv->locations, 0);
	for (n = node->children; n != NULL; n = n->next) {
		AsRefString *str;
		if (as_node_get_tag (n) != AS_TAG_LOCATION)
			continue;
		str = as_node_get_data_as_refstr (n);
		if (str == NULL)
			continue;
		as_release_ensure_locations (release);
		g_ptr_array_add (priv->locations, as_ref_string_ref (str));
	}

	/* get optional checksums */
	for (n = node->children; n != NULL; n = n->next) {
		g_autoptr(AsChecksum) csum = NULL;
		if (as_node_get_tag (n) != AS_TAG_CHECKSUM)
			continue;
		csum = as_checksum_new ();
		if (!as_checksum_node_parse (csum, n, ctx, error))
			return FALSE;
		as_release_add_checksum (release, csum);
	}

	/* get optional sizes */
	for (n = node->children; n != NULL; n = n->next) {
		AsSizeKind kind;
		if (as_node_get_tag (n) != AS_TAG_SIZE)
			continue;
		tmp = as_node_get_attribute (n, "type");
		if (tmp == NULL)
			continue;
		kind = as_size_kind_from_string (tmp);
		if (kind == AS_SIZE_KIND_UNKNOWN)
			continue;
		tmp = as_node_get_data (n);
		if (tmp == NULL)
			continue;
		as_release_ensure_sizes (release);
		priv->sizes[kind] = g_ascii_strtoull (tmp, NULL, 10);
	}

	/* AppStream: multiple <description> tags */
	if (as_node_context_get_format_kind (ctx) == AS_FORMAT_KIND_APPSTREAM) {
		for (n = node->children; n != NULL; n = n->next) {
			g_autoptr(GString) xml = NULL;
			if (as_node_get_tag (n) != AS_TAG_DESCRIPTION)
				continue;
			if (n->children == NULL)
				continue;
			xml = as_node_to_xml (n->children,
					      AS_NODE_TO_XML_FLAG_INCLUDE_SIBLINGS);
			if (xml == NULL)
				continue;
			as_release_set_description (release,
						    as_node_get_attribute (n, "xml:lang"),
						    xml->str);
		}

	/* AppData: mutliple languages encoded in one <description> tag */
	} else {
		n = as_node_find (node, "description");
		if (n != NULL) {
			if (priv->descriptions != NULL)
				g_hash_table_unref (priv->descriptions);
			priv->descriptions = as_node_get_localized_unwrap (n, error);
			if (priv->descriptions == NULL)
				return FALSE;
		}
	}

	return TRUE;
}

/**
 * as_release_node_parse_dep11:
 * @release: a #AsRelease instance.
 * @node: a #GNode.
 * @ctx: a #AsNodeContext.
 * @error: A #GError or %NULL.
 *
 * Populates the object from a DEP-11 node.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.5.13
 **/
gboolean
as_release_node_parse_dep11 (AsRelease *release, GNode *node,
			     AsNodeContext *ctx, GError **error)
{
	GNode *c;
	GNode *n;
	const gchar *tmp;
	const gchar *value;

	for (n = node->children; n != NULL; n = n->next) {
		tmp = as_yaml_node_get_key (n);
		if (g_strcmp0 (tmp, "unix-timestamp") == 0) {
			value = as_yaml_node_get_value (n);
			as_release_set_timestamp (release, g_ascii_strtoull (value, NULL, 10));
			continue;
		}
		if (g_strcmp0 (tmp, "version") == 0) {
			as_release_set_version (release, as_yaml_node_get_value (n));
			continue;
		}
		if (g_strcmp0 (tmp, "type") == 0) {
			as_release_set_kind (release, as_release_kind_from_string (as_yaml_node_get_value (n)));
			continue;
		}
		if (g_strcmp0 (tmp, "description") == 0) {
			for (c = n->children; c != NULL; c = c->next) {
				as_release_set_description (release,
							    as_yaml_node_get_key (c),
							    as_yaml_node_get_value (c));
			}
			continue;
		}
	}
	return TRUE;
}

/**
 * as_release_new:
 *
 * Creates a new #AsRelease.
 *
 * Returns: (transfer full): a #AsRelease
 *
 * Since: 0.1.0
 **/
AsRelease *
as_release_new (void)
{
	AsRelease *release;
	release = g_object_new (AS_TYPE_RELEASE, NULL);
	return AS_RELEASE (release);
}
