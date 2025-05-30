/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations

import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.translate.Language
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.translate.TranslationPageSettingOperation
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TranslationsDialogMiddlewareTest {
    private val browserStore = spyk(
        BrowserStore(
            BrowserState(
                tabs = listOf(createTab("https://www.mozilla.org", id = "tab1")),
                selectedTabId = "tab1",
            ),
        ),
    )
    private val settings = Settings(testContext)
    private val translationsDialogMiddleware =
        TranslationsDialogMiddleware(browserStore = browserStore, settings = settings)

    @Test
    fun `GIVEN translationState WHEN FetchSupportedLanguages action is called THEN call OperationRequestedAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(TranslationsDialogAction.FetchSupportedLanguages)
                .joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.OperationRequestedAction(
                        tabId = "tab1",
                        operation = TranslationOperation.FETCH_SUPPORTED_LANGUAGES,
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN TranslateAction from TranslationDialogStore is called THEN call TranslateAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(
                    initialFrom = Language("en", "English"),
                    initialTo = Language("fr", "France"),
                ),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(TranslationsDialogAction.TranslateAction).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.TranslateAction(
                        tabId = "tab1",
                        fromLanguage = "en",
                        toLanguage = "fr",
                        options = null,
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN RestoreTranslation from TranslationDialogStore is called THEN call TranslateRestoreAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(TranslationsDialogAction.RestoreTranslation).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.TranslateRestoreAction(
                        tabId = "tab1",
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN FetchDownloadFileSizeAction from TranslationDialogStore is called THEN call FetchTranslationDownloadSizeAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(
                TranslationsDialogAction.FetchDownloadFileSizeAction(
                    toLanguage = Language("en", "English"),
                    fromLanguage = Language("fr", "France"),
                ),
            ).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.FetchTranslationDownloadSizeAction(
                        tabId = "tab1",
                        fromLanguage = Language("fr", "France"),
                        toLanguage = Language("en", "English"),
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN FetchPageSettings from TranslationDialogStore is called THEN call FETCH_PAGE_SETTINGS from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(TranslationsDialogAction.FetchPageSettings).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.OperationRequestedAction(
                        tabId = "tab1",
                        operation = TranslationOperation.FETCH_PAGE_SETTINGS,
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN UpdatePageSettingsValue with action type AlwaysOfferPopup from TranslationDialogStore is called THEN call UpdateGlobalOfferTranslateSettingAction from BrowserStore`() =
        runTest {
            assertTrue(settings.offerTranslation)
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(
                TranslationsDialogAction.UpdatePageSettingsValue(
                    type = TranslationPageSettingsOption.AlwaysOfferPopup(),
                    checkValue = false,
                ),
            ).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.UpdateGlobalOfferTranslateSettingAction(
                        offerTranslation = false,
                    ),
                )
            }
            assertFalse(settings.offerTranslation)
        }

    @Test
    fun `GIVEN translationState WHEN UpdatePageSettingsValue with action type AlwaysTranslateLanguage from TranslationDialogStore is called THEN call UpdatePageSettingAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(
                TranslationsDialogAction.UpdatePageSettingsValue(
                    type = TranslationPageSettingsOption.AlwaysTranslateLanguage(),
                    checkValue = false,
                ),
            ).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.UpdatePageSettingAction(
                        tabId = "tab1",
                        operation = TranslationPageSettingOperation.UPDATE_ALWAYS_TRANSLATE_LANGUAGE,
                        setting = false,
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN UpdatePageSettingsValue with action type NeverTranslateLanguage from TranslationDialogStore is called THEN call UpdatePageSettingAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(
                TranslationsDialogAction.UpdatePageSettingsValue(
                    type = TranslationPageSettingsOption.NeverTranslateLanguage(),
                    checkValue = true,
                ),
            ).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.UpdatePageSettingAction(
                        tabId = "tab1",
                        operation = TranslationPageSettingOperation.UPDATE_NEVER_TRANSLATE_LANGUAGE,
                        setting = true,
                    ),
                )
            }
        }

    @Test
    fun `GIVEN translationState WHEN UpdatePageSettingsValue with action type NeverTranslateSite from TranslationDialogStore is called THEN call UpdatePageSettingAction from BrowserStore`() =
        runTest {
            val translationStore = TranslationsDialogStore(
                initialState = TranslationsDialogState(),
                middlewares = listOf(translationsDialogMiddleware),
            )
            translationStore.dispatch(
                TranslationsDialogAction.UpdatePageSettingsValue(
                    type = TranslationPageSettingsOption.NeverTranslateSite(),
                    checkValue = false,
                ),
            ).joinBlocking()

            translationStore.waitUntilIdle()

            verify {
                browserStore.dispatch(
                    TranslationsAction.UpdatePageSettingAction(
                        tabId = "tab1",
                        operation = TranslationPageSettingOperation.UPDATE_NEVER_TRANSLATE_SITE,
                        setting = false,
                    ),
                )
            }
        }
}
