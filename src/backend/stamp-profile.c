/*
 * Copyright 2026 Jan-Michael Brummer
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

/*
 * A profile is a view: a named set of accounts, optionally claiming
 * parts of the week. It holds no state of its own beyond that -- the
 * manager decides which one is active and what that hides.
 */

#include "stamp-profile.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "stamp-helper.h"

struct _StampProfile {
  GObject parent_instance;

  gchar *id;
  gchar *name;
  gchar *color;
  gchar *initial;

  GStrv accounts;
  GArray *rules;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_COLOR,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_FINAL_TYPE (StampProfile, stamp_profile, G_TYPE_OBJECT);

/* Named after the GNOME palette, so the circles sit well next to the
 * rest of the shell no matter which accent the user runs. */
static const StampProfileColor palette[] = {
  { "blue",   N_("Blue"),   "#3584e4" },
  { "teal",   N_("Teal"),   "#2190a4" },
  { "green",  N_("Green"),  "#3a944a" },
  { "yellow", N_("Yellow"), "#c88800" },
  { "orange", N_("Orange"), "#ed5b00" },
  { "red",    N_("Red"),    "#e62d42" },
  { "pink",   N_("Pink"),   "#d56199" },
  { "purple", N_("Purple"), "#9141ac" },
  { "slate",  N_("Slate"),  "#6f8396" },
};

const StampProfileColor *
stamp_profile_get_palette (guint *n_colors)
{
  if (n_colors)
    *n_colors = G_N_ELEMENTS (palette);

  return palette;
}

const StampProfileColor *
stamp_profile_find_color (const gchar *color_id)
{
  for (guint i = 0; i < G_N_ELEMENTS (palette); i++) {
    if (g_strcmp0 (palette[i].id, color_id) == 0)
      return &palette[i];
  }

  return &palette[0];
}

/*
 * Auxiliary methods
 */

static void
update_initial (StampProfile *self)
{
  const gchar *name = self->name;

  g_clear_pointer (&self->initial, g_free);

  /* Skip anything that is not a letter or digit, so a profile called
   * "(work)" still shows a W rather than a bracket. */
  while (name && *name) {
    gunichar c = g_utf8_get_char (name);

    if (g_unichar_isalnum (c)) {
      gchar buffer[8] = { 0 };
      gint len = g_unichar_to_utf8 (g_unichar_toupper (c), buffer);

      self->initial = g_strndup (buffer, len);
      return;
    }

    name = g_utf8_next_char (name);
  }

  self->initial = g_strdup ("");
}

/*
 * GObject
 */

static void
stamp_profile_dispose (GObject *object)
{
  StampProfile *self = STAMP_PROFILE (object);

  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->color, g_free);
  g_clear_pointer (&self->initial, g_free);
  g_clear_pointer (&self->accounts, g_strfreev);
  g_clear_pointer (&self->rules, g_array_unref);

  G_OBJECT_CLASS (stamp_profile_parent_class)->dispose (object);
}

