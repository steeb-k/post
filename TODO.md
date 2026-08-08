# Contacts View
- Sidebar as complete widget
- 

# Profiles

Shipped: the switcher, the management dialog, mail/contacts/calendar
filtering, muted notifications for hidden accounts, and the schedule.
What follows is what was deliberately left out.

- **Custom images on the badge.** The switcher was built on `AdwAvatar`
  specifically so this is a small change — `adw_avatar_set_custom_image()`
  in `stamp_profile_button_style_avatar()` — but there is nowhere to
  choose a file, and nowhere to put it. Decide first whether the image is
  copied into the profile's config or merely referenced by path, because
  a referenced path breaks the moment the file moves.
- **Reordering profiles.** The list is in creation order and the order
  matters: `scheduled_profile_at()` takes the *first* profile whose rules
  match, so two overlapping schedules are resolved by a sequence the user
  cannot see or change. The mail sidebar already drag-reorders accounts
  (`save_account_order()` in `stamp-folder-list.c`) if a pattern is
  wanted.
- **A keyboard shortcut for switching.** Mail/Contacts/Calendar have
  Ctrl+1..3; profiles have nothing. `app.profiles` opens the dialog but
  no action activates a profile, so nothing can be bound yet.
- **Per-profile defaults.** A profile knows which accounts it shows and
  which layout it wants, and nothing else. Obvious extensions: default
  sending identity, default calendar for new events, a per-profile
  notification mode.
- **Any profile awareness in search.** Global search still reaches every
  account. Arguably correct — you often search precisely for the thing
  you cannot see — but it is not a decision that was made, it is one that
  was never reached.
- **Calendar event reminders.** They do not exist at all; see
  KNOWN-ISSUES.md. When they land they must be profile-scoped.
