"use strict";

const { BaseAction } = ChromeUtils.importESModule(
  "resource://normandy/actions/BaseAction.sys.mjs"
);
const { ClientEnvironment } = ChromeUtils.importESModule(
  "resource://normandy/lib/ClientEnvironment.sys.mjs"
);
const { Heartbeat } = ChromeUtils.importESModule(
  "resource://normandy/lib/Heartbeat.sys.mjs"
);

const { Uptake } = ChromeUtils.importESModule(
  "resource://normandy/lib/Uptake.sys.mjs"
);
const { NormandyTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/NormandyTestUtils.sys.mjs"
);

const HOUR_IN_MS = 60 * 60 * 1000;

function heartbeatRecipeFactory(overrides = {}) {
  const defaults = {
    revision_id: 1,
    name: "Test Recipe",
    action: "show-heartbeat",
    arguments: {
      surveyId: "a survey",
      message: "test message",
      engagementButtonLabel: "",
      thanksMessage: "thanks!",
      postAnswerUrl: "http://example.com",
      learnMoreMessage: "Learn More",
      learnMoreUrl: "http://example.com",
      repeatOption: "once",
    },
  };

  if (overrides.arguments) {
    defaults.arguments = Object.assign(defaults.arguments, overrides.arguments);
    delete overrides.arguments;
  }

  return recipeFactory(Object.assign(defaults, overrides));
}

// Test that a normal heartbeat works as expected
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testHappyPath({ heartbeatClassStub, heartbeatInstanceStub }) {
    const recipe = heartbeatRecipeFactory();
    const action = new ShowHeartbeatAction();
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    await action.finalize();
    is(
      action.state,
      ShowHeartbeatAction.STATE_FINALIZED,
      "Action should be finalized"
    );
    is(action.lastError, null, "No errors should have been thrown");

    const options = heartbeatClassStub.args[0][1];
    Assert.deepEqual(
      heartbeatClassStub.args,
      [
        [
          heartbeatClassStub.args[0][0], // target window
          {
            surveyId: options.surveyId,
            message: recipe.arguments.message,
            engagementButtonLabel: recipe.arguments.engagementButtonLabel,
            thanksMessage: recipe.arguments.thanksMessage,
            learnMoreMessage: recipe.arguments.learnMoreMessage,
            learnMoreUrl: recipe.arguments.learnMoreUrl,
            postAnswerUrl: options.postAnswerUrl,
            flowId: options.flowId,
            surveyVersion: recipe.revision_id,
          },
        ],
      ],
      "expected arguments were passed"
    );

    ok(NormandyTestUtils.isUuid(options.flowId, "flowId should be a uuid"));

    // postAnswerUrl gains several query string parameters. Check that the prefix is right
    ok(options.postAnswerUrl.startsWith(recipe.arguments.postAnswerUrl));

    ok(
      heartbeatInstanceStub.eventEmitter.once.calledWith("Voted"),
      "Voted event handler should be registered"
    );
    ok(
      heartbeatInstanceStub.eventEmitter.once.calledWith("Engaged"),
      "Engaged event handler should be registered"
    );
  }
);

/* Test that heartbeat doesn't show if an unrelated heartbeat has shown recently. */
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testRepeatGeneral({ heartbeatClassStub }) {
    await ShowHeartbeatAction._setLastShown("recipe0", Date.now());
    const recipe = heartbeatRecipeFactory();

    const action = new ShowHeartbeatAction();
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    is(action.lastError, null, "No errors should have been thrown");

    is(
      heartbeatClassStub.args.length,
      0,
      "Heartbeat should not be called once"
    );
  }
);

/* Test that a heartbeat shows if an unrelated heartbeat showed more than 24 hours ago. */
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testRepeatUnrelated({ heartbeatClassStub }) {
    await ShowHeartbeatAction._setLastShown(
      "recipe0",
      Date.now() - 25 * HOUR_IN_MS
    );
    const recipe = heartbeatRecipeFactory();

    const action = new ShowHeartbeatAction();
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    is(action.lastError, null, "No errors should have been thrown");

    is(heartbeatClassStub.args.length, 1, "Heartbeat should be called once");
  }
);

