/**
 * \file GeneralSettings.java
 * Android GENERAL page. A dark category list on the left and the chosen page on the right.
 */
package com.efis.app;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Settings list drawn over the flight picture. Values are applied on the engine thread
 * and stored in settings.json. Maps leaves the right side clear so the Europe chart shows through.
 */
public final class GeneralSettings {
    private static final int BG = 0xFF0A0D10;
    private static final int RAIL = 0xFF090C10;
    private static final int SELECTED = 0xFF152030;
    private static final int CARD = 0xFF131920;
    private static final int BORDER = 0xFF273444;
    private static final int INK = 0xFFF0F4F8;
    private static final int MUTED = 0xFF8899A8;
    private static final int CYAN = 0xFF00E5FF;
    private static final int BUTTON = 0xFF2A3848;
    private static final int BUTTON_HI = 0xFF365274;
    private static final int FIELD = 0xFF0A0F14;

    private static final String[] CATEGORIES = {"IMAGERY", "MAPS", "SOUND", "SITUATION", "DISPLAY"};

    private static Dialog sDialog;
    private static boolean sUpdating;
    private static boolean sDragFar;
    private static boolean sDragNear;
    private static int sCategory;
    private static int sShown = -1;
    private static Map<String, String> sValues = new LinkedHashMap<>();
    private static String[] sFarLabels = new String[0];
    private static String[] sNearLabels = new String[0];
    private static final Handler sHandler = new Handler(Looper.getMainLooper());
    private static Runnable sRefresh;

    private GeneralSettings() {
    }

    /**
     * Current GENERAL values as key=value lines.
     */
    public static native String snapshot();

    /**
     * Queues one edit. The engine applies it on the next frame and writes settings.json.
     */
    public static native void apply(String key, String value);

