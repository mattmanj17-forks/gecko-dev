/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.toolbar

import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.concept.menu.candidate.DecorativeTextMenuCandidate
import mozilla.components.concept.menu.candidate.TextMenuCandidate
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SearchSelectorMenuTest {

    private lateinit var menu: SearchSelectorMenu
    private val interactor = mockk<ToolbarInteractor>()

    @Before
    fun setup() {
        menu = SearchSelectorMenu(testContext, interactor)
    }

    @Test
    fun `WHEN building the menu items THEN the header is the first item AND the search settings is the last item`() {
        every { interactor.onMenuItemTapped(any()) } just Runs

        val items = menu.menuItems(listOf())
        val lastItem = (items.last() as TextMenuCandidate)
        lastItem.onClick()

        assertEquals(
            testContext.getString(R.string.search_header_menu_item_2),
            (items.first() as DecorativeTextMenuCandidate).text,
        )
        assertEquals(
            testContext.getString(R.string.search_settings_menu_item),
            lastItem.text,
        )
        verify { interactor.onMenuItemTapped(SearchSelectorMenu.Item.SearchSettings) }
    }
}
