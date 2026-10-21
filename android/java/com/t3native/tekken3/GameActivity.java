package com.t3native.tekken3;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.os.Build;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.text.InputFilter;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;

/**
 * The game's activity: Android's NativeActivity (the game itself is libt3game.so) with the system
 * bars hidden while it is in front, as a full-screen game shows them only on a swipe from the edge.
 * Hiding them needs the UI thread, which the native code does not run on.
 *
 * It also asks for a line of text (a touch button's name): a dialog with the system keyboard. The
 * game thread calls askText and then pollText each frame until the answer is there.
 */
public class GameActivity extends NativeActivity {
    private static final int MAX_TEXT = 12;
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

    /** Called from the game thread: shows the dialog on the UI thread. */
    public void askText(final String title, final String initial) {
        synchronized (this) {
            textAnswer = null;
        }
        runOnUiThread(() -> {
            final EditText edit = new EditText(this);
            edit.setSingleLine(true);
            edit.setFilters(new InputFilter[] {new InputFilter.LengthFilter(MAX_TEXT)});
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

    /** Called from the game thread: the answer once, then null again. */
    public synchronized String pollText() {
        String text = textAnswer;
        textAnswer = null;
        return text;
    }
}