static void
stamp_profile_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  StampProfile *self = STAMP_PROFILE (object);

  switch (property_id) {
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;
    case PROP_COLOR:
      g_value_set_string (value, self->color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_profile_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  StampProfile *self = STAMP_PROFILE (object);

  switch (property_id) {
    case PROP_NAME:
      stamp_profile_set_name (self, g_value_get_string (value));
      break;
    case PROP_COLOR:
      stamp_profile_set_color (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_profile_class_init (StampProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_profile_dispose;
  object_class->get_property = stamp_profile_get_property;
  object_class->set_property = stamp_profile_set_property;

  properties[PROP_NAME] = g_param_spec_string ("name", NULL, NULL, NULL,
                                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_COLOR] = g_param_spec_string ("color", NULL, NULL, NULL,
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
stamp_profile_init (StampProfile *self)
{
  self->accounts = g_new0 (gchar *, 1);
  self->rules = g_array_new (FALSE, FALSE, sizeof (StampProfileRule));
  self->initial = g_strdup ("");
}

StampProfile *
stamp_profile_new (const gchar *id,
                   const gchar *name,
                   const gchar *color)
{
  StampProfile *self = g_object_new (STAMP_TYPE_PROFILE, NULL);

  self->id = id ? g_strdup (id) : g_uuid_string_random ();
  self->color = g_strdup (stamp_profile_find_color (color)->id);
  self->name = g_strdup (name ? name : "");
  update_initial (self);

  return self;
}

StampProfile *
stamp_profile_copy (StampProfile *self)
{
  StampProfile *copy;

  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  copy = stamp_profile_new (self->id, self->name, self->color);
  stamp_profile_set_accounts (copy, (const gchar * const *)self->accounts);
  g_array_append_vals (copy->rules, self->rules->data, self->rules->len);

  return copy;
}

/*
 * Getter/Setter
 */

const gchar *
stamp_profile_get_id (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  return self->id;
}

const gchar *
stamp_profile_get_name (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  return self->name;
}

void
stamp_profile_set_name (StampProfile *self,
                        const gchar  *name)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));

  if (g_strcmp0 (self->name, name) == 0)
    return;

  g_set_str (&self->name, name ? name : "");
  update_initial (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
stamp_profile_get_color (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  return self->color;
}

void
stamp_profile_set_color (StampProfile *self,
                         const gchar  *color)
{
  const gchar *resolved;

  g_return_if_fail (STAMP_IS_PROFILE (self));

  resolved = stamp_profile_find_color (color)->id;
  if (g_strcmp0 (self->color, resolved) == 0)
    return;

  g_set_str (&self->color, resolved);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

const gchar *
stamp_profile_get_initial (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  return self->initial;
}

/*
 * Accounts
 */

const gchar * const *
stamp_profile_get_accounts (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  return (const gchar * const *)self->accounts;
}

void
stamp_profile_set_accounts (StampProfile        *self,
                            const gchar * const *account_uids)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));

  g_clear_pointer (&self->accounts, g_strfreev);
  self->accounts = account_uids ? g_strdupv ((GStrv)account_uids) : g_new0 (gchar *, 1);
}

gboolean
stamp_profile_has_account (StampProfile *self,
                           const gchar  *account_uid)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), FALSE);

  if (!account_uid)
    return FALSE;

  return g_strv_contains ((const gchar * const *)self->accounts, account_uid);
}

void
stamp_profile_add_account (StampProfile *self,
                           const gchar  *account_uid)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));
  g_return_if_fail (account_uid != NULL);

  if (stamp_profile_has_account (self, account_uid))
    return;

  {
    GStrv grown = g_strv_append ((const gchar * const *)self->accounts, account_uid);

    g_clear_pointer (&self->accounts, g_strfreev);
    self->accounts = grown;
  }
}

void
stamp_profile_remove_account (StampProfile *self,
                              const gchar  *account_uid)
{
  GStrv shrunk;

  g_return_if_fail (STAMP_IS_PROFILE (self));
  g_return_if_fail (account_uid != NULL);

  if (!stamp_profile_has_account (self, account_uid))
    return;

  shrunk = g_strv_remove ((const gchar * const *)self->accounts, account_uid);

  g_clear_pointer (&self->accounts, g_strfreev);
  self->accounts = shrunk;
}

/*
 * Schedule
 */

guint
stamp_profile_get_n_rules (StampProfile *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), 0);

  return self->rules->len;
}

gboolean
stamp_profile_get_rule (StampProfile     *self,
                        guint             index,
                        StampProfileRule *rule)
{
  g_return_val_if_fail (STAMP_IS_PROFILE (self), FALSE);
  g_return_val_if_fail (rule != NULL, FALSE);

  if (index >= self->rules->len)
    return FALSE;

  *rule = g_array_index (self->rules, StampProfileRule, index);

  return TRUE;
}

void
stamp_profile_add_rule (StampProfile           *self,
                        const StampProfileRule *rule)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));
  g_return_if_fail (rule != NULL);

  g_array_append_val (self->rules, *rule);
}

void
stamp_profile_set_rule (StampProfile           *self,
                        guint                   index,
                        const StampProfileRule *rule)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));
  g_return_if_fail (rule != NULL);

  if (index >= self->rules->len)
    return;

  g_array_index (self->rules, StampProfileRule, index) = *rule;
}

