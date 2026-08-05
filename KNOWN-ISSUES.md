# Known Issues

Issues found while testing this fork. Not yet fixed.

## Folders show up empty until the app is restarted

After adding an account the folder list appears, including custom folders,
but every folder shows no mail. Closing and reopening the app shows the
mail as expected.

The debug log shows the account init finishing normally
(`on_account_ready: 'Protonmail' services ready`) with nothing that points
at the cause, so this needs a closer look. Likely candidates are the
conversation list being built before the folder summary has been fetched
for the first time, or the initial refresh not emitting a change the list
model listens to.

## Preferences freeze when enabling background notifications and autostart

Turning on Background Notifications and then Autostart in Preferences
freezes the app and it has to be killed. The settings do get stored.

Console output:

    GLib-GObject-CRITICAL: invalid cast from 'AdwSwitchRow' to 'XdpPortal'
    libportal-CRITICAL: xdp_portal_request_background: assertion 'XDP_IS_PORTAL (portal)' failed
    GLib-CRITICAL: g_variant_new_string: assertion 'string != NULL' failed
    GLib-CRITICAL: g_variant_ref_sink: assertion 'value != NULL' failed

The invalid cast says a switch row is being passed where the portal object
is expected, so the callback is reading the wrong argument. See
`on_autostart` and `on_background_notifications` in `src/stamp-preferences.c`.
