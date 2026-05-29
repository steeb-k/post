/*
 * Copyright 2024-2026 Jan-Michael Brummer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stamp-category.h"

struct _StampCategory {
  GObject parent_instance;

  gchar *id;
  gchar *name;
  gchar *color;
  gchar *color_hex;
};

typedef enum {
  PROP_ID = 1,
  PROP_NAME,
  PROP_COLOR,
  PROP_COLOR_HEX,
} StampCategoryProps;

static GParamSpec *props[PROP_COLOR_HEX + 1];

G_DEFINE_FINAL_TYPE (StampCategory, stamp_category, G_TYPE_OBJECT);

static void
stamp_category_finalize (GObject *object)
{
  StampCategory *self = STAMP_CATEGORY (object);

  g_free (self->id);
  g_free (self->name);
  g_free (self->color);
  g_free (self->color_hex);

  G_OBJECT_CLASS (stamp_category_parent_class)->finalize (object);
}

static void
stamp_category_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  StampCategory *self = STAMP_CATEGORY (object);

  switch ((StampCategoryProps)prop_id) {
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;
    case PROP_COLOR:
      g_value_set_string (value, self->color);
      break;
    case PROP_COLOR_HEX:
      g_value_set_string (value, self->color_hex);
      break;
  }
}

static void
stamp_category_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  StampCategory *self = STAMP_CATEGORY (object);

  switch ((StampCategoryProps)property_id) {
    case PROP_ID:
      g_set_str (&self->id, g_value_get_string (value));
      break;
    case PROP_NAME:
      g_set_str (&self->name, g_value_get_string (value));
      break;
    case PROP_COLOR:
      g_set_str (&self->color, g_value_get_string (value));
      break;
    case PROP_COLOR_HEX:
      g_set_str (&self->color_hex, g_value_get_string (value));
      break;
  }
}

static void
stamp_category_dispose (GObject *object)
{
  StampCategory *self = STAMP_CATEGORY (object);

  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->color, g_free);
  g_clear_pointer (&self->color_hex, g_free);

  G_OBJECT_CLASS (stamp_category_parent_class)->dispose (object);
}

static void
stamp_category_class_init (StampCategoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = stamp_category_finalize;
  object_class->get_property = stamp_category_get_property;
  object_class->set_property = stamp_category_set_property;
  object_class->dispose = stamp_category_dispose;

  props[PROP_ID] = g_param_spec_string ("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  props[PROP_NAME] = g_param_spec_string ("name", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  props[PROP_COLOR] = g_param_spec_string ("color", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  props[PROP_COLOR_HEX] = g_param_spec_string ("color-hex", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
stamp_category_init (StampCategory *self)
{
}

StampCategory *
stamp_category_new (const gchar *id,
                    const gchar *name,
                    const gchar *color,
                    const gchar *color_hex)
{
  return g_object_new (STAMP_TYPE_CATEGORY, "id", id, "name", name, "color", color, "color-hex", color_hex, NULL);
}

const gchar *
stamp_category_get_id (StampCategory *self)
{
  g_return_val_if_fail (STAMP_IS_CATEGORY (self), NULL);
  return self->id;
}

const gchar *
stamp_category_get_name (StampCategory *self)
{
  g_return_val_if_fail (STAMP_IS_CATEGORY (self), NULL);
  return self->name;
}

const gchar *
stamp_category_get_color (StampCategory *self)
{
  g_return_val_if_fail (STAMP_IS_CATEGORY (self), NULL);
  return self->color;
}

const gchar *
stamp_category_get_hex (StampCategory *self)
{
  g_return_val_if_fail (STAMP_IS_CATEGORY (self), NULL);
  return self->color_hex;
}

/* Microsoft 365 Helper */

static const struct {
  const char *preset;
  const char *hex;
  const char *name;
} COLOR_MAP[] = {
  { "preset0", "#e74856", "Red"          },
  { "preset1", "#ff8c00", "Orange"       },
  { "preset2", "#8e562e", "Brown"        },
  { "preset3", "#ffd335", "Yellow"       },
  { "preset4", "#0f7b0f", "Green"        },
  { "preset5", "#038387", "Teal"         },
  { "preset6", "#636e00", "Olive"        },
  { "preset7", "#0078d4", "Blue"         },
  { "preset8", "#881798", "Purple"       },
  { "preset9", "#b4009e", "Cranberry"    },
  { "preset10", "#767676", "Steel"        },
  { "preset11", "#4c4a48", "DarkSteel"    },
  { "preset12", "#9d9d9d", "Gray"         },
  { "preset13", "#6d6d6d", "DarkGray"     },
  { "preset14", "#000000", "Black"        },
  { "preset15", "#750b1c", "DarkRed"      },
  { "preset16", "#d83b01", "DarkOrange"   },
  { "preset17", "#4e2d1e", "DarkBrown"    },
  { "preset18", "#c19c00", "DarkYellow"   },
  { "preset19", "#0b6a0b", "DarkGreen"    },
  { "preset20", "#005b70", "DarkTeal"     },
  { "preset21", "#3d4000", "DarkOlive"    },
  { "preset22", "#003966", "DarkBlue"     },
  { "preset23", "#4b0082", "DarkPurple"   },
  { "preset24", "#6b0043", "DarkCranberry"},
  { NULL, NULL }
};