void
stamp_profile_remove_rule (StampProfile *self,
                           guint         index)
{
  g_return_if_fail (STAMP_IS_PROFILE (self));

  if (index >= self->rules->len)
    return;

  g_array_remove_index (self->rules, index);
}

/* g_date_time_get_day_of_week() counts Monday as 1; our bits count it
 * as 0. */
static inline guint8
weekday_bit (gint day_of_week)
{
  return 1 << ((day_of_week - 1) % 7);
}

static gboolean
rule_matches (const StampProfileRule *rule,
              gint                    day_of_week,
              guint                   minute)
{
  guint8 today = weekday_bit (day_of_week);
  guint8 yesterday = weekday_bit (day_of_week == 1 ? 7 : day_of_week - 1);

  if (rule->days == 0)
    return FALSE;

  if (rule->end_minute > rule->start_minute) {
    return (rule->days & today) && minute >= rule->start_minute && minute < rule->end_minute;
  }

  /* Wraps past midnight: the tail belongs to the day the rule started
   * on, which is yesterday as far as the clock is concerned. */
  if ((rule->days & today) && minute >= rule->start_minute)
    return TRUE;

  return (rule->days & yesterday) && minute < rule->end_minute;
}

gboolean
stamp_profile_matches_time (StampProfile *self,
                            GDateTime    *when)
{
  gint day_of_week;
  guint minute;

  g_return_val_if_fail (STAMP_IS_PROFILE (self), FALSE);
  g_return_val_if_fail (when != NULL, FALSE);

  day_of_week = g_date_time_get_day_of_week (when);
  minute = g_date_time_get_hour (when) * 60 + g_date_time_get_minute (when);

  for (guint i = 0; i < self->rules->len; i++) {
    const StampProfileRule *rule = &g_array_index (self->rules, StampProfileRule, i);

    if (rule_matches (rule, day_of_week, minute))
      return TRUE;
  }

  return FALSE;
}

static gboolean
use_24h_clock (void)
{
  g_autoptr (GSettings) settings = g_settings_new ("org.gnome.desktop.interface");
  g_autofree gchar *format = g_settings_get_string (settings, "clock-format");

  return g_strcmp0 (format, "12h") != 0;
}

static gchar *
format_minute (guint minute,
               gboolean twenty_four_hour)
{
  guint hour = (minute / 60) % 24;
  guint minutes = minute % 60;

  if (twenty_four_hour)
    return g_strdup_printf ("%02u:%02u", hour, minutes);

  return g_strdup_printf ("%u:%02u %s",
                          hour % 12 == 0 ? 12 : hour % 12,
                          minutes,
                          hour < 12 ? _("AM") : _("PM"));
}

const gchar *
stamp_profile_get_weekday_label (gint day)
{
  /* Abbreviated weekday names, Monday first. */
  static const gchar *names[] = {
    NC_("weekday", "Mon"),
    NC_("weekday", "Tue"),
    NC_("weekday", "Wed"),
    NC_("weekday", "Thu"),
    NC_("weekday", "Fri"),
    NC_("weekday", "Sat"),
    NC_("weekday", "Sun"),
  };

  g_return_val_if_fail (day >= 0 && day < (gint)G_N_ELEMENTS (names), "");

  return g_dpgettext2 (NULL, "weekday", names[day]);
}

static void
append_day_range (GString *out,
                  gint     first,
                  gint     last)
{
  if (out->len > 0)
    g_string_append (out, ", ");

  if (first == last) {
    g_string_append (out, stamp_profile_get_weekday_label (first));
  } else {
    /* Translators: a range of weekdays, as in "Mon-Fri". */
    g_string_append_printf (out, _("%s\xe2\x80\x93%s"),
                            stamp_profile_get_weekday_label (first),
                            stamp_profile_get_weekday_label (last));
  }
}

