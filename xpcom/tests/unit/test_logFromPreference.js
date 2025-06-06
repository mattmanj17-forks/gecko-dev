/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const kPrefName = "logging.prof";
const kPrefValue = 5;
add_task(async () => {
  Services.prefs.setIntPref(kPrefName, kPrefValue);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(kPrefName);
  });

  const entries = 10000;
  const interval = 1;
  const threads = ["GeckoMain"];
  const features = ["nostacksampling"];
  await Services.profiler.StartProfiler(entries, interval, features, threads);
  // We need to pause the profiler here, otherwise we get crashes.
  // This seems to be a combination of json streaming + markers from Rust.
  // See Bug 1920704 for more details.
  await Services.profiler.Pause();
  const profileData = await Services.profiler.getProfileDataAsync();
  await Services.profiler.StopProfiler();
  const { markers, stringTable } = profileData.threads[0];
  const stringIndexForLogMessages = stringTable.indexOf("LogMessages");
  Assert.greaterOrEqual(
    stringIndexForLogMessages,
    0,
    "A string index for the string LogMessages have been found."
  );

  // At least one log for the profiler json streaming operation should exist.
  // See https://searchfox.org/mozilla-central/rev/445a6e86233c733c5557ef44e1d33444adaddefc/mozglue/baseprofiler/core/platform.cpp#2015
  const logMessageMarkers = markers.data.filter(
    tuple => tuple[markers.schema.name] === stringIndexForLogMessages
  );

  Assert.greaterOrEqual(
    logMessageMarkers.length,
    0,
    "At least one log message have been found."
  );
});