    /**
     * Shows the list. A second call while it is open does nothing.
     */
    public static void show(final Activity activity) {
        if (sDialog != null && sDialog.isShowing()) {
            return;
        }
        sCategory = 0;
        sValues = new LinkedHashMap<>();

        final int railW = dp(activity, 280);
        final LinearLayout rail = new LinearLayout(activity);
        rail.setOrientation(LinearLayout.VERTICAL);
        rail.setBackgroundColor(RAIL);
        rail.setLayoutParams(new LinearLayout.LayoutParams(railW, ViewGroup.LayoutParams.MATCH_PARENT));
        rail.setPadding(0, dp(activity, 12), 0, dp(activity, 12));

        final TextView heading = new TextView(activity);
        heading.setText("GENERAL");
        heading.setTextColor(CYAN);
        heading.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        heading.setPadding(dp(activity, 20), dp(activity, 8), dp(activity, 16), dp(activity, 12));
        rail.addView(heading);

        final TextView[] tabs = new TextView[CATEGORIES.length];
        final View[] marks = new View[CATEGORIES.length];
        for (int i = 0; i < CATEGORIES.length; ++i) {
            final int index = i;
            final LinearLayout row = new LinearLayout(activity);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setMinimumHeight(dp(activity, 72));
            final View mark = new View(activity);
            final LinearLayout.LayoutParams markLp = new LinearLayout.LayoutParams(dp(activity, 4), dp(activity, 40));
            markLp.setMargins(0, dp(activity, 8), 0, dp(activity, 8));
            mark.setLayoutParams(markLp);
            row.addView(mark);
            final TextView label = new TextView(activity);
            label.setText(CATEGORIES[i]);
            label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
            label.setGravity(Gravity.CENTER_VERTICAL);
            label.setPadding(dp(activity, 16), 0, dp(activity, 16), 0);
            row.addView(label, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            row.setOnClickListener(v -> select(activity, index));
            rail.addView(row, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            tabs[i] = label;
            marks[i] = mark;
        }

        final FrameLayout content = new FrameLayout(activity);
        content.setBackgroundColor(BG);
        final LinearLayout.LayoutParams contentLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f);
        content.setLayoutParams(contentLp);

        final LinearLayout imagery = page(activity);
        final TextView farSummary = body(activity);
        final SeekBar farSlider = slider(activity);
        final TextView nearSummary = body(activity);
        final SeekBar nearSlider = slider(activity);
        imagery.addView(card(activity, "Far ring", farSummary, farSlider));
        imagery.addView(card(activity, "Near ring", nearSummary, nearSlider));
        wireSlider(farSlider, farSummary, true);
        wireSlider(nearSlider, nearSummary, false);

        final LinearLayout sound = page(activity);
        final Knob narration = toggle(activity, "Narration", "narration");
        final Button voice = field(activity);
        final Knob airspace = toggle(activity, "Airspace", "airspace");
        final Knob reporting = toggle(activity, "Reporting points", "reporting");
        final Knob nearest = toggle(activity, "Nearest field", "nearest");
        final Knob obstacles = toggle(activity, "Obstacles", "obstacles");
        final Button preview = action(activity, "Test phrase", () -> apply("preview", "1"));
        final LinearLayout soundCard = cardShell(activity);
        soundCard.addView(narration);
        soundCard.addView(caption(activity, "Voice"));
        soundCard.addView(voice);
        soundCard.addView(airspace);
        soundCard.addView(reporting);
        soundCard.addView(nearest);
        soundCard.addView(obstacles);
        soundCard.addView(preview);
        sound.addView(soundCard);
        voice.setOnClickListener(v -> openList(activity, voice, voiceLabels(), integer(sValues.get("voice")), index -> {
            apply("voiceIndex", Integer.toString(index));
            schedule(80);
        }));

        final LinearLayout situation = page(activity);
        final Button source = field(activity);
        final Knob gps = toggle(activity, "GPS", "gps");
        final TextView gpsLine = body(activity);
        final Knob gyro = toggle(activity, "Gyroscope", "gyro");
        final TextView gyroLine = body(activity);
        final Knob accel = toggle(activity, "Accelerometer", "accelerometer");
        final TextView accelLine = body(activity);
        final Knob compass = toggle(activity, "Compass", "compass");
        final TextView compassLine = body(activity);
        final Button horizon = action(activity, "Set horizon", () -> {
            apply("horizon", "1");
            schedule(80);
        });
        final LinearLayout situationCard = cardShell(activity);
        situationCard.addView(caption(activity, "Source"));
        situationCard.addView(source);
        situationCard.addView(gps);
        situationCard.addView(gpsLine);
        situationCard.addView(gyro);
        situationCard.addView(gyroLine);
        situationCard.addView(accel);
        situationCard.addView(accelLine);
        situationCard.addView(compass);
        situationCard.addView(compassLine);
        situationCard.addView(horizon);
        situation.addView(situationCard);
        source.setOnClickListener(v -> openList(activity, source, sourceLabels(), sourceIndex(), index -> {
            apply("source", sourceValue(index));
            schedule(80);
        }));

        final LinearLayout display = page(activity);
        final Button horizontal = choice(activity, "Horizontal");
        final Button vertical = choice(activity, "Vertical");
        final LinearLayout displayRow = new LinearLayout(activity);
        displayRow.setOrientation(LinearLayout.HORIZONTAL);
        displayRow.addView(horizontal, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        displayRow.addView(vertical, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        final LinearLayout displayCard = cardShell(activity);
        displayCard.addView(caption(activity, "Layout"));
        displayCard.addView(displayRow);
        display.addView(displayCard);
        horizontal.setOnClickListener(v -> {
            apply("layout", "horizontal");
            schedule(80);
        });
        vertical.setOnClickListener(v -> {
            apply("layout", "vertical");
            schedule(80);
        });

        final View[] pages = {scroll(imagery), new View(activity), scroll(sound), scroll(situation), scroll(display)};
        for (final View page : pages) {
            content.addView(page, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }

        final LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(rail);
        root.addView(content);

        final Dialog dialog = new Dialog(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        dialog.setContentView(root);
        dialog.setCancelable(true);
        dialog.setCanceledOnTouchOutside(false);
        dialog.setOnDismissListener(d -> {
            sHandler.removeCallbacks(sRefresh);
            apply("coverage", "0");
            sShown = -1;
            sDialog = null;
        });
        sDialog = dialog;

        sRefresh = () -> {
            if (sDialog == null || !sDialog.isShowing()) {
                return;
            }
            sValues = parse(snapshot());
            paintTabs(tabs, marks);
            sFarLabels = labels(sValues, "far");
            sNearLabels = labels(sValues, "near");
            fillSlider(farSlider, farSummary, sFarLabels, integer(sValues.get("farIndex")), sDragFar);
            fillSlider(nearSlider, nearSummary, sNearLabels, integer(sValues.get("nearIndex")), sDragNear);
            setSwitch(narration, "1".equals(sValues.get("narration")));
            final boolean spoken = "1".equals(sValues.get("narration"));
            final float phrase = spoken ? 1f : 0.45f;
            airspace.setAlpha(phrase);
            reporting.setAlpha(phrase);
            nearest.setAlpha(phrase);
            obstacles.setAlpha(phrase);
            setSwitch(airspace, "1".equals(sValues.get("airspace")));
            setSwitch(reporting, "1".equals(sValues.get("reporting")));
            setSwitch(nearest, "1".equals(sValues.get("nearest")));
            setSwitch(obstacles, "1".equals(sValues.get("obstacles")));
            voice.setText(voiceCaption());
            setSwitch(gps, "1".equals(sValues.get("gps")));
            setSwitch(gyro, "1".equals(sValues.get("gyro")));
            setSwitch(accel, "1".equals(sValues.get("accelerometer")));
            setSwitch(compass, "1".equals(sValues.get("compass")));
            gpsLine.setText(sValues.getOrDefault("gpsLine", "N/A"));
            gyroLine.setText(sValues.getOrDefault("gyroLine", "N/A"));
            accelLine.setText(sValues.getOrDefault("accelLine", "N/A"));
            compassLine.setText(sValues.getOrDefault("compassLine", "N/A"));
            source.setText(sourceCaption());
            horizon.setText("1".equals(sValues.get("horizon")) ? "Horizon set — tap to clear" : "Set horizon");
            final boolean verticalOn = "vertical".equals(sValues.get("layout"));
            paintChoice(horizontal, !verticalOn);
            paintChoice(vertical, verticalOn);
            if (sShown != sCategory) {
                sShown = sCategory;
                showPage(activity, content, pages, sCategory);
            }
            sHandler.postDelayed(sRefresh, 400);
        };
        dialog.show();
        immersive(dialog);
        sRefresh.run();
    }

    private static void select(final Activity activity, final int index) {
        sCategory = index;
        if (sRefresh != null) {
            sHandler.removeCallbacks(sRefresh);
            sRefresh.run();
        }
    }

    private static void showPage(final Activity activity, final FrameLayout content, final View[] pages, final int index) {
        for (int i = 0; i < pages.length; ++i) {
            pages[i].setVisibility(i == index && index != 1 ? View.VISIBLE : View.GONE);
        }
        final boolean maps = index == 1;
        content.setVisibility(maps ? View.GONE : View.VISIBLE);
        final Dialog dialog = sDialog;
        if (dialog == null || dialog.getWindow() == null) {
            return;
        }
        final Window window = dialog.getWindow();
        window.setDimAmount(0f);
        window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        final WindowManager.LayoutParams lp = window.getAttributes();
        lp.gravity = Gravity.START | Gravity.TOP;
        if (maps) {
            lp.width = dp(activity, 280);
            lp.height = ViewGroup.LayoutParams.MATCH_PARENT;
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            window.setBackgroundDrawable(new ColorDrawable(RAIL));
            showChart(activity, true);
        } else {
            lp.width = ViewGroup.LayoutParams.MATCH_PARENT;
            lp.height = ViewGroup.LayoutParams.MATCH_PARENT;
            window.clearFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            window.setBackgroundDrawable(new ColorDrawable(BG));
            showChart(activity, false);
        }
        window.setAttributes(lp);
        immersive(dialog);
    }

    private static void showChart(final Activity activity, final boolean on) {
        if (!on) {
            apply("coverage", "0");
            return;
        }
        final int screenW;
        final int screenH;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final android.graphics.Rect bounds = activity.getWindowManager().getCurrentWindowMetrics().getBounds();
            screenW = bounds.width();
            screenH = bounds.height();
        } else {
            final DisplayMetrics metrics = new DisplayMetrics();
            activity.getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
            screenW = metrics.widthPixels;
            screenH = metrics.heightPixels;
        }
        final int rail = dp(activity, 280);
        apply("coverageRect", rail + ",0," + Math.max(1, screenW - rail) + "," + screenH);
        apply("coverage", "1");
    }

    private static void paintTabs(final TextView[] tabs, final View[] marks) {
        for (int i = 0; i < tabs.length; ++i) {
            final boolean on = i == sCategory;
            tabs[i].setTextColor(on ? INK : MUTED);
            tabs[i].setTypeface(on ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            marks[i].setBackgroundColor(on ? CYAN : Color.TRANSPARENT);
            ((View) tabs[i].getParent()).setBackgroundColor(on ? SELECTED : Color.TRANSPARENT);
        }
    }

    private static void wireSlider(final SeekBar bar, final TextView summary, final boolean far) {
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(final SeekBar seekBar, final int progress, final boolean fromUser) {
                if (!fromUser || sUpdating) {
                    return;
                }
                final String[] labels = far ? sFarLabels : sNearLabels;
                if (progress >= 0 && progress < labels.length) {
                    summary.setText(labels[progress]);
                }
            }

            @Override
            public void onStartTrackingTouch(final SeekBar seekBar) {
                if (far) {
                    sDragFar = true;
                } else {
                    sDragNear = true;
                }
            }

            @Override
            public void onStopTrackingTouch(final SeekBar seekBar) {
                if (far) {
                    sDragFar = false;
                } else {
                    sDragNear = false;
                }
                apply(far ? "farIndex" : "nearIndex", Integer.toString(seekBar.getProgress()));
                schedule(80);
            }
        });
    }

    private static void fillSlider(final SeekBar bar, final TextView summary, final String[] labels, final int index,
                                   final boolean dragging) {
        final int max = Math.max(0, labels.length - 1);
        sUpdating = true;
        if (bar.getMax() != max) {
            bar.setMax(max);
        }
        if (!dragging) {
            bar.setProgress(Math.max(0, Math.min(index, max)));
            if (index >= 0 && index < labels.length) {
                summary.setText(labels[index]);
            }
        }
        sUpdating = false;
    }

    private static String[] labels(final Map<String, String> values, final String prefix) {
        final int count = integer(values.get(prefix + "Count"));
        final String[] labels = new String[count];
        for (int i = 0; i < count; ++i) {
            labels[i] = values.getOrDefault(prefix + i, "");
        }
        return labels;
    }

    private static String[] voiceLabels() {
        final int count = integer(sValues.get("voiceCount"));
        final String[] labels = new String[Math.max(count, 1)];
        if (count == 0) {
            labels[0] = "DEFAULT";
            return labels;
        }
        for (int i = 0; i < count; ++i) {
            labels[i] = sValues.getOrDefault("voice" + i, "DEFAULT");
        }
        return labels;
    }

    private static String voiceCaption() {
        final String[] labels = voiceLabels();
        final int index = integer(sValues.get("voice"));
        if (index >= 0 && index < labels.length) {
            return labels[index];
        }
        return labels.length == 0 ? "DEFAULT" : labels[0];
    }

    private static String[] sourceLabels() {
        if ("1".equals(sValues.get("receiver"))) {
            return new String[] {"Simulator", "Tablet sensors", "Stratux"};
        }
        return new String[] {"Simulator", "Tablet sensors"};
    }

    private static String sourceValue(final int index) {
        if (index == 1) {
            return "internal";
        }
        if (index == 2) {
            return "stratux";
        }
        return "sim";
    }

    private static int sourceIndex() {
        final String source = sValues.getOrDefault("source", "sim");
        if ("internal".equals(source)) {
            return 1;
        }
        if ("stratux".equals(source)) {
            return 2;
        }
        return 0;
    }

    private static String sourceCaption() {
        final String[] labels = sourceLabels();
        final int index = sourceIndex();
        if (index >= 0 && index < labels.length) {
            return labels[index];
        }
        return labels[0];
    }

    private static void openList(final Activity activity, final View anchor, final String[] labels, final int selected,
                                 final Pick pick) {
        final LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setBackground(rounded(CARD, 0));
        final int pad = dp(activity, 4);
        list.setPadding(pad, pad, pad, pad);
        final PopupWindow popup = new PopupWindow(activity);
        popup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        for (int i = 0; i < labels.length; ++i) {
            final int index = i;
            final TextView row = new TextView(activity);
            row.setText(labels[i]);
            row.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
            row.setTextColor(INK);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setMinHeight(dp(activity, 56));
            row.setPadding(dp(activity, 16), dp(activity, 8), dp(activity, 16), dp(activity, 8));
            row.setBackgroundColor(i == selected ? SELECTED : Color.TRANSPARENT);
            row.setOnClickListener(v -> {
                popup.dismiss();
                pick.onPick(index);
            });
            list.addView(row);
        }
        final ScrollView scroll = new ScrollView(activity);
        scroll.setBackground(rounded(CARD, dp(activity, 8)));
        scroll.addView(list);
        popup.setContentView(scroll);
        popup.setWidth(Math.max(anchor.getWidth(), dp(activity, 240)));
        popup.setHeight(Math.min(dp(activity, 360), labels.length * dp(activity, 56) + dp(activity, 8)));
        popup.showAsDropDown(anchor);
    }

    private static void setSwitch(final Knob view, final boolean on) {
        sUpdating = true;
        view.setOn(on);
        sUpdating = false;
    }

    private static void paintChoice(final Button button, final boolean on) {
        button.setBackground(rounded(on ? BUTTON_HI : BUTTON, dp(button.getContext(), 8)));
        button.setTextColor(INK);
    }

    private static LinearLayout page(final Activity activity) {
        final LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(activity, 16), dp(activity, 16), dp(activity, 16), dp(activity, 16));
        return page;
    }

    private static ScrollView scroll(final View child) {
        final ScrollView scroll = new ScrollView(child.getContext());
        scroll.setFillViewport(true);
        scroll.addView(child);
        return scroll;
    }

    private static LinearLayout card(final Activity activity, final String title, final TextView summary, final SeekBar slider) {
        final LinearLayout card = cardShell(activity);
        card.addView(caption(activity, title));
        card.addView(summary);
        card.addView(slider);
        return card;
    }

    private static LinearLayout cardShell(final Activity activity) {
        final LinearLayout card = new LinearLayout(activity);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(roundedStroke(CARD, BORDER, dp(activity, 8), dp(activity, 1)));
        final int pad = dp(activity, 16);
        card.setPadding(pad, pad, pad, pad);
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(activity, 12);
        card.setLayoutParams(lp);
        return card;
    }

    private static TextView caption(final Activity activity, final String text) {
        final TextView view = new TextView(activity);
        view.setText(text);
        view.setTextColor(INK);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setPadding(0, dp(activity, 8), 0, dp(activity, 4));
        return view;
    }

    private static TextView body(final Activity activity) {
        final TextView view = new TextView(activity);
        view.setTextColor(MUTED);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        view.setPadding(0, 0, 0, dp(activity, 8));
        return view;
    }

    private static SeekBar slider(final Activity activity) {
        final SeekBar bar = new SeekBar(activity);
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(CYAN));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(BORDER));
        bar.setThumbTintList(android.content.res.ColorStateList.valueOf(CYAN));
        bar.setMinimumHeight(dp(activity, 48));
        bar.setPadding(dp(activity, 8), dp(activity, 8), dp(activity, 8), dp(activity, 12));
        return bar;
    }