static gchar *
dup_days_label (guint8 days)
{
  g_autoptr (GString) out = g_string_new (NULL);
  gint run_start = -1;

  if ((days & STAMP_PROFILE_ALL_DAYS) == STAMP_PROFILE_ALL_DAYS)
    return g_strdup (_("Every day"));

  if ((days & STAMP_PROFILE_ALL_DAYS) == STAMP_PROFILE_WEEKDAYS)
    return g_strdup (_("Weekdays"));

  for (gint day = 0; day < 7; day++) {
    gboolean set = (days & (1 << day)) != 0;

    if (set && run_start < 0) {
      run_start = day;
    } else if (!set && run_start >= 0) {
      append_day_range (out, run_start, day - 1);
      run_start = -1;
    }
  }

  if (run_start >= 0)
    append_day_range (out, run_start, 6);

  return g_strdup (out->str);
}

gchar *
stamp_profile_dup_schedule_summary (StampProfile *self)
{
  g_autoptr (GString) out = NULL;
  gboolean twenty_four_hour;

  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  if (self->rules->len == 0)
    return NULL;

  out = g_string_new (NULL);
  twenty_four_hour = use_24h_clock ();

  for (guint i = 0; i < self->rules->len; i++) {
    const StampProfileRule *rule = &g_array_index (self->rules, StampProfileRule, i);
    g_autofree gchar *days = dup_days_label (rule->days);
    g_autofree gchar *start = format_minute (rule->start_minute, twenty_four_hour);
    g_autofree gchar *end = format_minute (rule->end_minute, twenty_four_hour);

    if (out->len > 0)
      g_string_append (out, " \xc2\xb7 ");

    /* Translators: one schedule rule, as in "Mon-Fri 09:00-18:00". */
    g_string_append_printf (out, _("%s %s\xe2\x80\x93%s"), days, start, end);
  }

  return g_strdup (out->str);
}

/*
 * Serialization
 */

GVariant *
stamp_profile_to_variant (StampProfile *self)
{
  GVariantBuilder builder;

  g_return_val_if_fail (STAMP_IS_PROFILE (self), NULL);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(sssasa(yqq))"));
  g_variant_builder_add (&builder, "s", self->id);
  g_variant_builder_add (&builder, "s", self->name);
  g_variant_builder_add (&builder, "s", self->color);
  g_variant_builder_add_value (&builder, g_variant_new_strv ((const gchar * const *)self->accounts, -1));

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(yqq)"));
  for (guint i = 0; i < self->rules->len; i++) {
    const StampProfileRule *rule = &g_array_index (self->rules, StampProfileRule, i);

    g_variant_builder_add (&builder, "(yqq)", rule->days, rule->start_minute, rule->end_minute);
  }
  g_variant_builder_close (&builder);

  return g_variant_builder_end (&builder);
}

StampProfile *
stamp_profile_new_from_variant (GVariant *variant)
{
  StampProfile *self;
  g_autoptr (GVariantIter) rules = NULL;
  g_auto (GStrv) accounts = NULL;
  g_autofree gchar *id = NULL;
  g_autofree gchar *name = NULL;
  g_autofree gchar *color = NULL;
  guchar days;
  guint16 start;
  guint16 end;

  g_return_val_if_fail (variant != NULL, NULL);

  if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("(sssasa(yqq))"))) {
    g_warning ("%s: Ignoring a profile of unexpected type %s", G_STRFUNC, g_variant_get_type_string (variant));
    return NULL;
  }

  g_variant_get (variant, "(sss^asa(yqq))", &id, &name, &color, &accounts, &rules);

  /* A profile without an identifier could never be selected again. */
  if (!id || *id == '\0')
    return NULL;

  self = stamp_profile_new (id, name, color);
  stamp_profile_set_accounts (self, (const gchar * const *)accounts);

  while (g_variant_iter_next (rules, "(yqq)", &days, &start, &end)) {
    StampProfileRule rule = {
      .days = days,
      .start_minute = MIN (start, STAMP_PROFILE_MINUTES_PER_DAY),
      .end_minute = MIN (end, STAMP_PROFILE_MINUTES_PER_DAY),
    };

    stamp_profile_add_rule (self, &rule);
  }

  return self;
}
