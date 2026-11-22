package org.libsdl.app;

import android.content.Intent;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;

/**
 * SDL activity with app-specific additions (camera capture results and IME
 * height reporting).
 */
public class MainActivity extends SDLActivity {
    private int lastImeInset = -1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        //Report the on-screen keyboard height so the native UI can move the
        //composer above it. WindowInsets is the reliable source on Android 11+;
        //older releases fall back to comparing the visible frame.
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            decor.setOnApplyWindowInsetsListener((v, insets) -> {
                reportImeInset(v, insets);
                //Return unchanged so SDLSurface still receives its insets.
                return insets;
            });
        } else {
            decor.getViewTreeObserver().addOnGlobalLayoutListener(
                () -> reportImeInset(decor, null));
        }
        decor.requestApplyInsets();
    }

    private void reportImeInset(View decor, WindowInsets insets) {
        int bottom = 0;
        if (Build.VERSION.SDK_INT >= 30) {
            if (insets == null) {
                insets = decor.getRootWindowInsets();
            }
            if (insets != null) {
                bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
            }
        } else {
            Rect visible = new Rect();
            decor.getWindowVisibleDisplayFrame(visible);
            bottom = Math.max(0, decor.getRootView().getHeight() - visible.bottom);
            //Ignore small differences caused by system bars.
            if (bottom < decor.getRootView().getHeight() / 6) {
                bottom = 0;
            }
        }

        if (bottom != lastImeInset) {
            lastImeInset = bottom;
            nativeImeInset(bottom);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (CameraCapture.handleResult(requestCode, resultCode)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    /** Called from native code through JNI. */
    public void launchCamera() {
        runOnUiThread(() -> CameraCapture.launch(this));
    }

    /** Implemented in libmain.so (src/ime.cc). */
    public static native void nativeImeInset(int px);

    /** Implemented in libmain.so (src/camera.cc). */
    public static native void nativeCameraResult(boolean ok, String path);
}
