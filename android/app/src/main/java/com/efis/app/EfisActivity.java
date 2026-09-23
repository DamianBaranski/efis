/**
 * \file EfisActivity.java
 * Android host. Speaks with the UK voice and downloads tiles for native code.
 */
package com.efis.app;

import android.speech.tts.TextToSpeech;
import android.speech.tts.Voice;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Locale;
import java.util.Set;

/**
 * Tablet host. Owns UK text-to-speech and HTTP downloads for native code.
 * Native code calls the public static methods through JNI.
 */
public class EfisActivity extends SDLActivity {
    private static TextToSpeech sTts;
    private static boolean sTtsReady;
    private static String sPending;
    private static boolean sPendingFlush;
    private static volatile String sVoiceCatalog = "";
    private static String sSelectedVoice = "";

    /**
     * Stops and releases the speech engine before the activity goes away.
     */
    @Override
    protected void onDestroy() {
        if (sTts != null) {
            sTts.stop();
            sTts.shutdown();
            sTts = null;
            sTtsReady = false;
        }
        sVoiceCatalog = "";
        super.onDestroy();
    }

    /**
     * Creates the speech engine on the UI thread when it does not exist yet.
     * Language is UK and the rate is 0.9. A phrase queued before the engine
     * is ready is spoken when the engine reports success.
     *
     * @param activity Host activity. Null is ignored.
     */
    private static void ensureEngine(final SDLActivity activity) {
        if (sTts != null || activity == null) {
            return;
        }
        sTts = new TextToSpeech(activity.getApplicationContext(), status -> {
            if (status != TextToSpeech.SUCCESS || sTts == null) {
                return;
            }
            sTts.setLanguage(Locale.UK);
            sTts.setSpeechRate(0.9f);
            sTtsReady = true;
            refreshVoiceCatalog();
            applySelectedVoice();
            final String pending = sPending;
            final boolean pendingFlush = sPendingFlush;
            sPending = null;
            if (pending != null) {
                sTts.speak(pending, pendingFlush ? TextToSpeech.QUEUE_FLUSH : TextToSpeech.QUEUE_ADD, null, "efis");
            }
        });
    }

    /**
     * Short label for the sound page, such as "en-GB" or "en-GB ALAN".
     *
     * @param voice Installed voice. The locale must be present.
     * @return Label text. Never null.
     */
    private static String voiceLabel(final Voice voice) {
        final Locale locale = voice.getLocale();
        final String country = locale.getCountry() == null || locale.getCountry().isEmpty()
                ? locale.getLanguage().toUpperCase(Locale.UK)
                : locale.getCountry();
        String token = "";
        final String[] parts = voice.getName().toLowerCase(Locale.UK).split("-");
        for (int i = parts.length - 1; i >= 0; --i) {
            final String part = parts[i];
            if (part.isEmpty() || part.equals("local") || part.equals("network") || part.equals("x")
                    || part.equals("en") || part.equals(country.toLowerCase(Locale.UK))) {
                continue;
            }
            token = part.toUpperCase(Locale.UK);
            break;
        }
        return token.isEmpty() ? "en-" + country : "en-" + country + " " + token;
    }

    /**
     * Rebuilds the voice list from installed English voices that do not need a network.
     * Each line is the engine name, a tab, then the short label. UK voices come first.
     */
    private static void refreshVoiceCatalog() {
        if (sTts == null) {
            return;
        }
        final Set<Voice> voices = sTts.getVoices();
        if (voices == null) {
            return;
        }
        final ArrayList<Voice> english = new ArrayList<>();
        for (Voice voice : voices) {
            if (voice == null || voice.isNetworkConnectionRequired() || voice.getLocale() == null) {
                continue;
            }
            if (!"en".equals(voice.getLocale().getLanguage())) {
                continue;
            }
            english.add(voice);
        }
        Collections.sort(english, (a, b) -> {
            final String ac = a.getLocale().getCountry() == null ? "" : a.getLocale().getCountry();
            final String bc = b.getLocale().getCountry() == null ? "" : b.getLocale().getCountry();
            final int gb = Boolean.compare("GB".equals(bc), "GB".equals(ac));
            if (gb != 0) {
                return gb;
            }
            return a.getName().compareToIgnoreCase(b.getName());
        });
        final StringBuilder catalog = new StringBuilder();
        for (Voice voice : english) {
            if (catalog.length() > 0) {
                catalog.append('\n');
            }
            catalog.append(voice.getName()).append('\t').append(voiceLabel(voice));
        }
        sVoiceCatalog = catalog.toString();
    }

