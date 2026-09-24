/// \file gpws_voice.cpp
/// Second voice slot for ground-proximity phrases. Not linked into the app.

std::string VoiceAnnouncer::voiceIdFor(int slot)
{
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    const int index = slot == 1 ? mGpwsVoiceIndex : mVoiceIndex;
    if (mVoices.empty() || index < 0 || index >= static_cast<int>(mVoices.size()))
    {
        return "male1";
    }
    const std::string &id = mVoices[static_cast<size_t>(index)].id;
    return id.empty() ? "male1" : id;
}

int VoiceAnnouncer::gpwsVoiceIndex()
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty())
    {
        return 0;
    }
    return std::clamp(mGpwsVoiceIndex, 0, static_cast<int>(mVoices.size()) - 1);
}

void VoiceAnnouncer::selectGpwsVoice(int index)
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty() || index < 0 || index >= static_cast<int>(mVoices.size()))
    {
        return;
    }
    mGpwsVoiceIndex = index;
}

std::string VoiceAnnouncer::gpwsVoiceLabel()
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty() || mGpwsVoiceIndex < 0 || mGpwsVoiceIndex >= static_cast<int>(mVoices.size()))
    {
        return "DEFAULT";
    }
    return mVoices[static_cast<size_t>(mGpwsVoiceIndex)].label;
}

