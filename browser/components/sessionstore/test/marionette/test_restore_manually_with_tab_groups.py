# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
from urllib.parse import quote

# add this directory to the path
sys.path.append(os.path.dirname(__file__))

from marionette_driver import Wait, errors
from session_store_test_case import SessionStoreTestCase


def inline(doc):
    return "data:text/html;charset=utf-8,{}".format(quote(doc))


class TestSessionRestoreWithTabGroups(SessionStoreTestCase):
    def setUp(self):
        super(TestSessionRestoreWithTabGroups, self).setUp(
            startup_page=1,
            include_private=False,
            restore_on_demand=True,
            test_windows=set(
                [
                    (
                        inline("""<div">lorem</div>"""),
                        inline("""<div">ipsum</div>"""),
                        inline("""<div">dolor</div>"""),
                        inline("""<div">sit</div>"""),
                    ),
                ]
            ),
        )

    def test_no_restore_with_quit(self):
        self.wait_for_windows(
            self.all_windows, "Not all requested windows have been opened"
        )
        self.marionette.execute_async_script(
            """
            let resolve = arguments[0];
            gBrowser.addTabGroup([gBrowser.tabs[2], gBrowser.tabs[3]], { id: "group-left-open", label: "open" });
            let closedGroup = gBrowser.addTabGroup([gBrowser.tabs[1]], { id:"group-closed", label: "closed" });
            gBrowser.removeTabGroup(closedGroup);

            let { TabStateFlusher } = ChromeUtils.importESModule("resource:///modules/sessionstore/TabStateFlusher.sys.mjs");
            TabStateFlusher.flushWindow(gBrowser.ownerGlobal).then(resolve);
        """
        )

        self.marionette.quit()
        self.marionette.start_session()
        self.marionette.set_context("chrome")
        self.assertEqual(
            self.marionette.execute_script(
                "return !SessionStore.getWindowState(gBrowser.ownerGlobal).windows[0].closedGroups.find(group => group.id == 'group-left-open')"
            ),
            True,
            "Group that was left open is NOT in closedGroups",
        )

        self.assertEqual(
            self.marionette.execute_script(
                "return SessionStore.getWindowState(gBrowser.ownerGlobal).windows[0].closedGroups.length"
            ),
            1,
            msg="There is one closed group in the window",
        )

        self.assertEqual(
            self.marionette.execute_script(
                "return SessionStore.getWindowState(gBrowser.ownerGlobal).windows[0].closedGroups[0].id"
            ),
            "group-closed",
            msg="Correct group appears in closedGroups",
        )

        self.assertEqual(
            self.marionette.execute_script("return SessionStore.savedGroups.length"),
            1,
            msg="There is one saved group",
        )

        self.assertEqual(
            self.marionette.execute_script("return SessionStore.savedGroups[0].id"),
            "group-left-open",
            msg="The open group from last session is now saved",
        )

        self.assertEqual(
            self.marionette.execute_script(
                "return SessionStore.getWindowState(gBrowser.ownerGlobal).windows[0].closedGroups[0].tabs.length"
            ),
            1,
            msg="Closed group has 1 tab",
        )

        self.assertEqual(
            self.marionette.execute_script(
                "return SessionStore.savedGroups[0].tabs.length"
            ),
            2,
            msg="Saved group has 2 tabs",
        )

        self.marionette.execute_script(
            """
            SessionStore.restoreLastSession();
        """
        )
        self.wait_for_tabcount(1, "Waiting for 1 tab")

        self.assertEqual(
            self.marionette.execute_script("return gBrowser.tabs.length"),
            1,
            msg="1 tab was restored",
        )

        self.assertEqual(
            self.marionette.execute_script("return SessionStore.savedGroups?.length"),
            True,
            msg="We still have one saved group",
        )

        self.marionette.execute_script(
            "SessionStore.forgetSavedTabGroup('group-left-open')"
        )

    def wait_for_tabcount(self, expected_tabcount, message, timeout=5):
        current_tabcount = None

        def check(_):
            nonlocal current_tabcount
            current_tabcount = self.marionette.execute_script(
                "return gBrowser.tabs.length;"
            )
            return current_tabcount == expected_tabcount

        try:
            wait = Wait(self.marionette, timeout=timeout, interval=0.1)
            wait.until(check, message=message)
        except errors.TimeoutException as e:
            # Update the message to include the most recent list of windows
            message = (
                f"{e.message}. Expected {expected_tabcount}, got {current_tabcount}."
            )
            raise errors.TimeoutException(message)