    private static Knob toggle(final Activity activity, final String label, final String key) {
        final Knob view = new Knob(activity, label, key);
        view.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(activity, 64)));
        return view;
    }

    /**
     * A settings row with the label on the left and a cyan switch on the right.
     * The framework switch did not draw on this theme.
     */
    private static final class Knob extends View {
        private final String mLabel;
        private final String mKey;
        private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private boolean mOn;

        Knob(final Context context, final String label, final String key) {
            super(context);
            mLabel = label;
            mKey = key;
            setClickable(true);
            setOnClickListener(v -> {
                if (sUpdating) {
                    return;
                }
                mOn = !mOn;
                invalidate();
                apply(mKey, mOn ? "1" : "0");
                schedule(80);
            });
        }

        void setOn(final boolean on) {
            if (mOn == on) {
                return;
            }
            mOn = on;
            invalidate();
        }

        @Override
        protected void onDraw(final Canvas canvas) {
            final float textPx = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 18,
                    getResources().getDisplayMetrics());
            mPaint.setTextSize(textPx);
            mPaint.setTypeface(Typeface.DEFAULT);
            mPaint.setColor(INK);
            final Paint.FontMetrics metrics = mPaint.getFontMetrics();
            final float baseline = (getHeight() - metrics.ascent - metrics.descent) / 2f;
            canvas.drawText(mLabel, 0, baseline, mPaint);

            final float trackW = dp(getContext(), 52);
            final float trackH = dp(getContext(), 28);
            final float knob = dp(getContext(), 12);
            final float right = getWidth() - dp(getContext(), 4);
            final float left = right - trackW;
            final float top = (getHeight() - trackH) / 2f;
            mPaint.setColor(mOn ? 0x8800E5FF : BORDER);
            canvas.drawRoundRect(left, top, right, top + trackH, trackH / 2f, trackH / 2f, mPaint);
            final float cx = mOn ? right - knob - dp(getContext(), 4) : left + knob + dp(getContext(), 4);
            mPaint.setColor(mOn ? CYAN : MUTED);
            canvas.drawCircle(cx, getHeight() / 2f, knob, mPaint);
        }
    }

    private static Button field(final Activity activity) {
        final Button button = new Button(activity);
        button.setAllCaps(false);
        button.setTextColor(INK);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        button.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        button.setMinHeight(dp(activity, 56));
        button.setPadding(dp(activity, 16), 0, dp(activity, 16), 0);
        button.setBackground(roundedStroke(FIELD, BORDER, dp(activity, 8), dp(activity, 1)));
        return button;
    }

    private static Button action(final Activity activity, final String label, final Runnable action) {
        final Button button = new Button(activity);
        button.setAllCaps(false);
        button.setText(label);
        button.setTextColor(INK);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        button.setMinHeight(dp(activity, 64));
        button.setBackground(rounded(BUTTON, dp(activity, 8)));
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(activity, 12);
        button.setLayoutParams(lp);
        button.setOnClickListener(v -> action.run());
        return button;
    }

    private static Button choice(final Activity activity, final String label) {
        final Button button = new Button(activity);
        button.setAllCaps(false);
        button.setText(label);
        button.setTextColor(INK);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        button.setMinHeight(dp(activity, 64));
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.setMargins(dp(activity, 4), dp(activity, 8), dp(activity, 4), 0);
        button.setLayoutParams(lp);
        return button;
    }

    private static GradientDrawable rounded(final int fill, final int radius) {
        final GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(radius);
        return drawable;
    }

    private static GradientDrawable roundedStroke(final int fill, final int stroke, final int radius, final int width) {
        final GradientDrawable drawable = rounded(fill, radius);
        drawable.setStroke(Math.max(1, width), stroke);
        return drawable;
    }

    private static void immersive(final Dialog dialog) {
        final Window window = dialog.getWindow();
        if (window == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController bars = window.getInsetsController();
            if (bars != null) {
                bars.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    private static void schedule(final int delayMs) {
        if (sRefresh == null) {
            return;
        }
        sHandler.removeCallbacks(sRefresh);
        sHandler.postDelayed(sRefresh, delayMs);
    }

    private static Map<String, String> parse(final String text) {
        final Map<String, String> values = new LinkedHashMap<>();
        if (text == null) {
            return values;
        }
        for (final String line : text.split("\n")) {
            final int eq = line.indexOf('=');
            if (eq > 0) {
                values.put(line.substring(0, eq), line.substring(eq + 1));
            }
        }
        return values;
    }

    private static int integer(final String text) {
        if (text == null || text.isEmpty()) {
            return 0;
        }
        try {
            return Integer.parseInt(text);
        } catch (final NumberFormatException ignored) {
            return 0;
        }
    }

    private static int dp(final Context context, final int value) {
        if (context == null) {
            return value;
        }
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                context.getResources().getDisplayMetrics());
    }

    private interface Pick {
        void onPick(int index);
    }
}
