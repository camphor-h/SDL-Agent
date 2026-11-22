package org.libsdl.app;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.net.Uri;
import android.provider.MediaStore;

import androidx.core.content.FileProvider;
import androidx.exifinterface.media.ExifInterface;

import java.io.File;
import java.io.FileOutputStream;

/** Launches the system camera and reports the captured file. */
final class CameraCapture {
    private static final int REQUEST_CODE = 0x5344;
    private static final int MAX_DIMENSION = 2048;

    private static File pendingFile;

    private CameraCapture() {
    }

    static void launch(Activity activity) {
        try {
            File dir = new File(activity.getCacheDir(), "camera");
            if (!dir.exists() && !dir.mkdirs()) {
                MainActivity.nativeCameraResult(false, "");
                return;
            }

            File file = new File(dir, "photo-" + System.currentTimeMillis() + ".jpg");
            Uri uri = FileProvider.getUriForFile(activity,
                    activity.getPackageName() + ".fileprovider", file);

            pendingFile = file;

            Intent intent = new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
            intent.putExtra(MediaStore.EXTRA_OUTPUT, uri);
            intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);
            activity.startActivityForResult(intent, REQUEST_CODE);
        } catch (Exception e) {
            pendingFile = null;
            MainActivity.nativeCameraResult(false, "");
        }
    }

    static boolean handleResult(int requestCode, int resultCode) {
        if (requestCode != REQUEST_CODE) {
            return false;
        }

        File file = pendingFile;
        pendingFile = null;

        boolean ok = resultCode == Activity.RESULT_OK &&
                     file != null && file.exists() && file.length() > 0;
        if (ok) {
            file = compress(file);
            ok = file != null && file.exists() && file.length() > 0;
        }

        MainActivity.nativeCameraResult(ok, ok ? file.getAbsolutePath() : "");
        return true;
    }

    //Camera photos are often larger than the attachment limit and may carry
    //EXIF rotation. Re-encode at a bounded size with the rotation applied.
    private static File compress(File input) {
        try {
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(input.getAbsolutePath(), bounds);

            int sample = 1;
            while (bounds.outWidth / (sample * 2) >= MAX_DIMENSION &&
                   bounds.outHeight / (sample * 2) >= MAX_DIMENSION) {
                sample *= 2;
            }

            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inSampleSize = sample;

            Bitmap bitmap = BitmapFactory.decodeFile(input.getAbsolutePath(), options);
            if (bitmap == null) {
                return input;
            }

            float scale = Math.min(1.0f,
                    (float) MAX_DIMENSION /
                    (float) Math.max(bitmap.getWidth(), bitmap.getHeight()));
            if (scale < 1.0f) {
                bitmap = Bitmap.createScaledBitmap(bitmap,
                        Math.round(bitmap.getWidth() * scale),
                        Math.round(bitmap.getHeight() * scale), true);
            }

            bitmap = applyExifRotation(input, bitmap);

            File output = new File(input.getParentFile(),
                    input.getName().replace(".jpg", "-s.jpg"));
            try (FileOutputStream out = new FileOutputStream(output)) {
                bitmap.compress(Bitmap.CompressFormat.JPEG, 85, out);
            }
            bitmap.recycle();

            if (output.length() > 0 && output.length() < input.length()) {
                input.delete();
                return output;
            }
            output.delete();
            return input;
        } catch (Exception e) {
            return input;
        }
    }

    private static Bitmap applyExifRotation(File file, Bitmap bitmap) {
        try {
            ExifInterface exif = new ExifInterface(file.getAbsolutePath());
            int orientation = exif.getAttributeInt(
                    ExifInterface.TAG_ORIENTATION,
                    ExifInterface.ORIENTATION_NORMAL);

            int degrees;
            switch (orientation) {
                case ExifInterface.ORIENTATION_ROTATE_90:
                    degrees = 90;
                    break;
                case ExifInterface.ORIENTATION_ROTATE_180:
                    degrees = 180;
                    break;
                case ExifInterface.ORIENTATION_ROTATE_270:
                    degrees = 270;
                    break;
                default:
                    return bitmap;
            }

            Matrix matrix = new Matrix();
            matrix.postRotate(degrees);
            Bitmap rotated = Bitmap.createBitmap(bitmap, 0, 0,
                    bitmap.getWidth(), bitmap.getHeight(), matrix, true);
            if (rotated != bitmap) {
                bitmap.recycle();
            }
            return rotated;
        } catch (Exception e) {
            return bitmap;
        }
    }
}