/* Test that a repeat=once recipe is not shown again, even more than 24 hours ago. */
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testRepeatTypeOnce({ heartbeatClassStub }) {
    const recipe = heartbeatRecipeFactory({
      arguments: { repeatOption: "once" },
    });
    await ShowHeartbeatAction._setLastShown(
      recipe.id,
      Date.now() - 25 * HOUR_IN_MS
    );

    const action = new ShowHeartbeatAction();
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    is(action.lastError, null, "No errors should have been thrown");

    is(heartbeatClassStub.args.length, 0, "Heartbeat should not be called");
  }
);

/* Test that a repeat=xdays recipe is shown again, only after the expected number of days. */
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testRepeatTypeXdays({ heartbeatClassStub }) {
    const recipe = heartbeatRecipeFactory({
      arguments: {
        repeatOption: "xdays",
        repeatEvery: 2,
      },
    });

    await ShowHeartbeatAction._setLastShown(
      recipe.id,
      Date.now() - 25 * HOUR_IN_MS
    );
    const action = new ShowHeartbeatAction();
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    is(action.lastError, null, "No errors should have been thrown");
    is(heartbeatClassStub.args.length, 0, "Heartbeat should not be called");

    await ShowHeartbeatAction._setLastShown(
      recipe.id,
      Date.now() - 50 * HOUR_IN_MS
    );
    await action.processRecipe(recipe, BaseAction.suitability.FILTER_MATCH);
    is(action.lastError, null, "No errors should have been thrown");
    is(
      heartbeatClassStub.args.length,
      1,
      "Heartbeat should have been called once"
    );
  }
);

/* generatePostAnswerURL shouldn't annotate empty strings */
add_task(async function postAnswerEmptyString() {
  const recipe = heartbeatRecipeFactory({ arguments: { postAnswerUrl: "" } });
  const action = new ShowHeartbeatAction();
  is(
    await action.generatePostAnswerURL(recipe),
    "",
    "an empty string should not be annotated"
  );
});

/* generatePostAnswerURL should include the right details */
add_task(async function postAnswerUrl() {
  const recipe = heartbeatRecipeFactory({
    arguments: {
      postAnswerUrl: "https://example.com/survey?survey_id=42",
      includeTelemetryUUID: false,
      message: "Hello, World!",
    },
  });
  const action = new ShowHeartbeatAction();
  const url = new URL(await action.generatePostAnswerURL(recipe));

  is(
    url.searchParams.get("survey_id"),
    "42",
    "Pre-existing search parameters should be preserved"
  );
  is(
    url.searchParams.get("fxVersion"),
    Services.appinfo.version,
    "Firefox version should be included"
  );
  is(
    url.searchParams.get("surveyversion"),
    Services.appinfo.version,
    "Survey version should also be the Firefox version"
  );
  ok(
    ["0", "1"].includes(url.searchParams.get("syncSetup")),
    `syncSetup should be 0 or 1, got ${url.searchParams.get("syncSetup")}`
  );
  is(
    url.searchParams.get("updateChannel"),
    UpdateUtils.getUpdateChannel("false"),
    "Update channel should be included"
  );
  ok(!url.searchParams.has("userId"), "no user id should be included");
  is(
    url.searchParams.get("utm_campaign"),
    "Hello%2CWorld!",
    "utm_campaign should be an encoded version of the message"
  );
  is(
    url.searchParams.get("utm_medium"),
    "show-heartbeat",
    "utm_medium should be the action name"
  );
  is(
    url.searchParams.get("utm_source"),
    "firefox",
    "utm_source should be firefox"
  );
});

