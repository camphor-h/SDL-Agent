package org.libsdl.app;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

/**
 * SDL activity with app-specific additions (camera capture results and IME
 * height reporting).
 */
public class MainActivity extends SDLActivity {
    private int lastImeInset = -1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (Build.VERSION.SDK_INT >= 30) {
            //The native layout shrinks itself using the IME inset. Keep
            //Android from panning or resizing the window too, otherwise the
            //composer moves up by roughly two keyboard heights.
            getWindow().setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);

            View decor = getWindow().getDecorView();
            decor.setOnApplyWindowInsetsListener((v, insets) -> {
                reportImeInset(insets);
                //Return unchanged so SDLSurface still receives its insets.
                return insets;
            });
            decor.requestApplyInsets();
        }
        //API < 30: the manifest's adjustResize shrinks the window and SDL
        //reflows on the resulting resize event.
    }

    private void reportImeInset(WindowInsets insets) {
        int bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
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