    /**
     * Applies sSelectedVoice when the engine is ready.
     * The value is matched against the engine voice name, not the short label.
     */
    private static void applySelectedVoice() {
        if (!sTtsReady || sTts == null || sSelectedVoice == null || sSelectedVoice.isEmpty()) {
            return;
        }
        final Set<Voice> voices = sTts.getVoices();
        if (voices == null) {
            return;
        }
        for (Voice voice : voices) {
            if (voice != null && sSelectedVoice.equals(voice.getName())) {
                sTts.setVoice(voice);
                return;
            }
        }
    }

    /**
     * Names of the installed UK voices, one per line.
     * Empty until the engine has reported its voice list.
     */
    public static String voiceCatalog() {
        final SDLActivity activity = mSingleton;
        if (activity != null) {
            activity.runOnUiThread(() -> ensureEngine(activity));
        }
        return sVoiceCatalog == null ? "" : sVoiceCatalog;
    }

    /**
     * Selects a voice by the label voiceCatalog() printed.
     * Applied when the engine is ready.
     *
     * @param name Label from voiceCatalog(). Empty keeps the current voice.
     */
    public static void selectVoice(final String name) {
        sSelectedVoice = name == null ? "" : name;
        final SDLActivity activity = mSingleton;
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            ensureEngine(activity);
            applySelectedVoice();
        });
    }

    /**
     * Speaks one phrase on the UK voice.
     * A call before the engine is ready is kept and spoken when the engine starts.
     *
     * @param text Spoken text. Empty is ignored.
     * @param flush True replaces the current phrase. False waits its turn.
     */
    public static void speak(final String text, final boolean flush) {
        final SDLActivity activity = mSingleton;
        if (activity == null || text == null || text.isEmpty()) {
            return;
        }
        activity.runOnUiThread(() -> {
            if (sTts == null) {
                sPending = text;
                sPendingFlush = flush;
                ensureEngine(activity);
                return;
            }
            if (!sTtsReady) {
                sPending = text;
                sPendingFlush = flush;
                return;
            }
            applySelectedVoice();
            sTts.speak(text, flush ? TextToSpeech.QUEUE_FLUSH : TextToSpeech.QUEUE_ADD, null,
                    "efis-" + System.nanoTime());
        });
    }

    /**
     * Starts or stops the tablet GPS, gyro, accelerometer, and compass.
     *
     * @param on True registers the listeners. False removes them.
     */
    public static void setTabletSensors(final boolean on) {
        TabletSensors.setEnabled(mSingleton, on);
    }

    /**
     * Latest tablet sample. See TabletSensors.sample().
     *
     * @return Nine floats. Empty until the first call after the activity exists.
     */
    public static float[] tabletSample() {
        return TabletSensors.sample();
    }

    /**
     * Forwards the location permission result to the sensor listeners.
     */
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        TabletSensors.onPermission(requestCode, grantResults);
    }

    /**
     * Native libraries SDL loads before the first frame.
     * main is the EFIS library and must come after SDL.
     */
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "glm",
            "main"
        };
    }

    /**
     * Downloads url into destPath.
     *
     * @param url Source URL.
     * @param destPath Absolute file path on the device. Parent directories must exist.
     * @param apiKey Sent as the x-openaip-api-key header. Empty skips the header.
     * @return True when the file was written. False on any HTTP or IO failure.
     */
    public static boolean downloadUrl(String url, String destPath, String apiKey) {
        HttpURLConnection conn = null;
        InputStream in = null;
        FileOutputStream out = null;
        final File dest = new File(destPath);
        final File tmp = new File(destPath + ".part");
        try {
            final File parent = dest.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs()) {
                return false;
            }
            conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setInstanceFollowRedirects(true);
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(30000);
            conn.setRequestProperty("User-Agent", "efis-openaip/1.0");
            if (apiKey != null && !apiKey.isEmpty()) {
                conn.setRequestProperty("x-openaip-api-key", apiKey);
            }
            final int code = conn.getResponseCode();
            if (code == HttpURLConnection.HTTP_NO_CONTENT) {
                return false;
            }
            if (code != HttpURLConnection.HTTP_OK) {
                Log.w("efis", "Tile failed HTTP " + code + " " + url);
                return false;
            }
            in = conn.getInputStream();
            out = new FileOutputStream(tmp);
            final byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            out.close();
            out = null;
            if (dest.exists() && !dest.delete()) {
                return false;
            }
            return tmp.renameTo(dest);
        } catch (Exception e) {
            Log.w("efis", "Tile download failed " + url, e);
            return false;
        } finally {
            if (out != null) {
                try {
                    out.close();
                } catch (Exception ignored) {
                }
            }
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
            if (conn != null) {
                conn.disconnect();
            }
            if (tmp.exists() && !dest.exists()) {
                //noinspection ResultOfMethodCallIgnored
                tmp.delete();
            }
        }
    }
}
