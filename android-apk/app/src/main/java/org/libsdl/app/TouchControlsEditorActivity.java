package org.libsdl.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Locale;

public final class TouchControlsEditorActivity extends Activity {
    private static final int CONTROL_COUNT = 12;
    private static final String[] NAMES = {"STICK", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "START", "BACK", "RSTICK"};
    private static final float[] DEFAULT_X = {0.135f, 0.865f, 0.927f, 0.865f, 0.865f, 0.075f, 0.925f, 0.075f, 0.803f, 0.555f, 0.445f, 0.680f};
    private static final float[] DEFAULT_Y = {0.760f, 0.885f, 0.760f, 0.650f, 0.570f, 0.090f, 0.090f, 0.185f, 0.185f, 0.070f, 0.070f, 0.820f};
    private static final float[] LEGACY_X = {0.135f, 0.865f, 0.927f, 0.803f, 0.865f, 0.075f, 0.925f, 0.075f, 0.925f, 0.555f, 0.445f, 0.680f};
    private static final float[] LEGACY_Y = {0.760f, 0.885f, 0.760f, 0.760f, 0.650f, 0.185f, 0.185f, 0.090f, 0.090f, 0.070f, 0.070f, 0.820f};
    private static final float[] SONIC_X = {0.135f, 0.850f, 0.760f, 0.780f, 0.880f, 0.075f, 0.925f, 0.075f, 0.780f, 0.555f, 0.445f, 0.680f};
    private static final float[] SONIC_Y = {0.760f, 0.850f, 0.850f, 0.680f, 0.680f, 0.090f, 0.090f, 0.185f, 0.480f, 0.070f, 0.070f, 0.820f};
    private static final float[] WEREHOG_X = {0.135f, 0.850f, 0.740f, 0.790f, 0.900f, 0.890f, 0.920f, 0.075f, 0.700f, 0.555f, 0.445f, 0.680f};
    private static final float[] WEREHOG_Y = {0.760f, 0.860f, 0.840f, 0.660f, 0.490f, 0.680f, 0.290f, 0.185f, 0.680f, 0.070f, 0.070f, 0.820f};

