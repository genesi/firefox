// Disable default browser checking.
pref("browser.shell.checkDefaultBrowser", false);

// Don't disable extensions dropped in to a system
// location, or those owned by the application
pref("extensions.autoDisableScopes", 3);

// Don't display the one-off addon selection dialog when
// upgrading from a version of Firefox older than 8.0
pref("extensions.shownSelectionUI", true);


// Genesi

// Disable WebM to force h.264
pref("media.webm.enabled", false);

// Enable accelerated layers (needs more GPU memory)
pref("layers.acceleration.force-enabled", true);
pref("layers.composer2d.enabled", true);
pref("layers.offmainthreadcomposition.animate-opacity", true);
pref("layers.offmainthreadcomposition.animate-transform", true);
pref("layers.async-video.enabled", true);

// Disable xrender
pref("gfx.xrender.enabled", false);

// Networking
pref("network.http.max-connections", 6);
pref("network.http.pipelining", true);
pref("network.http.pipelining.max-optimistic-requests", 1);
pref("network.http.pipelining.maxrequests", 6);
pref("network.http.pipelining.ssl", true);
pref("network.http.proxy.pipelining", true);

// Hide missing plugin infobar
pref("plugins.hide_infobar_for_missing_plugin", true);
pref("plugins.hide_infobar_for_outdated_plugin", true);

// Reduce memory consumption
pref("browser.sessionhistory.max_total_viewers", 0);
