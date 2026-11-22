package org.libsdl.app;

import android.content.Intent;

/**
 * SDL activity with app-specific additions (camera capture results).
 */
public class MainActivity extends SDLActivity {
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

    /** Implemented in libmain.so (src/camera.cc). */
    public static native void nativeCameraResult(boolean ok, String path);
}