    private final float[] x = DEFAULT_X.clone();
    private final float[] y = DEFAULT_Y.clone();
    private int character = 0;
    private int preset = 1;
    private boolean legacy;
    private ControlCanvas editor;
    private TextView modeLabel;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        loadPreferences();
        buildScreen();
        loadLayout();
        if (!getPreferences(MODE_PRIVATE).getBoolean("tutorial_shown_0_6_0", false)) {
            new AlertDialog.Builder(this)
                .setTitle(R.string.controls_tutorial_title)
                .setMessage(R.string.controls_tutorial_message)
                .setPositiveButton(android.R.string.ok, null)
                .show();
            getPreferences(MODE_PRIVATE).edit().putBoolean("tutorial_shown_0_6_0", true).apply();
        }
    }

    private void buildScreen() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(18, 21, 26));

        editor = new ControlCanvas();
        root.addView(editor, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout topBar = new LinearLayout(this);
        topBar.setOrientation(LinearLayout.HORIZONTAL);
        topBar.setGravity(android.view.Gravity.CENTER_VERTICAL);
        topBar.setPadding(6, 1, 6, 1);
        topBar.setBackgroundColor(Color.argb(190, 12, 16, 21));
        FrameLayout.LayoutParams topParams = new FrameLayout.LayoutParams(-1, -2);
        topParams.gravity = android.view.Gravity.TOP;
        root.addView(topBar, topParams);

        TextView title = new TextView(this);
        title.setText("Editar controles de tela");
        title.setTextSize(13);
        title.setTextColor(Color.WHITE);
        title.setSingleLine(true);
        title.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1.4f));
        topBar.addView(title);

        topBar.addView(button("Sonic", view -> selectCharacter(0)));
        topBar.addView(button("Werehog", view -> selectCharacter(1)));

        Spinner presetSpinner = new Spinner(this);
        presetSpinner.setAdapter(new ArrayAdapter<String>(this, android.R.layout.simple_spinner_dropdown_item,
            new String[] {"Preset 1", "Preset 2", "Preset 3"}));
        presetSpinner.setSelection(preset - 1);
        presetSpinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                if (preset != position + 1) selectPreset(position + 1);
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
        });
        topBar.addView(presetSpinner, new LinearLayout.LayoutParams(0, -2, 1.0f));

        Button toggle = button("Alternar modo", view -> {
            legacy = !legacy;
            resetLayout();
            saveLayout();
            updateModeLabel();
            editor.invalidate();
        });
        topBar.addView(toggle);
        modeLabel = new TextView(this);
        modeLabel.setTextSize(12);
        modeLabel.setTextColor(Color.WHITE);
        modeLabel.setPadding(4, 0, 4, 0);
        topBar.addView(modeLabel);

        LinearLayout actionRow = row();
        actionRow.addView(button("Restaurar", view -> {
            resetLayout();
            editor.invalidate();
        }));
        actionRow.addView(button("Salvar", view -> {
            saveLayout();
            finish();
        }));
        FrameLayout.LayoutParams actionParams = new FrameLayout.LayoutParams(-1, -2);
        actionParams.gravity = android.view.Gravity.BOTTOM;
        actionRow.setPadding(10, 2, 10, 2);
        actionRow.setBackgroundColor(Color.argb(190, 12, 16, 21));
        root.addView(actionRow, actionParams);
        setContentView(root);
    }

    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        return row;
    }

    private Button button(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(listener);
        button.setLayoutParams(new LinearLayout.LayoutParams(0, -2, 1));
        return button;
    }

    private void selectCharacter(int value) {
        saveLayout();
        character = value;
        loadPreferences();
        loadLayout();
        editor.invalidate();
    }

    private void selectPreset(int value) {
        saveLayout();
        preset = value;
        loadLayout();
        editor.invalidate();
    }

    private void resetLayout() {
        float[] defaultX = character == 0 ? SONIC_X : WEREHOG_X;
        float[] defaultY = character == 0 ? SONIC_Y : WEREHOG_Y;
        System.arraycopy(legacy ? LEGACY_X : defaultX, 0, x, 0, CONTROL_COUNT);
        System.arraycopy(legacy ? LEGACY_Y : defaultY, 0, y, 0, CONTROL_COUNT);
    }

    private File root() {
        return AppStorage.activeGameRoot(this);
    }

    private File preferencesFile() {
        return new File(root(), "touch_controls.ini");
    }

    private File layoutFile() {
        return new File(root(), "touch_layout_" + (character == 1 ? "werehog" : "sonic") + "_" + preset + ".ini");
    }

    private void loadPreferences() {
        File file = preferencesFile();
        if (!file.isFile()) {
            legacy = false;
            preset = 1;
            updateModeLabel();
            return;
        }
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int equals = line.indexOf('=');
                if (equals < 0) continue;
                String key = line.substring(0, equals).trim();
                String value = line.substring(equals + 1).trim();
                if (character == 0 && key.equals("sonic_legacy")) legacy = value.equals("1");
                if (character == 1 && key.equals("werehog_legacy")) legacy = value.equals("1");
                if (character == 0 && key.equals("sonic_preset")) preset = clampPreset(value);
                if (character == 1 && key.equals("werehog_preset")) preset = clampPreset(value);
            }
        } catch (IOException ignored) {
        }
        updateModeLabel();
    }

    private int clampPreset(String value) {
        try {
            return Math.max(1, Math.min(3, Integer.parseInt(value)));
        } catch (NumberFormatException ignored) {
            return 1;
        }
    }

    private void savePreferences() throws IOException {
        File directory = root();
        if (!directory.exists() && !directory.mkdirs()) throw new IOException("Não foi possível criar a pasta de configuração");
        boolean sonicLegacy = character == 0 ? legacy : readMode(false);
        boolean werehogLegacy = character == 1 ? legacy : readMode(true);
        int sonicPreset = character == 0 ? preset : readPreset(false);
        int werehogPreset = character == 1 ? preset : readPreset(true);
        try (FileWriter writer = new FileWriter(preferencesFile(), false)) {
            writer.write("sonic_legacy=" + (sonicLegacy ? "1" : "0") + "\n");
            writer.write("werehog_legacy=" + (werehogLegacy ? "1" : "0") + "\n");
            writer.write("sonic_preset=" + sonicPreset + "\n");
            writer.write("werehog_preset=" + werehogPreset + "\n");
        }
    }

    private boolean readMode(boolean werehog) {
        return readPreference(werehog ? "werehog_legacy" : "sonic_legacy").equals("1");
    }

    private int readPreset(boolean werehog) {
        return clampPreset(readPreference(werehog ? "werehog_preset" : "sonic_preset"));
    }

    private String readPreference(String wanted) {
        try (BufferedReader reader = new BufferedReader(new FileReader(preferencesFile()))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int equals = line.indexOf('=');
                if (equals >= 0 && line.substring(0, equals).trim().equals(wanted)) return line.substring(equals + 1).trim();
            }
        } catch (IOException ignored) {
        }
        return wanted.contains("preset") ? "1" : "0";
    }

    private void loadLayout() {
        resetLayout();
        File file = layoutFile();
        if (!file.isFile()) return;
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int equals = line.indexOf('=');
                if (equals < 0) continue;
                String key = line.substring(0, equals).trim();
                String[] values = line.substring(equals + 1).trim().split(",");
                if (values.length != 2) continue;
                for (int i = 0; i < CONTROL_COUNT; i++) {
                    if (NAMES[i].equalsIgnoreCase(key)) {
                        x[i] = clamp(Float.parseFloat(values[0]));
                        y[i] = clamp(Float.parseFloat(values[1]));
                        break;
                    }
                }
            }
        } catch (IOException | NumberFormatException ignored) {
        }
    }

    private void saveLayout() {
        try {
            File directory = root();
            if (!directory.exists() && !directory.mkdirs()) return;
            try (FileWriter writer = new FileWriter(layoutFile(), false)) {
                writer.write("version=2\n");
                writer.write("scale=1.0\n");
                for (int i = 0; i < CONTROL_COUNT; i++)
                    writer.write(NAMES[i].toLowerCase(Locale.ROOT) + "=" + x[i] + "," + y[i] + "\n");
            }
            savePreferences();
        } catch (IOException ignored) {
        }
    }

    private float clamp(float value) {
        return Math.max(0.02f, Math.min(0.98f, value));
    }

    private void updateModeLabel() {
        if (modeLabel != null) modeLabel.setText(legacy ? "Legacy" : "Default");
    }

    private final class ControlCanvas extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int dragging = -1;

        ControlCanvas() {
            super(TouchControlsEditorActivity.this);
            paint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            setBackgroundColor(Color.rgb(28, 32, 38));
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float width = getWidth();
            float height = getHeight();
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(Math.max(18, height * 0.035f));
            paint.setColor(Color.WHITE);
            canvas.drawText(character == 0 ? "SONIC" : "WEREHOG", width * 0.5f, height * 0.07f, paint);
            paint.setTextSize(Math.max(12, height * 0.025f));
            paint.setColor(Color.LTGRAY);
            canvas.drawText("Arraste os controles para posicionar", width * 0.5f, height * 0.12f, paint);

            for (int i = 0; i < CONTROL_COUNT; i++) {
                if (!visible(i)) continue;
                float cx = x[i] * width;
                float cy = y[i] * height;
                float radius = Math.max(25, height * 0.065f);
                Bitmap buttonImage = !legacy ? buttonImage(i) : null;
                if (buttonImage != null) {
                    drawButtonImage(canvas, buttonImage, cx, cy, radius);
                    if (i == dragging) {
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setStrokeWidth(4);
                        paint.setColor(Color.rgb(255, 190, 50));
                        canvas.drawCircle(cx, cy, radius, paint);
                        paint.setStyle(Paint.Style.FILL);
                    }
                } else {
                    paint.setColor(i == dragging ? Color.argb(220, 255, 190, 50) : Color.argb(165, 30, 36, 44));
                    canvas.drawCircle(cx, cy, radius, paint);
                    paint.setColor(Color.argb(235, 255, 255, 255));
                    paint.setTextSize(Math.max(12, radius * 0.42f));
                    canvas.drawText(NAMES[i], cx, cy + paint.getTextSize() * 0.35f, paint);
                }
            }
        }

        private Bitmap buttonImage(int index) {
            int resource = 0;
            if (character == 0) {
                if (index == 1) resource = R.drawable.sonicjumpxbox;
                else if (index == 2) resource = R.drawable.sonicssxbox;
                else if (index == 3) resource = R.drawable.sonicboostxbox;
                else if (index == 4) resource = R.drawable.sonicdashxbox;
                else if (index == 8) resource = R.drawable.sonicdriftxbox;
            } else {
                if (index == 1) resource = R.drawable.werehogjumpxbox;
                else if (index == 2) resource = R.drawable.werehoggrabxbox;
                else if (index == 3) resource = R.drawable.werehogatk1xbox;
                else if (index == 4) resource = R.drawable.werehogatk2xbox;
                else if (index == 5) resource = R.drawable.werehogguardxbox;
                else if (index == 6) resource = R.drawable.werehogunleashxbox;
                else if (index == 8) resource = R.drawable.werehogdashxbox;
            }
            return resource == 0 ? null : BitmapFactory.decodeResource(getResources(), resource);
        }

        private void drawButtonImage(Canvas canvas, Bitmap image, float cx, float cy, float radius) {
            float aspect = image.getHeight() == 0 ? 1.0f : (float) image.getWidth() / image.getHeight();
            float halfWidth = radius * Math.max(0.75f, aspect);
            RectF destination = new RectF(cx - halfWidth, cy - radius, cx + halfWidth, cy + radius);
            paint.setAlpha(190);
            canvas.drawBitmap(image, null, destination, paint);
            paint.setAlpha(255);
        }

        private boolean visible(int index) {
            if (legacy) return true;
            if (character == 0) return index == 0 || (index >= 1 && index <= 4) || index == 8 || index == 9;
            return index == 0 || (index >= 1 && index <= 4) || (index >= 5 && index <= 6) || index == 8 || index == 9;
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            float nx = clamp(event.getX() / Math.max(1, getWidth()));
            float ny = clamp(event.getY() / Math.max(1, getHeight()));
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                float best = 0.08f * getHeight();
                for (int i = 0; i < CONTROL_COUNT; i++) {
                    if (!visible(i)) continue;
                    float dx = event.getX() - x[i] * getWidth();
                    float dy = event.getY() - y[i] * getHeight();
                    if (dx * dx + dy * dy <= best * best) {
                        dragging = i;
                        break;
                    }
                }
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_MOVE && dragging >= 0) {
                x[dragging] = nx;
                y[dragging] = ny;
                invalidate();
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_UP || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
                dragging = -1;
                return true;
            }
            return true;
        }
    }
}