/* generatePostAnswerURL shouldn't override existing values in the url */
add_task(async function postAnswerUrlNoOverwite() {
  const recipe = heartbeatRecipeFactory({
    arguments: {
      postAnswerUrl:
        "https://example.com/survey?utm_source=shady_tims_firey_fox",
    },
  });
  const action = new ShowHeartbeatAction();
  const url = new URL(await action.generatePostAnswerURL(recipe));
  is(
    url.searchParams.get("utm_source"),
    "shady_tims_firey_fox",
    "utm_source should not be overwritten"
  );
});

/* generatePostAnswerURL should only include userId if requested */
add_task(async function postAnswerUrlUserIdIfRequested() {
  const recipeWithId = heartbeatRecipeFactory({
    arguments: { includeTelemetryUUID: true },
  });
  const recipeWithoutId = heartbeatRecipeFactory({
    arguments: { includeTelemetryUUID: false },
  });
  const action = new ShowHeartbeatAction();

  const urlWithId = new URL(await action.generatePostAnswerURL(recipeWithId));
  is(
    urlWithId.searchParams.get("userId"),
    ClientEnvironment.userId,
    "clientId should be included"
  );

  const urlWithoutId = new URL(
    await action.generatePostAnswerURL(recipeWithoutId)
  );
  ok(!urlWithoutId.searchParams.has("userId"), "userId should not be included");
});

/* generateSurveyId should include userId only if requested */
decorate_task(
  withStubbedHeartbeat(),
  withClearStorage(),
  async function testGenerateSurveyId() {
    const recipeWithoutId = heartbeatRecipeFactory({
      arguments: { surveyId: "test-id", includeTelemetryUUID: false },
    });
    const recipeWithId = heartbeatRecipeFactory({
      arguments: { surveyId: "test-id", includeTelemetryUUID: true },
    });
    const action = new ShowHeartbeatAction();
    is(
      action.generateSurveyId(recipeWithoutId),
      "test-id",
      "userId should not be included if not requested"
    );
    is(
      action.generateSurveyId(recipeWithId),
      `test-id::${ClientEnvironment.userId}`,
      "userId should be included if requested"
    );
  }
);

/* _set*, _get* and _clearAllStorage should store and retrieve data */
decorate_task(
  withClearStorage(),
  async function testProfileDatastoreServiceStorage() {
    // Make sure values return null before being set
    Assert.equal(await ShowHeartbeatAction._getLastShown(), null);
    Assert.equal(await ShowHeartbeatAction._getLastInteraction(), null);

    // Set values to check
    await ShowHeartbeatAction._setLastShown("recipe1", 1);
    await ShowHeartbeatAction._setLastShown("recipe2", 2);
    await ShowHeartbeatAction._setLastInteraction("recipe1", 3);
    await ShowHeartbeatAction._setLastInteraction("recipe2", 4);

    // Check that they are available
    Assert.equal(await ShowHeartbeatAction._getLastShown("recipe1"), 1);
    Assert.equal(await ShowHeartbeatAction._getLastShown("recipe2"), 2);
    Assert.equal(await ShowHeartbeatAction._getLastShown(), 2);
    Assert.equal(await ShowHeartbeatAction._getLastInteraction("recipe1"), 3);
    Assert.equal(await ShowHeartbeatAction._getLastInteraction("recipe2"), 4);
    Assert.equal(await ShowHeartbeatAction._getLastInteraction(), 4);

    // Check that clearing the old storage doesn't remove data.
    await Storage.clearAllStorage();
    Assert.equal(await ShowHeartbeatAction._getLastShown(), 2);
    Assert.equal(await ShowHeartbeatAction._getLastInteraction(), 4);

    // Check that clearing the storage removes data from multiple prefixes.
    await ShowHeartbeatAction._clearAllStorage();
    Assert.equal(await ShowHeartbeatAction._getLastShown("recipe1"), null);
    Assert.equal(await ShowHeartbeatAction._getLastShown("recipe2"), null);
    Assert.equal(await ShowHeartbeatAction._getLastShown(), null);
  }
);
