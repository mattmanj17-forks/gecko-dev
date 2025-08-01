/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.content.SharedPreferences
import androidx.fragment.app.FragmentActivity
import androidx.preference.CheckBoxPreference
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkObject
import io.mockk.mockkStatic
import io.mockk.unmockkStatic
import io.mockk.verify
import mozilla.components.service.pocket.PocketStoriesService
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.pocket.ContentRecommendationsFeatureHelper
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
internal class HomeSettingsFragmentTest {
    private lateinit var homeSettingsFragment: HomeSettingsFragment
    private lateinit var appSettings: Settings
    private lateinit var appPrefs: SharedPreferences
    private lateinit var appPrefsEditor: SharedPreferences.Editor
    private lateinit var pocketService: PocketStoriesService
    private lateinit var appStore: AppStore

    @Before
    fun setup() {
        mockkStatic("org.mozilla.fenix.ext.FragmentKt")
        mockkStatic("org.mozilla.fenix.ext.ContextKt")
        appPrefsEditor = mockk(relaxed = true)
        appPrefs = mockk(relaxed = true) {
            every { edit() } returns appPrefsEditor
        }
        appSettings = mockk(relaxed = true) {
            every { preferences } returns appPrefs
        }
        every { any<Context>().settings() } returns appSettings
        appStore = mockk(relaxed = true)
        pocketService = mockk(relaxed = true)
        every { any<Context>().components } returns mockk {
            every { appStore } returns this@HomeSettingsFragmentTest.appStore
            every { core.pocketStoriesService } returns pocketService
        }

        homeSettingsFragment = HomeSettingsFragment()
    }

    @After
    fun teardown() {
        unmockkStatic("org.mozilla.fenix.ext.ContextKt")
        unmockkStatic("org.mozilla.fenix.ext.FragmentKt")
    }

    @Test
    fun `GIVEN the Pocket sponsored stories feature is disabled for the app WHEN accessing settings THEN the settings for it are not visible`() {
        mockkObject(ContentRecommendationsFeatureHelper) {
            every { ContentRecommendationsFeatureHelper.isPocketSponsoredStoriesFeatureEnabled(any()) } returns false

            activateFragment()

            assertFalse(getSponsoredStoriesPreference().isVisible)
        }
    }

    @Test
    fun `GIVEN the Pocket sponsored stories feature is enabled for the app WHEN accessing settings THEN the settings for it are visible`() {
        mockkObject(ContentRecommendationsFeatureHelper) {
            every { ContentRecommendationsFeatureHelper.isPocketSponsoredStoriesFeatureEnabled(any()) } returns true

            activateFragment()

            assertTrue(getSponsoredStoriesPreference().isVisible)
        }
    }

    @Test
    fun `GIVEN the Pocket sponsored stories preference is false WHEN accessing settings THEN the setting for it is unchecked`() {
        every { appSettings.showPocketSponsoredStories } returns false

        activateFragment()

        assertFalse(getSponsoredStoriesPreference().isChecked)
    }

    @Test
    fun `GIVEN the Pocket sponsored stories preference is true WHEN accessing settings THEN the setting for it is checked`() {
        every { appSettings.showPocketSponsoredStories } returns true

        activateFragment()

        assertTrue(getSponsoredStoriesPreference().isChecked)
    }

    @Test
    fun `GIVEN sponsored stories is disabled WHEN toggling the sponsored setting to enabled THEN start downloading sponsored stories`() {
        activateFragment()

        val result = getSponsoredStoriesPreference().callChangeListener(true)

        assertTrue(result)
        verify {
            appPrefsEditor.putBoolean(testContext.getString(R.string.pref_key_pocket_sponsored_stories), true)
            pocketService.startPeriodicSponsoredContentsRefresh()
        }
    }

    @Test
    fun `GIVEN sponsored stories is enabled WHEN toggling the sponsored stories setting to disabled THEN delete Pocket profile and remove sponsored contents from showing`() {
        activateFragment()

        val result = getSponsoredStoriesPreference().callChangeListener(false)

        assertTrue(result)
        verify {
            appPrefsEditor.putBoolean(testContext.getString(R.string.pref_key_pocket_sponsored_stories), false)
            pocketService.deleteUser()
            appStore.dispatch(
                ContentRecommendationsAction.SponsoredContentsChange(
                    sponsoredContents = emptyList(),
                ),
            )
        }
    }

    private fun activateFragment() {
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()
        activity.supportFragmentManager.beginTransaction()
            .add(homeSettingsFragment, "HomeSettingFragmentTest")
            .commitNow()
    }

    private fun getSponsoredStoriesPreference(): CheckBoxPreference =
        homeSettingsFragment.findPreference(
            homeSettingsFragment.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories),
        )!!
}
