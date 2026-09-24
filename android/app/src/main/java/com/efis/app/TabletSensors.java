/**
 * \file TabletSensors.java
 * GPS plus the gyro, accelerometer, and compass for the SOURCES tab.
 */
package com.efis.app;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.GnssStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;

/**
 * Tablet situation sample for the PX4 EKF.
 * Gyro and accelerometer are integrated in the aircraft frame and consumed by native code.
 * The compass and GPS are passed through for the same filter.
 * The screen faces the pilot. The top of the screen is up and the nose points into the glass.
 */
public final class TabletSensors {
    /** Native request code for the location permission. */
    public static final int REQUEST_LOCATION = 42;

    private static final Object LOCK = new Object();
    private static final float[] SAMPLE = new float[32];
    private static final Listener LISTENER = new Listener();
    private static final int GPS_FIX = 1;
    private static final int ATTITUDE = 2;
    private static final int GYRO_DATA = 4;
    private static final int ACCEL_DATA = 8;
    private static final int MAG_DATA = 16;
    private static final int GYRO_HW = 32;
    private static final int ACCEL_HW = 64;
    private static final int MAG_HW = 128;

    private static Activity sActivity;
    private static SensorManager sSensors;
    private static LocationManager sLocations;
    private static boolean sRunning;
    private static final float[] DELTA_ANG = new float[3];
    private static final float[] DELTA_VEL = new float[3];
    private static final float[] MAG_FRD = new float[3];
    private static float sDtAng;
    private static float sDtVel;
    private static long sGyroNs;
    private static long sAccelNs;
    private static long sLastAltMs;
    private static float sLastAlt;
    private static boolean sHaveAlt;
    private static GnssStatus.Callback sGnss;
    private static boolean sGnssRegistered;

    private TabletSensors() {
    }

