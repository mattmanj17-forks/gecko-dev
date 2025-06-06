/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import com.google.android.play.core.review.ReviewManagerFactory
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

class TestReviewSettings(
    override var numberOfAppLaunches: Int = 0,
    var isDefault: Boolean = false,
    override var lastReviewPromptTimeInMillis: Long = 0,
) : ReviewSettings {
    override val isDefaultBrowser: Boolean
        get() = isDefault
}

@RunWith(RobolectricTestRunner::class)
class ReviewPromptControllerTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var playStoreController: PlayStoreReviewPromptController

    @Before
    fun setUp() {
        playStoreController = PlayStoreReviewPromptController(
            manager = ReviewManagerFactory.create(testContext),
            numberOfAppLaunches = { 5 },
        )
    }

    @Test
    fun promptReviewDoesNotSetMillis() = runTest {
        var promptWasCalled = false
        val settings = TestReviewSettings(
            numberOfAppLaunches = 5,
            isDefault = false,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { 100L },
            { promptWasCalled = true },
        )

        controller.reviewPromptIsReady = true
        controller.promptReview(HomeActivity())

        assertEquals(settings.lastReviewPromptTimeInMillis, 0L)
        assertFalse(promptWasCalled)
    }

    @Test
    fun promptReviewSetsMillisIfSuccessful() = runTest {
        var promptWasCalled = false
        val settings = TestReviewSettings(
            numberOfAppLaunches = 5,
            isDefault = true,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { 100L },
            { promptWasCalled = true },
        )

        controller.reviewPromptIsReady = true
        controller.promptReview(HomeActivity())
        assertEquals(100L, settings.lastReviewPromptTimeInMillis)
        assertTrue(promptWasCalled)
    }

    @Test
    fun promptReviewWillNotBeCalledIfNotReady() = runTest {
        var promptWasCalled = false
        val settings = TestReviewSettings(
            numberOfAppLaunches = 5,
            isDefault = true,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { 100L },
            { promptWasCalled = true },
        )

        controller.promptReview(HomeActivity())
        assertFalse(promptWasCalled)
    }

    @Test
    fun promptReviewWillUnreadyPromptAfterCalled() = runTest {
        var promptWasCalled = false
        val settings = TestReviewSettings(
            numberOfAppLaunches = 5,
            isDefault = true,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { 100L },
            { promptWasCalled = true },
        )

        controller.reviewPromptIsReady = true

        assertTrue(controller.reviewPromptIsReady)
        controller.promptReview(HomeActivity())

        assertFalse(controller.reviewPromptIsReady)
        assertTrue(promptWasCalled)
    }

    @Test
    fun trackApplicationLaunch() {
        val settings = TestReviewSettings(
            numberOfAppLaunches = 4,
            isDefault = true,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { 0L },
        )

        assertFalse(controller.reviewPromptIsReady)

        controller.trackApplicationLaunch()

        assertTrue(controller.reviewPromptIsReady)
    }

    @Test
    fun shouldShowPrompt() {
        val settings = TestReviewSettings(
            numberOfAppLaunches = 5,
            isDefault = true,
            lastReviewPromptTimeInMillis = 0L,
        )

        val controller = ReviewPromptController(
            playStoreController,
            settings,
            { TEST_TIME_NOW },
        )

        // Test first success criteria
        controller.reviewPromptIsReady = true
        assertTrue(controller.shouldShowPrompt())

        // Test with last prompt approx 4 months earlier
        settings.apply {
            numberOfAppLaunches = 5
            isDefault = true
            lastReviewPromptTimeInMillis = MORE_THAN_4_MONTHS_FROM_TEST_TIME_NOW
        }

        controller.reviewPromptIsReady = true
        assertTrue(controller.shouldShowPrompt())

        // Test without being the default browser
        settings.apply {
            numberOfAppLaunches = 5
            isDefault = false
            lastReviewPromptTimeInMillis = 0L
        }

        controller.reviewPromptIsReady = true
        assertFalse(controller.shouldShowPrompt())

        // Test with number of app launches < 5
        settings.apply {
            numberOfAppLaunches = 4
            isDefault = true
            lastReviewPromptTimeInMillis = 0L
        }

        controller.reviewPromptIsReady = true
        assertFalse(controller.shouldShowPrompt())

        // Test with last prompt less than 4 months ago
        settings.apply {
            numberOfAppLaunches = 5
            isDefault = true
            lastReviewPromptTimeInMillis = LESS_THAN_4_MONTHS_FROM_TEST_TIME_NOW
        }

        controller.reviewPromptIsReady = true
        assertFalse(controller.shouldShowPrompt())
    }

    companion object {
        private const val TEST_TIME_NOW = 1598416882805L
        private const val MORE_THAN_4_MONTHS_FROM_TEST_TIME_NOW = 1588048882804L
        private const val LESS_THAN_4_MONTHS_FROM_TEST_TIME_NOW = 1595824882905L
    }
}
