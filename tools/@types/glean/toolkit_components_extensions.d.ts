/**
 * NOTE: Do not modify this file by hand.
 * Content was generated from source metrics.yaml files.
 * If you're updating some of the sources, see README for instructions.
 */

interface GleanImpl {

  extensions: {
    useRemotePref: GleanBoolean;
    useRemotePolicy: GleanBoolean;
    startupCacheLoadTime: GleanTimespan;
    startupCacheReadErrors: Record<string, GleanCounter>;
    startupCacheWriteBytelength: GleanQuantity;
    processEvent: Record<string, GleanCounter>;
  }

  extensionsApisDnr: {
    startupCacheReadSize: GleanMemoryDistribution;
    startupCacheReadTime: GleanTimingDistribution;
    startupCacheWriteSize: GleanMemoryDistribution;
    startupCacheWriteTime: GleanTimingDistribution;
    startupCacheEntries: Record<string, GleanCounter>;
    validateRulesTime: GleanTimingDistribution;
    evaluateRulesTime: GleanTimingDistribution;
    evaluateRulesCountMax: GleanQuantity;
  }

  extensionsData: {
    migrateResult: GleanEvent;
    storageLocalError: GleanEvent;
    syncUsageQuotas: GleanEvent;
    migrateResultCount: Record<string, GleanCounter>;
  }

  extensionsCounters: {
    browserActionPreloadResult: Record<string, GleanCounter>;
    eventPageIdleResult: Record<string, GleanCounter>;
  }

  extensionsTiming: {
    backgroundPageLoad: GleanTimingDistribution;
    backgroundPageLoadByAddonid: Record<string, GleanTimingDistribution>;
    browserActionPopupOpen: GleanTimingDistribution;
    browserActionPopupOpenByAddonid: Record<string, GleanTimingDistribution>;
    contentScriptInjection: GleanTimingDistribution;
    contentScriptInjectionByAddonid: Record<string, GleanTimingDistribution>;
    eventPageRunningTime: GleanCustomDistribution;
    eventPageRunningTimeByAddonid: Record<string, GleanTimingDistribution>;
    extensionStartup: GleanTimingDistribution;
    extensionStartupByAddonid: Record<string, GleanTimingDistribution>;
    pageActionPopupOpen: GleanTimingDistribution;
    pageActionPopupOpenByAddonid: Record<string, GleanTimingDistribution>;
    storageLocalGetIdb: GleanTimingDistribution;
    storageLocalGetIdbByAddonid: Record<string, GleanTimingDistribution>;
    storageLocalSetIdb: GleanTimingDistribution;
    storageLocalSetIdbByAddonid: Record<string, GleanTimingDistribution>;
  }
}
