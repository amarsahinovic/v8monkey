Cu.import("resource://services-sync/ext/Preferences.js");

function run_test() {
  _("Set some prefs on the old branch");
  let globalPref = new Preferences("");
  globalPref.set("extensions.weave.hello", "world");
  globalPref.set("extensions.weave.number", 42);
  globalPref.set("extensions.weave.yes", true);
  globalPref.set("extensions.weave.no", false);

  _("Make sure the old prefs are there");
  do_check_eq(globalPref.get("extensions.weave.hello"), "world");
  do_check_eq(globalPref.get("extensions.weave.number"), 42);
  do_check_eq(globalPref.get("extensions.weave.yes"), true);
  do_check_eq(globalPref.get("extensions.weave.no"), false);

  _("New prefs shouldn't exist yet");
  do_check_eq(globalPref.get("services.sync.hello"), null);
  do_check_eq(globalPref.get("services.sync.number"), null);
  do_check_eq(globalPref.get("services.sync.yes"), null);
  do_check_eq(globalPref.get("services.sync.no"), null);

  _("Loading service should migrate");
  Cu.import("resource://services-sync/service.js");
  do_check_eq(globalPref.get("services.sync.hello"), "world");
  do_check_eq(globalPref.get("services.sync.number"), 42);
  do_check_eq(globalPref.get("services.sync.yes"), true);
  do_check_eq(globalPref.get("services.sync.no"), false);
  do_check_eq(globalPref.get("extensions.weave.hello"), null);
  do_check_eq(globalPref.get("extensions.weave.number"), null);
  do_check_eq(globalPref.get("extensions.weave.yes"), null);
  do_check_eq(globalPref.get("extensions.weave.no"), null);

  _("Migrating should set a pref to make sure to not re-migrate");
  do_check_true(globalPref.get("services.sync.migrated"));

  _("Make sure re-migrate doesn't happen");
  globalPref.set("extensions.weave.tooLate", "already migrated!");
  do_check_eq(globalPref.get("extensions.weave.tooLate"), "already migrated!");
  do_check_eq(globalPref.get("services.sync.tooLate"), null);
  Service._migratePrefs();
  do_check_eq(globalPref.get("extensions.weave.tooLate"), "already migrated!");
  do_check_eq(globalPref.get("services.sync.tooLate"), null);

  _("Clearing out pref changes for other tests");
  globalPref.resetBranch("extensions.weave.");
  globalPref.resetBranch("services.sync.");
}