static const gchar *
stamp_preset_to_hex (const gchar *preset)
{
  if (!preset)
    return "#808080";

  for (gint i = 0; COLOR_MAP[i].preset != NULL; i++) {
    if (g_strcmp0 (COLOR_MAP[i].preset, preset) == 0)
      return COLOR_MAP[i].hex;
  }

  return "#808080";
}

static GList *
parse_categories_json (const gchar  *json_data,
                       gsize         json_len,
                       GError      **error)
{
  g_autoptr (JsonParser) parser = NULL;
  JsonNode *root_node;
  JsonObject *root_obj;
  JsonArray *value_arr;
  GList *result = NULL;
  GError *local_error = NULL;

  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, json_data, (gssize)json_len, &local_error)) {
    g_propagate_prefixed_error (error, local_error, "Graph API JSON invalid: ");
    return NULL;
  }

  root_node = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root_node)) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Graph API: unexpected JSON format");
    return NULL;
  }

  root_obj = json_node_get_object (root_node);
  if (json_object_has_member (root_obj, "error")) {
    JsonObject *err_obj = json_object_get_object_member (root_obj, "error");
    const gchar *msg = json_object_get_string_member (err_obj, "message");

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Graph API Error: %s", msg ? msg : "unknown");
    return NULL;
  }

  if (!json_object_has_member (root_obj, "value")) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Graph API: 'value' array missing");
    return NULL;
  }

  value_arr = json_object_get_array_member (root_obj, "value");

  for (guint i = 0; i < json_array_get_length (value_arr); i++) {
    JsonObject *obj = json_array_get_object_element (value_arr, i);
    StampCategory *cat = NULL;
    const gchar *preset;

    preset = json_object_get_string_member_with_default (obj, "color", NULL);
    cat = stamp_category_new (json_object_get_string_member_with_default (obj, "id", NULL),
                              json_object_get_string_member_with_default (obj, "displayName", NULL),
                              preset,
                              stamp_preset_to_hex (preset));
    result = g_list_prepend (result, cat);
  }

  return g_list_reverse (result);
}

GList *
stamp_m365_get_categories_sync (ESource       *source,
                                GCancellable  *cancellable,
                                GError       **error)
{
  g_autoptr (SoupSession) session = NULL;
  g_autoptr (SoupMessage) msg = NULL;
  g_autoptr (GBytes) body = NULL;
  g_autofree char *token = NULL;
  g_autofree char *auth_hdr = NULL;
  GError *local_error = NULL;
  gconstpointer data;
  gsize data_len;

  g_return_val_if_fail (E_IS_SOURCE (source), NULL);

  if (!e_source_get_oauth2_access_token_sync (source, cancellable, &token, NULL, &local_error)) {
    g_propagate_prefixed_error (error, local_error, "OAuth2-Token not accessible: ");
    return NULL;
  }

  session = soup_session_new ();
  msg = soup_message_new ("GET", "https://graph.microsoft.com/v1.0/me/outlook/masterCategories");

  auth_hdr = g_strdup_printf ("Bearer %s", token);
  soup_message_headers_replace (soup_message_get_request_headers (msg), "Authorization", auth_hdr);
  soup_message_headers_replace (soup_message_get_request_headers (msg), "Accept", "application/json");

  body = soup_session_send_and_read (session, msg, cancellable, &local_error);

  if (local_error) {
    g_propagate_prefixed_error (error, local_error, "Graph API HTTP error: ");
    return NULL;
  }

  if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (msg))) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Graph API: HTTP %u – %s",
                 soup_message_get_status (msg),
                 soup_message_get_reason_phrase (msg));
    return NULL;
  }

  data = g_bytes_get_data (body, &data_len);
  return parse_categories_json ((const char *)data, data_len, error);
}

typedef struct {
  ESource *source;
  GCancellable *cancellable;
} CategoriesTaskData;

static void
categories_task_data_free (CategoriesTaskData *data)
{
  g_clear_object (&data->source);
  g_clear_object (&data->cancellable);
  g_free (data);
}

static void
stamp_categories_free (GList *list)
{
  g_list_free_full (list, g_object_unref);
}

static void
categories_thread_func (GTask        *task,
                        gpointer      source_object,
                        gpointer      task_data,
                        GCancellable *cancellable)
{
  CategoriesTaskData *data = task_data;
  GError *error = NULL;
  GList *result;

  result = stamp_m365_get_categories_sync (data->source, cancellable, &error);
  if (error)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, result, (GDestroyNotify)stamp_categories_free);
}

void
stamp_m365_get_categories_async (ESource             *source,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  CategoriesTaskData *data;

  g_return_if_fail (E_IS_SOURCE (source));

  data = g_new0 (CategoriesTaskData, 1);
  data->source = g_object_ref (source);
  data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

  task = g_task_new (source, cancellable, callback, user_data);
  g_task_set_source_tag (task, stamp_m365_get_categories_async);
  g_task_set_task_data (task, data, (GDestroyNotify)categories_task_data_free);
  g_task_run_in_thread (task, categories_thread_func);
}

GList *
stamp_m365_get_categories_finish (ESource       *source,
                                  GAsyncResult  *result,
                                  GError       **error)
{
  g_return_val_if_fail (E_IS_SOURCE (source), NULL);
  g_return_val_if_fail (g_task_is_valid (result, source), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
