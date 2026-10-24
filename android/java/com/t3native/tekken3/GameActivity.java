package com.t3native.tekken3;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.ContentValues;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PowerManager;
import android.net.wifi.WifiManager;
import android.provider.MediaStore;
import android.provider.Settings;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.text.InputFilter;
import android.text.InputType;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

/**
 * The game's activity: Android's NativeActivity (the game itself is libt3game.so) with the system
 * bars hidden while it is in front, as a full-screen game shows them only on a swipe from the edge.
 * Hiding them needs the UI thread, which the native code does not run on.
 *
 * It also asks for a line of text (a touch button's name, a host's address): a dialog with the system keyboard. The
 * game thread calls askText and then pollText each frame until the answer is there.
 */
public class GameActivity extends NativeActivity {
    private static final int MAX_TEXT = 12;
    // A host's address: "255.255.255.255:65535" at most, digits, dots and the port's colon (a comma,
    // which some keyboards' number rows put where the dot is, is read as a dot by the game).
    private static final int MAX_ADDRESS = 21;
    private static final String ADDRESS_CHARS = "0123456789.,:";
    private String textAnswer;   // "1" + the text (OK) or "0" (Cancel); null while none

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        preferSixtyMultiple();
    }

    /**
     * A display whose refresh is not a multiple of 60 Hz (a Galaxy A22's 90 Hz) shows the game's 60
     * steps a second unevenly: one frame for one refresh, the next for two (a stutter that looks
     * like flicker). Such a display is asked for a refresh of a multiple of 60 Hz (60, or 120): a
     * mode of the same size, or (Android 12+) its own mode's alternative rate, which is how the
     * A22 offers its 60 Hz (one mode listed, 90 Hz with 60 Hz as its alternative). One already at
     * 60 or 120 Hz (a Fold7) keeps its refresh.
     */
    private void preferSixtyMultiple() {
        if (Build.VERSION.SDK_INT < 23) return;
        android.view.Display display = getWindowManager().getDefaultDisplay();
        android.view.Display.Mode current = display.getMode();
        float hz = display.getRefreshRate();
        if (isSixtyMultiple(hz)) return;
        int modeId = 0;
        float rate = 0;
        for (android.view.Display.Mode m : display.getSupportedModes()) {
            if (m.getPhysicalWidth() != current.getPhysicalWidth() || m.getPhysicalHeight() != current.getPhysicalHeight()) continue;
            if (isSixtyMultiple(m.getRefreshRate()) && m.getRefreshRate() > rate) {
                rate = m.getRefreshRate();
                modeId = m.getModeId();
            }
        }
        if (rate == 0 && Build.VERSION.SDK_INT >= 31) {
            for (float alt : current.getAlternativeRefreshRates()) {
                if (isSixtyMultiple(alt) && alt > rate) rate = alt;
            }
        }
        if (rate == 0) return;
        WindowManager.LayoutParams params = getWindow().getAttributes();
        if (params.preferredRefreshRate == rate && params.preferredDisplayModeId == modeId) return;
        params.preferredDisplayModeId = modeId;   // 0: any mode, the rate below decides
        params.preferredRefreshRate = rate;
        getWindow().setAttributes(params);
        android.util.Log.i("t3native", "display: " + hz + " Hz, asked for " + rate + " Hz");
    }

    private static boolean isSixtyMultiple(float hz) {
        float k = hz / 60.0f;
        return Math.abs(k - Math.round(k)) < 0.05f && Math.round(k) >= 1;
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    /** Called from the game thread: shows the dialog on the UI thread (`address`: a host's address). */
    public void askText(final String title, final String initial, final boolean address) {
        synchronized (this) {
            textAnswer = null;
        }
        runOnUiThread(() -> {
            final EditText edit = new EditText(this);
            edit.setSingleLine(true);
            if (address) {
                // The URI keyboard has the dot on its first page; only the address's characters stay.
                edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
                InputFilter only = (source, start, end, dest, dstart, dend) -> {
                    StringBuilder kept = new StringBuilder();
                    for (int i = start; i < end; i++) {
                        if (ADDRESS_CHARS.indexOf(source.charAt(i)) >= 0) kept.append(source.charAt(i));
                    }
                    return kept.length() == end - start ? null : kept;
                };
                edit.setFilters(new InputFilter[] {new InputFilter.LengthFilter(MAX_ADDRESS), only});
                edit.setHint("192.168.1.20");
            } else {
                edit.setFilters(new InputFilter[] {new InputFilter.LengthFilter(MAX_TEXT)});
            }
            edit.setText(initial);
            edit.setSelection(edit.getText().length());
            final boolean[] answered = {false};
            AlertDialog dialog = new AlertDialog.Builder(this)
                    .setTitle(title)
                    .setView(edit)
                    .setPositiveButton(android.R.string.ok, (d, which) -> {
                        answered[0] = true;
                        answer("1" + edit.getText().toString());
                    })
                    .setNegativeButton(android.R.string.cancel, null)
                    .create();
            dialog.setOnDismissListener(d -> {
                if (!answered[0]) answer("0");
                hideSystemBars();
            });
            // The keyboard's Done key is OK too, and in landscape the field stays in the dialog above the
            // keyboard (no full-screen editor whose Done only hides the keyboard).
            edit.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            edit.setOnEditorActionListener((v, action, event) -> {
                if (action != EditorInfo.IME_ACTION_DONE) return false;
                answered[0] = true;
                answer("1" + edit.getText().toString());
                dialog.dismiss();
                return true;
            });
            dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            dialog.show();
            edit.requestFocus();
        });
    }

    private synchronized void answer(String text) {
        textAnswer = text;
    }

    /**
     * Called from the game thread: the pad's own motors (a DualSense's, an Xbox pad's), not the
     * phone's. weak / strong 0-255 for ms; all 0 stops them. Android 12+ gives a pad's motors one
     * by one (the first taken as the strong one); before, the pad's single Vibrator gets the
     * stronger level. A pad without motors stays still.
     */
    public void padRumble(int deviceId, int weak, int strong, int ms) {
        InputDevice device = InputDevice.getDevice(deviceId);
        if (device == null) return;
        try {
            if (Build.VERSION.SDK_INT >= 31) {
                VibratorManager manager = device.getVibratorManager();
                int[] ids = manager.getVibratorIds();
                if (ids.length == 0) return;
                if (weak <= 0 && strong <= 0) {
                    manager.cancel();
                    return;
                }
                CombinedVibration.ParallelCombination both = CombinedVibration.startParallel();
                if (ids.length == 1) {
                    both.addVibrator(ids[0], VibrationEffect.createOneShot(ms, Math.max(weak, strong)));
                } else {
                    if (strong > 0) both.addVibrator(ids[0], VibrationEffect.createOneShot(ms, strong));
                    if (weak > 0) both.addVibrator(ids[1], VibrationEffect.createOneShot(ms, weak));
                }
                manager.vibrate(both.combine());
            } else {
                Vibrator vibrator = device.getVibrator();
                if (!vibrator.hasVibrator()) return;
                if (weak <= 0 && strong <= 0) {
                    vibrator.cancel();
                } else {
                    vibrator.vibrate(VibrationEffect.createOneShot(ms, Math.max(weak, strong)));
                }
            }
        } catch (RuntimeException e) {
            // A pad gone mid-call, or one whose driver refuses: the game goes on without rumble.
        }
    }

    /**
     * Called from the game thread every few seconds: a performance log line, written on its own
     * thread (a write to shared storage can take milliseconds). The first one makes
     * t3native_perf_<time>.txt in Downloads (MediaStore, Android 10+; before, the app's own files
     * folder) with the phone's name, kept open; each line gets the battery's temperature and the
     * thermal headroom (Android 11+: 1.0 is where the system starts to slow the phone down).
     */
    public void perfLine(final String line) {
        perfWriter.execute(() -> {
            try {
                if (perfOut == null) perfOut = openPerfFile();
                if (perfOut == null) return;
                String text = line + " " + temperatures() + "\n";
                perfOut.write(text.getBytes(StandardCharsets.UTF_8));
                perfOut.flush();
            } catch (Exception e) {
                perfOut = null;   // tried again with the next line
            }
        });
    }

    private final ExecutorService perfWriter = Executors.newSingleThreadExecutor();
    private OutputStream perfOut;

    private OutputStream openPerfFile() throws Exception {
        return openDownloads("t3native_perf_", "t3native performance log: " + Build.MANUFACTURER + " " + Build.MODEL
                + ", Android " + Build.VERSION.RELEASE
                + ", display " + getWindowManager().getDefaultDisplay().getRefreshRate() + " Hz\n"
                + "frame = median/longest interval, jank = intervals over 1.5 x the median, cpu = mean/95%/longest,"
                + " gpu = mean/longest, shadow/scene/post/2d = the GPU per pass (means, every other frame),"
                + " stage = the fight's stage and version, battery in C, headroom 1.0 = throttling\n");
    }

    /** `<prefix><time>.txt` in Downloads (MediaStore, Android 10+; before, the app's files folder), `head` first. */
    private OutputStream openDownloads(String prefix, String head) throws Exception {
        String name = prefix + new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT).format(new Date()) + ".txt";
        OutputStream out;
        if (Build.VERSION.SDK_INT >= 29) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
            Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) return null;
            out = getContentResolver().openOutputStream(uri, "w");
        } else {
            out = new FileOutputStream(new File(getExternalFilesDir(null), name));
        }
        if (out == null) return null;
        out.write(head.getBytes(StandardCharsets.UTF_8));
        return out;
    }

    private OutputStream netOut;
    private final SimpleDateFormat netClock = new SimpleDateFormat("HH:mm:ss.SSS", Locale.ROOT);

    /**
     * NETWORK MODE's log (any thread): each "net:" line of the game and of Wi-Fi Direct, with the
     * time, in t3native_net_<time>.txt in Downloads, made at the first line of a run.
     */
    public void netLine(final String line) {
        final String stamped;
        synchronized (netClock) {   // called from the game thread and from Wi-Fi Direct's
            stamped = netClock.format(new Date()) + " " + line + "\n";
        }
        perfWriter.execute(() -> {
            try {
                if (netOut == null) {
                    netOut = openDownloads("t3native_net_", "t3native network log: " + Build.MANUFACTURER + " " + Build.MODEL
                            + ", Android " + Build.VERSION.RELEASE + ", " + getPackageName() + "\n");
                }
                if (netOut == null) return;
                netOut.write(stamped.getBytes(StandardCharsets.UTF_8));
                netOut.flush();
            } catch (Exception e) {
                netOut = null;   // tried again with the next line
            }
        });
    }

    private String temperatures() {
        StringBuilder s = new StringBuilder();
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int tenths = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE) : Integer.MIN_VALUE;
        s.append("battery=").append(tenths == Integer.MIN_VALUE ? "-" : String.format(Locale.ROOT, "%.1fC", tenths / 10.0));
        if (Build.VERSION.SDK_INT >= 30) {
            PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
            float headroom = power != null ? power.getThermalHeadroom(0) : Float.NaN;
            s.append(" headroom=").append(Float.isNaN(headroom) ? "-" : String.format(Locale.ROOT, "%.2f", headroom));
        }
        return s.toString();
    }

    /** Called from the game thread: the answer once, then null again. */
    public synchronized String pollText() {
        String text = textAnswer;
        textAnswer = null;
        return text;
    }

    /**
     * NETWORK MODE (called from the game thread): the name the other player sees (the one set in
     * the phone's settings, else its model), upper case and plain ASCII for the game's font.
     */
    public String deviceName() {
        String name = null;
        try {
            name = Settings.Global.getString(getContentResolver(), Settings.Global.DEVICE_NAME);
        } catch (RuntimeException e) {
            // Not readable on this phone: the model below.
        }
        if (name == null || name.trim().isEmpty()) name = Build.MODEL;
        // Letters with marks become plain ones (a Polish "URZADZENIE" read "URZDZENIE"): the marks split
        // off and dropped, the L with a stroke (which has none to split) by hand.
        String upper = name.toUpperCase(Locale.ROOT).replace('Ł', 'L');
        upper = java.text.Normalizer.normalize(upper, java.text.Normalizer.Form.NFD);
        StringBuilder s = new StringBuilder();
        for (char c : upper.toCharArray()) {
            // The game's font has no underscore ("sdk_gphone64" drew as "SDK?GPHONE64"): a space.
            if (c == '_') s.append(' ');
            else if (c >= 32 && c < 127) s.append(c);
        }
        String plain = s.toString().trim();
        return plain.length() > 20 ? plain.substring(0, 20) : plain;
    }

    private WifiManager.MulticastLock multicastLock;

    /** A host keeps the Wi-Fi letting broadcasts (the searches for games) in while it waits. */
    public synchronized void netLock(boolean on) {
        try {
            if (on && multicastLock == null) {
                WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
                if (wifi == null) return;
                multicastLock = wifi.createMulticastLock("t3native");
                multicastLock.setReferenceCounted(false);
                multicastLock.acquire();
            } else if (!on && multicastLock != null) {
                multicastLock.release();
                multicastLock = null;
            }
        } catch (RuntimeException e) {
            multicastLock = null;
        }
    }

    /**
     * No local network on entering NETWORK MODE: Android 10+ shows its Wi-Fi panel over the game
     * (an app may not turn Wi-Fi on itself); older versions turn Wi-Fi on.
     */
    @SuppressWarnings("deprecation")
    public void wifiPanel() {
        runOnUiThread(() -> {
            try {
                if (Build.VERSION.SDK_INT >= 29) {
                    startActivity(new Intent(Settings.Panel.ACTION_WIFI));
                } else {
                    WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
                    if (wifi != null) wifi.setWifiEnabled(true);
                }
            } catch (RuntimeException e) {
                // No panel on this phone: the player turns Wi-Fi on from the quick settings.
            }
        });
    }
}
