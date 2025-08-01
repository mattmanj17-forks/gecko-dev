import pytest

URL = "https://virtualvisit.telushealth.com/"

SUPPORTED_CSS = "#inviteIdInput"
UNSUPPORTED_CSS = "#browserNotSupported"


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(UNSUPPORTED_CSS, is_displayed=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(UNSUPPORTED_CSS, is_displayed=True)
    assert not client.find_css(SUPPORTED_CSS, is_displayed=True)
