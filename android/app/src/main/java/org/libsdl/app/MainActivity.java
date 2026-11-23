package org.libsdl.app;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * SDL activity with app-specific additions (camera capture results, document
 * picking and IME height reporting).
 */
public class MainActivity extends SDLActivity {
    private static final int REQUEST_PICK_FILES = 0x5345;
    private static final long MAX_PICK_BYTES = 16L * 1024 * 1024;
    private static final int MAX_PICK_COUNT = 8;

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

    @Override
    protected void onResume() {
        super.onResume();
        //Keep the mode stable across window recreation.
        getWindow().setSoftInputMode(
            Build.VERSION.SDK_INT >= 30
                ? WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                : WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }

    private void reportImeInset(WindowInsets insets) {
        int bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
        if (bottom != lastImeInset) {
            lastImeInset = bottom;
            nativeImeInset(bottom);
        }
    }

    /** Called from native code through JNI. */
    public void launchFilePicker() {
        runOnUiThread(this::pickFiles);
    }

    private void pickFiles() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_PICK_FILES);
        } catch (Exception e) {
            nativeFilesPicked(new String[0]);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_PICK_FILES) {
            handlePickedFiles(resultCode, data);
            return;
        }
        if (CameraCapture.handleResult(requestCode, resultCode)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void handlePickedFiles(int resultCode, Intent data) {
        List<Uri> uris = new ArrayList<>();

        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                int count = Math.min(data.getClipData().getItemCount(), MAX_PICK_COUNT);
                for (int i = 0; i < count; ++i)
                    uris.add(data.getClipData().getItemAt(i).getUri());
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }

        nativeFilesPicked(copyPickedFiles(uris));
    }

    private String[] copyPickedFiles(List<Uri> uris) {
        List<String> out = new ArrayList<>();
        if (uris.isEmpty())
            return new String[0];

        File dir = new File(getCacheDir(), "picked");
        if (!dir.exists() && !dir.mkdirs())
            return new String[0];

        for (int i = 0; i < uris.size(); ++i) {
            Uri uri = uris.get(i);
            String name = displayName(uri);

            if (name.lastIndexOf('.') <= 0) {
                String mime = getContentResolver().getType(uri);
                if (mime != null) {
                    String ext = MimeTypeMap.getSingleton()
                        .getExtensionFromMimeType(mime);
                    if (ext != null && !ext.isEmpty())
                        name += "." + ext;
                }
            }

            name = name.replace('/', '_').replace('\\', '_');
            if (name.length() > 96)
                name = name.substring(name.length() - 96);

            File target = new File(dir,
                System.currentTimeMillis() + "-" + i + "-" + name);

            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream outStream = new FileOutputStream(target)) {
                if (in == null) {
                    target.delete();
                    continue;
                }

                byte[] buffer = new byte[64 * 1024];
                long total = 0;
                int read;
                boolean tooBig = false;

                while ((read = in.read(buffer)) > 0) {
                    total += read;
                    if (total > MAX_PICK_BYTES) {
                        tooBig = true;
                        break;
                    }
                    outStream.write(buffer, 0, read);
                }

                if (tooBig || target.length() <= 0) {
                    target.delete();
                    continue;
                }

                out.add(target.getAbsolutePath());
            } catch (Exception e) {
                target.delete();
            }
        }

        return out.toArray(new String[0]);
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                 new String[]{OpenableColumns.DISPLAY_NAME},
                 null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String name = cursor.getString(index);
                    if (name != null && !name.isEmpty())
                        return name;
                }
            }
        } catch (Exception ignored) {
        }

        String last = uri.getLastPathSegment();
        return (last == null || last.isEmpty()) ? "file" : last;
    }

    /** Implemented in libmain.so (src/ime.cc). */
    public static native void nativeImeInset(int px);

    /** Implemented in libmain.so (src/filepick.cc). */
    public static native void nativeFilesPicked(String[] paths);

    /** Called from native code through JNI. */
    public void launchCamera() {
        runOnUiThread(() -> CameraCapture.launch(this));
    }

    /** Implemented in libmain.so (src/camera.cc). */
    public static native void nativeCameraResult(boolean ok, String path);
}
