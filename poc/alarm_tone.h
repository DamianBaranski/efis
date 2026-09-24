/// \file alarm_tone.h
/// Synthesized GPWS chords. Speech stays on the voice announcer.
#ifndef ALARM_TONE_H
#define ALARM_TONE_H

/// Which alert the chord belongs to. The root changes. The color does not.
enum class AlarmChord
{
    None = 0,
    Terrain,
    Obstacle,
    Closure,
    Sink,
    TooLow,
    DontSink,
    FiveHundred,
    Bank,
};

/// Sine and triangle tones mixed into a jazz chord.
/// A caution is a major 9th: root, third, major seventh, ninth.
/// A warning is a dominant 7 sharp 9: the seventh sits under the raised ninth.
/// Each pulse blooms and then fades out. There is no held plateau.
class AlarmTone
{
public:
    /// Process-wide player.
    static AlarmTone &instance();

    /// Hold this chord until the next call. Level 0 is silence, 1 a slow pulse, 2 a faster one.
    void set(AlarmChord chord, int level);

    /// One caution pulse, used for the five-hundred callout. A live alert is left alone.
    void sting(AlarmChord chord);

    /// Caution chord, then the warning chord. Used by the GPWS test button.
    void demo();

private:
    AlarmTone() = default;
    void open();
    static void callback(void *userdata, unsigned char *stream, int length);

    unsigned mDevice = 0;
    bool mFailed = false;
};
#endif