    /**
     * Starts or stops the listeners.
     * Location updates wait until the user grants the location permission.
     *
     * @param activity Host activity. Required when turning the listeners on.
     * @param on True starts the listeners. False removes them.
     */
    public static void setEnabled(final Activity activity, final boolean on) {
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            if (on) {
                start(activity);
            } else {
                stop();
            }
        });
    }

    /**
     * Latest sample, thirty-two floats, for the PX4 EKF.
     * 0-2 latitude, longitude, altitude metres. 3 ground speed m/s. 4 vertical speed m/s, up positive.
     * 5 satellites in view. 6-7 unused. 8 flags.
     * 9-11 gyro rad/s in device axes. 12-14 accelerometer m/s2. 15-17 magnetometer microtesla.
     * 18-20 integrated gyro, body FRD, radians. 21-23 integrated specific force, body FRD, m/s.
     * 24 gyro integration interval, seconds. 25 accelerometer integration interval, seconds.
     * 26-28 magnetometer, body FRD, gauss. 29 GPS bearing degrees, or NaN. 30 horizontal accuracy, metres.
     * 31 satellites used in the fix.
     * Calling this consumes the integrated gyro and accelerometer chunks.
     * Flag bits: 0 GPS fix, 2 gyro sample, 3 accel sample, 4 compass sample,
     * 5 gyro present, 6 accel present, 7 compass present.
     *
     * @return A copy of the sample. Never null.
     */
    public static float[] sample() {
        synchronized (LOCK) {
            SAMPLE[18] = DELTA_ANG[0];
            SAMPLE[19] = DELTA_ANG[1];
            SAMPLE[20] = DELTA_ANG[2];
            SAMPLE[21] = DELTA_VEL[0];
            SAMPLE[22] = DELTA_VEL[1];
            SAMPLE[23] = DELTA_VEL[2];
            SAMPLE[24] = sDtAng;
            SAMPLE[25] = sDtVel;
            SAMPLE[26] = MAG_FRD[0];
            SAMPLE[27] = MAG_FRD[1];
            SAMPLE[28] = MAG_FRD[2];
            final float[] copy = SAMPLE.clone();
            java.util.Arrays.fill(DELTA_ANG, 0f);
            java.util.Arrays.fill(DELTA_VEL, 0f);
            sDtAng = 0f;
            sDtVel = 0f;
            return copy;
        }
    }

    /**
     * Continues the GPS request after the permission dialog.
     *
     * @param requestCode Code from the permission request.
     * @param grantResults Results, one per permission. Empty is a denial.
     */
    public static void onPermission(final int requestCode, final int[] grantResults) {
        if (requestCode != REQUEST_LOCATION || !sRunning || sActivity == null) {
            return;
        }
        if (grantResults != null && grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            requestLocations(sActivity);
        }
    }

    private static void start(final Activity activity) {
        sActivity = activity;
        sSensors = (SensorManager) activity.getSystemService(Activity.SENSOR_SERVICE);
        sLocations = (LocationManager) activity.getSystemService(Activity.LOCATION_SERVICE);
        sRunning = true;
        sDtAng = 0f;
        sDtVel = 0f;
        sGyroNs = 0L;
        sAccelNs = 0L;
        java.util.Arrays.fill(DELTA_ANG, 0f);
        java.util.Arrays.fill(DELTA_VEL, 0f);
        java.util.Arrays.fill(MAG_FRD, 0f);
        final boolean accel = listen(Sensor.TYPE_ACCELEROMETER);
        final boolean gyro = listen(Sensor.TYPE_GYROSCOPE) || listen(Sensor.TYPE_GYROSCOPE_UNCALIBRATED);
        final boolean compass = listen(Sensor.TYPE_MAGNETIC_FIELD) || listen(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED);
        orFlag((gyro ? GYRO_HW : 0) | (accel ? ACCEL_HW : 0) | (compass ? MAG_HW : 0));
        Log.i("efis", "Internal sensors gps=1 accel=" + accel + " gyro=" + gyro + " compass=" + compass
                + " horizon=gyro");
        if (activity.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(
                    new String[] {Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION},
                    REQUEST_LOCATION);
            return;
        }
        requestLocations(activity);
    }

    private static boolean listen(final int type) {
        if (sSensors == null) {
            return false;
        }
        final Sensor sensor = sSensors.getDefaultSensor(type);
        if (sensor == null) {
            return false;
        }
        sSensors.registerListener(LISTENER, sensor, SensorManager.SENSOR_DELAY_GAME);
        return true;
    }

    private static void requestLocations(final Activity activity) {
        if (sLocations == null || activity.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                != PackageManager.PERMISSION_GRANTED) {
            return;
        }
        try {
            final Location last = sLocations.getLastKnownLocation(LocationManager.GPS_PROVIDER);
            if (last != null) {
                LISTENER.onLocationChanged(last);
            }
            if (sLocations.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                sLocations.requestLocationUpdates(LocationManager.GPS_PROVIDER, 200L, 0f, LISTENER,
                        Looper.getMainLooper());
            }
            if (sLocations.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                sLocations.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000L, 0f, LISTENER,
                        Looper.getMainLooper());
            }
            if (sGnss == null) {
                sGnss = new GnssStatus.Callback() {
                    @Override
                    public void onSatelliteStatusChanged(final GnssStatus status) {
                        storeSatellites(status);
                    }
                };
            }
            if (!sGnssRegistered) {
                sLocations.registerGnssStatusCallback(sGnss, new Handler(Looper.getMainLooper()));
                sGnssRegistered = true;
            }
        } catch (SecurityException e) {
            Log.w("efis", "GPS permission missing", e);
        }
    }

    private static void storeSatellites(final GnssStatus status) {
        final int seen = status.getSatelliteCount();
        int used = 0;
        for (int i = 0; i < seen; i++) {
            if (status.usedInFix(i)) {
                used++;
            }
        }
        synchronized (LOCK) {
            SAMPLE[5] = seen;
            SAMPLE[31] = used;
        }
    }

    private static void stop() {
        sRunning = false;
        if (sSensors != null) {
            sSensors.unregisterListener(LISTENER);
        }
        if (sLocations != null) {
            try {
                sLocations.removeUpdates(LISTENER);
                if (sGnssRegistered && sGnss != null) {
                    sLocations.unregisterGnssStatusCallback(sGnss);
                }
            } catch (SecurityException ignored) {
            }
        }
        sGnssRegistered = false;
        synchronized (LOCK) {
            SAMPLE[5] = 0f;
            SAMPLE[8] = 0f;
            SAMPLE[31] = 0f;
        }
    }

    /**
     * Device axes to screen axes. X is right, Y is the top of the screen, Z is out of the glass.
     */
    private static void toScreen(final float x, final float y, final float z, final float[] out) {
        int rotation = Surface.ROTATION_0;
        if (sActivity != null) {
            rotation = sActivity.getWindowManager().getDefaultDisplay().getRotation();
        }
        if (rotation == Surface.ROTATION_90) {
            out[0] = y;
            out[1] = -x;
            out[2] = z;
        } else if (rotation == Surface.ROTATION_180) {
            out[0] = -x;
            out[1] = -y;
            out[2] = z;
        } else if (rotation == Surface.ROTATION_270) {
            out[0] = -y;
            out[1] = x;
            out[2] = z;
        } else {
            out[0] = x;
            out[1] = y;
            out[2] = z;
        }
    }

    /**
     * Device axes to aircraft FRD. Same rotation for gyro, accelerometer, and compass.
     * Screen: X right, Y top of the screen, Z out of the glass.
     * Body: forward into the glass, right along the screen, down toward the bottom edge.
     * 180° about forward: Right and Down are reversed so pitch and yaw match roll.
     */
    private static void toBody(final float x, final float y, final float z, final float[] out) {
        final float[] screen = new float[3];
        toScreen(x, y, z, screen);
        out[0] = -screen[2];
        out[1] = -screen[0];
        out[2] = screen[1];
    }

    private static void accumulate(final float x, final float y, final float z, final float dt,
            final float[] delta, final boolean angular) {
        final float[] body = new float[3];
        toBody(x, y, z, body);
        synchronized (LOCK) {
            delta[0] += body[0] * dt;
            delta[1] += body[1] * dt;
            delta[2] += body[2] * dt;
            if (angular) {
                sDtAng += dt;
            } else {
                sDtVel += dt;
            }
        }
    }

    private static void onGyro(final float x, final float y, final float z, final long timestampNs) {
        storeRaw(9, x, y, z, GYRO_DATA);
        if (sGyroNs == 0L) {
            sGyroNs = timestampNs;
            return;
        }
        final float dt = (timestampNs - sGyroNs) * 1e-9f;
        sGyroNs = timestampNs;
        if (dt <= 0f || dt > 0.1f) {
            return;
        }
        accumulate(x, y, z, dt, DELTA_ANG, true);
    }

    private static void onAccel(final float x, final float y, final float z, final long timestampNs) {
        storeRaw(12, x, y, z, ACCEL_DATA);
        if (sAccelNs == 0L) {
            sAccelNs = timestampNs;
            return;
        }
        final float dt = (timestampNs - sAccelNs) * 1e-9f;
        sAccelNs = timestampNs;
        if (dt <= 0f || dt > 0.1f) {
            return;
        }
        accumulate(x, y, z, dt, DELTA_VEL, false);
    }

    private static void onCompass(final float x, final float y, final float z) {
        storeRaw(15, x, y, z, MAG_DATA);
        final float[] body = new float[3];
        toBody(x, y, z, body);
        synchronized (LOCK) {
            MAG_FRD[0] = body[0] / 100f;
            MAG_FRD[1] = body[1] / 100f;
            MAG_FRD[2] = body[2] / 100f;
        }
    }

    private static void orFlag(final int bits) {
        if (bits == 0) {
            return;
        }
        synchronized (LOCK) {
            SAMPLE[8] = ((int) SAMPLE[8]) | bits;
        }
    }

    private static void storeRaw(final int base, final float x, final float y, final float z, final int bit) {
        synchronized (LOCK) {
            SAMPLE[base] = x;
            SAMPLE[base + 1] = y;
            SAMPLE[base + 2] = z;
            SAMPLE[8] = ((int) SAMPLE[8]) | bit;
        }
    }

    private static void storeFix(final Location location) {
        final float lat = (float) location.getLatitude();
        final float lon = (float) location.getLongitude();
        final boolean hasAlt = location.hasAltitude();
        final float alt = hasAlt ? (float) location.getAltitude() : 0f;
        final float speed = location.hasSpeed() ? location.getSpeed() : 0f;
        float vertical = 0f;
        final long now = location.getTime();
        if (hasAlt && sHaveAlt && now > sLastAltMs) {
            final float dt = (now - sLastAltMs) / 1000f;
            if (dt > 0.2f) {
                vertical = (alt - sLastAlt) / dt;
            }
        }
        if (hasAlt) {
            sLastAlt = alt;
            sLastAltMs = now;
            sHaveAlt = true;
        }
        synchronized (LOCK) {
            SAMPLE[0] = lat;
            SAMPLE[1] = lon;
            if (hasAlt) {
                SAMPLE[2] = alt;
                SAMPLE[4] = vertical;
            }
            SAMPLE[3] = speed;
            SAMPLE[29] = location.hasBearing() ? location.getBearing() : Float.NaN;
            SAMPLE[30] = location.hasAccuracy() ? location.getAccuracy() : 8f;
            SAMPLE[8] = ((int) SAMPLE[8]) | GPS_FIX;
        }
    }

    private static final class Listener implements SensorEventListener, LocationListener {
        @Override
        public void onSensorChanged(final SensorEvent event) {
            if (!sRunning || event == null || event.sensor == null) {
                return;
            }
            final int type = event.sensor.getType();
            if ((type == Sensor.TYPE_GYROSCOPE || type == Sensor.TYPE_GYROSCOPE_UNCALIBRATED)
                    && event.values.length >= 3) {
                onGyro(event.values[0], event.values[1], event.values[2], event.timestamp);
                return;
            }
            if (type == Sensor.TYPE_ACCELEROMETER && event.values.length >= 3) {
                onAccel(event.values[0], event.values[1], event.values[2], event.timestamp);
            } else if ((type == Sensor.TYPE_MAGNETIC_FIELD || type == Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED)
                    && event.values.length >= 3) {
                onCompass(event.values[0], event.values[1], event.values[2]);
            }
        }

        @Override
        public void onAccuracyChanged(final Sensor sensor, final int accuracy) {
        }

        @Override
        public void onLocationChanged(final Location location) {
            if (!sRunning || location == null) {
                return;
            }
            storeFix(location);
        }

        @Override
        public void onStatusChanged(final String provider, final int status, final Bundle extras) {
        }

        @Override
        public void onProviderEnabled(final String provider) {
        }

        @Override
        public void onProviderDisabled(final String provider) {
        }
    }
}
