/// \file gpws_page.cpp
/// CONF GENERAL page for the ground-proximity gates. Not linked into the app.

void SettingsPopup::adjustGpws(int control)
{
    Gpws &gpws = Gpws::instance();
    switch (control)
    {
    case 0:
        gpws.setEnabled(!gpws.enabled());
        break;
    case 1:
        gpws.setTerrain(!gpws.terrain());
        break;
    case 2:
        gpws.setClosure(!gpws.closure());
        break;
    case 3:
        gpws.setObstacles(!gpws.obstacles());
        break;
    case 4:
        gpws.setSinkRate(!gpws.sinkRate());
        break;
    case 5:
        gpws.setTooLow(!gpws.tooLow());
        break;
    case 6:
        gpws.setDontSink(!gpws.dontSink());
        break;
    case 7:
        gpws.setFiveHundred(!gpws.fiveHundred());
        break;
    case 8:
        gpws.setBank(!gpws.bank());
        break;
    default:
        gpws.preview();
        break;
    }
    mKey.clear();
}

void SettingsPopup::drawGpwsPage(int screenH)
{
    Gpws &gpws = Gpws::instance();
    VoiceAnnouncer &speaker = VoiceAnnouncer::instance();
    const int boxX = mWindow.contentX();
    const int boxY = mWindow.contentY();
    const int boxW = mWindow.contentW();
    const int boxH = mWindow.contentH();
    constexpr int kRows = 11;
    const int rowH = std::max(1, boxH / kRows);
    const int btnH = std::clamp(rowH * 2 / 3, 28, 56);
    const int btnW = std::clamp(boxW / 5, 96, 160);
    const float font = static_cast<float>(std::clamp(btnH / 3, 13, 22));
    const char *names[kRows] = {"GPWS",    "VOICE",   "TERRAIN", "CLOSURE", "OBSTACLE", "SINK RATE",
                                "TOO LOW", "DONT SINK", "500 FEET", "BANK", "TEST"};
    const int labelX = boxX + boxW / 12;
    const int btnX = boxX + boxW - btnW - boxW / 14;
    const bool on[9] = {gpws.enabled(), gpws.terrain(), gpws.closure(), gpws.obstacles(), gpws.sinkRate(),
                        gpws.tooLow(), gpws.dontSink(), gpws.fiveHundred(), gpws.bank()};
    const int slots[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    const int rows[9] = {0, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 9; ++i)
    {
        const int sdlRowTop = boxY + rows[i] * rowH;
        const int btnY = sdlRowTop + (rowH - btnH) / 2;
        const float textY = static_cast<float>(screenH - (sdlRowTop + rowH / 2));
        const std::string nameKey = std::string("efis-gpws-name-") + std::to_string(rows[i]);
        mWindow.setText(rows[i], names[rows[i]], font, static_cast<float>(labelX), textY, nameKey.c_str());
        const char *label = on[i] ? "ON" : "OFF";
        const std::string buttonKey = std::string("efis-gpws-btn-") + std::to_string(slots[i]) + "-" + label;
        mWindow.setButton(slots[i], btnX, btnY, btnW, btnH, label, font, buttonKey.c_str());
    }
    const int testTop = boxY + 10 * rowH;
    const int testY = testTop + (rowH - btnH) / 2;
    const float testTextY = static_cast<float>(screenH - (testTop + rowH / 2));
    mWindow.setText(10, "TEST", font, static_cast<float>(labelX), testTextY, "efis-gpws-name-10");
    mWindow.setButton(9, btnX, testY, btnW, btnH, "PLAY", font, "efis-gpws-btn-9-PLAY");

    const int voiceTop = boxY + rowH;
    const int voiceY = voiceTop + (rowH - btnH) / 2;
    const float voiceTextY = static_cast<float>(screenH - (voiceTop + rowH / 2));
    const int dropW = std::clamp(boxW * 46 / 100, 220, 480);
    const int dropX = boxX + boxW - dropW - boxW / 14;
    mWindow.setText(1, "VOICE", font, static_cast<float>(labelX), voiceTextY, "efis-gpws-name-1");
    mGpwsVoice.setFont(font);
    mGpwsVoice.place(dropX, voiceY, dropW, btnH);
    mGpwsVoice.setItems(speaker.voiceLabels(), speaker.gpwsVoiceIndex());
}

bool SettingsPopup::handleGpwsVoice(int x, int y)
{
    if (mWindow.activeTab() != 3)
    {
        return false;
    }
    const Dropdown::Click click = mGpwsVoice.mouseClick(x, y);
    if (!click.consumed)
    {
        return false;
    }
    if (click.chosen >= 0)
    {
        VoiceAnnouncer::instance().selectGpwsVoice(click.chosen);
        mKey.clear();
    }
    return true;
}
