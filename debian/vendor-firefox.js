// Disable default browser checking.
pref("browser.shell.checkDefaultBrowser", false);

// Don't disable extensions dropped in to a system
// location, or those owned by the application
pref("extensions.autoDisableScopes", 3);

// Don't display the one-off addon selection dialog when
// upgrading from a version of Firefox older than 8.0
pref("extensions.shownSelectionUI", true);

// Disable WebM to force h.264
pref("media.webm.enabled", false);

// Enable accelerated layers (needs more GPU memory)
pref("layers.acceleration.force-enabled", true);
pref("layers.composer2d.enabled", true);
