package com.n64recomp.dora64;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

public final class Dora64Activity extends SDLActivity {
    private static final int PICK_ROM = 64;
    private static final long ROM_SIZE = 8L * 1024L * 1024L;
    private static final String TAG = "Dora64";

    @Override
    protected String[] getLibraries() {
        return BuildConfig.DORA64_RUNTIME
            ? new String[] { "SDL2", "Dora64" }
            : new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (BuildConfig.DORA64_RUNTIME) {
            new File(getFilesDir(), ".assets-ready").delete();
            new File(getFilesDir(), ".rom-selection-canceled").delete();
        }
        super.onCreate(savedInstanceState);
        if (!BuildConfig.DORA64_RUNTIME) {
            return;
        }

        try {
            File assetsRoot = new File(getFilesDir(), "assets");
            copyAssetTree("dora64/assets", assetsRoot);
            File ready = new File(getFilesDir(), ".assets-ready");
            if (!ready.exists() && !ready.createNewFile()) {
                throw new IOException("Cannot mark assets ready");
            }
        } catch (IOException error) {
            Log.e(TAG, "Could not prepare game assets", error);
            markSelectionCanceled();
            new AlertDialog.Builder(this)
                .setTitle("Dora64")
                .setMessage("Could not prepare game assets: " + error.getMessage())
                .setPositiveButton("Close", (dialog, which) -> finish())
                .setCancelable(false)
                .show();
            return;
        }

        File rom = new File(getFilesDir(), "doraemon.n64.jp.z64");
        if (!rom.isFile()) {
            getWindow().getDecorView().post(this::selectRom);
        }
    }

    private void copyAssetTree(String source, File destination) throws IOException {
        String[] children = getAssets().list(source);
        if (children == null) {
            throw new IOException("Cannot list " + source);
        }
        if (children.length == 0) {
            File parent = destination.getParentFile();
            if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
                throw new IOException("Cannot create " + parent);
            }
            try (InputStream input = getAssets().open(source);
                 OutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
            }
            return;
        }
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new IOException("Cannot create " + destination);
        }
        for (String child : children) {
            copyAssetTree(source + "/" + child, new File(destination, child));
        }
    }

    private void selectRom() {
        File canceled = new File(getFilesDir(), ".rom-selection-canceled");
        if (canceled.exists() && !canceled.delete()) {
            Log.w(TAG, "Could not clear ROM selection marker");
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            markSelectionCanceled();
            finish();
            return;
        }

        File rom = new File(getFilesDir(), "doraemon.n64.jp.z64");
        File temporary = new File(getFilesDir(), "doraemon.n64.jp.z64.part");
        try (InputStream input = getContentResolver().openInputStream(data.getData());
             OutputStream output = new FileOutputStream(temporary)) {
            if (input == null) {
                throw new IOException("Cannot open the selected file");
            }
            byte[] buffer = new byte[64 * 1024];
            long total = 0;
            int count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > ROM_SIZE) {
                    throw new IOException("The selected file is larger than 8 MiB");
                }
                output.write(buffer, 0, count);
            }
            if (total != ROM_SIZE) {
                throw new IOException("The selected file must be exactly 8 MiB");
            }
        } catch (IOException error) {
            temporary.delete();
            showRomError(error.getMessage());
            return;
        }
        if (!temporary.renameTo(rom)) {
            temporary.delete();
            showRomError("Cannot save the selected ROM");
        }
    }

    private void showRomError(String message) {
        new AlertDialog.Builder(this)
            .setTitle("Dora64")
            .setMessage(message)
            .setPositiveButton("Choose again", (dialog, which) -> selectRom())
            .setNegativeButton("Close", (dialog, which) -> {
                markSelectionCanceled();
                finish();
            })
            .setCancelable(false)
            .show();
    }

    private void markSelectionCanceled() {
        try {
            new File(getFilesDir(), ".rom-selection-canceled").createNewFile();
        } catch (IOException error) {
            Log.e(TAG, "Could not mark ROM selection canceled", error);
        }
    }
}
