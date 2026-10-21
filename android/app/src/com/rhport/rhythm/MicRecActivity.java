package com.rhport.rhythm;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * 调试用麦克风录音 Activity：给「另一台设备放音、本机录音」做客观音频验证。
 * adb 启动：am start -n com.rhport.rhythm/.MicRecActivity --ei seconds 20 --es out /sdcard/.../mic.wav
 * 需要 RECORD_AUDIO 权限（debug 包可 adb shell pm grant com.rhport.rhythm android.permission.RECORD_AUDIO）。
 */
public final class MicRecActivity extends Activity {
    private static final String TAG = "RHPORT-MIC";
    private static final int RATE = 48000;
    private static final int REQUEST = 42;

    private int seconds;
    private String out;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        seconds = getIntent().getIntExtra("seconds", 20);
        out = getIntent().getStringExtra("out");
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, REQUEST);
            return;
        }
        startRecording();
    }

    @Override
    public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == REQUEST && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
            startRecording();
        } else {
            Log.e(TAG, "MICREC permission denied");
            finish();
        }
    }

    private void startRecording() {
        final int total = Math.max(1, seconds) * RATE * 2;
        new Thread(() -> {
            int min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            int buffer = Math.max(min, RATE / 2);
            AudioRecord record = null;
            try {
                record = new AudioRecord(MediaRecorder.AudioSource.DEFAULT, RATE, AudioFormat.CHANNEL_IN_MONO,
                        AudioFormat.ENCODING_PCM_16BIT, buffer);
                if (record.getState() != AudioRecord.STATE_INITIALIZED) {
                    Log.e(TAG, "MICREC init failed");
                    finish();
                    return;
                }
                ByteArrayOutputStream pcm = new ByteArrayOutputStream(total);
                record.startRecording();
                Log.i(TAG, "MICREC start seconds=" + seconds + " out=" + out);
                short[] block = new short[2048];
                long written = 0;
                while (written < total) {
                    int n = record.read(block, 0, block.length);
                    if (n <= 0) continue;
                    byte[] bytes = new byte[n * 2];
                    for (int i = 0; i < n; i++) {
                        bytes[2 * i] = (byte) (block[i] & 0xff);
                        bytes[2 * i + 1] = (byte) ((block[i] >> 8) & 0xff);
                    }
                    pcm.write(bytes, 0, bytes.length);
                    written += bytes.length;
                }
                record.stop();
                record.release();
                record = null;
                writeWav(pcm.toByteArray());
                Log.i(TAG, "MICREC stop bytes=" + written);
            } catch (Exception e) {
                Log.e(TAG, "MICREC failed: " + e);
            } finally {
                if (record != null) record.release();
            }
            runOnUiThread(this::finish);
        }).start();
    }

    private void writeWav(byte[] pcm) throws IOException {
        File file = new File(out != null ? out : "/sdcard/Android/media/com.rhport.rhythm/mic.wav");
        File parent = file.getParentFile();
        if (parent != null) parent.mkdirs();
        int dataLen = pcm.length;
        int riffLen = 36 + dataLen;
        byte[] header = new byte[44];
        header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
        put32(header, 4, riffLen);
        header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
        header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
        put32(header, 16, 16);
        put16(header, 20, 1);              /* PCM */
        put16(header, 22, 1);              /* mono */
        put32(header, 24, RATE);
        put32(header, 28, RATE * 2);
        put16(header, 32, 2);
        put16(header, 34, 16);
        header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
        put32(header, 40, dataLen);
        try (FileOutputStream stream = new FileOutputStream(file)) {
            stream.write(header);
            stream.write(pcm);
        }
        Log.i(TAG, "MICREC wrote " + file + " bytes=" + (dataLen + 44));
    }

    private static void put32(byte[] b, int at, int value) {
        b[at] = (byte) (value & 0xff);
        b[at + 1] = (byte) ((value >> 8) & 0xff);
        b[at + 2] = (byte) ((value >> 16) & 0xff);
        b[at + 3] = (byte) ((value >> 24) & 0xff);
    }

    private static void put16(byte[] b, int at, int value) {
        b[at] = (byte) (value & 0xff);
        b[at + 1] = (byte) ((value >> 8) & 0xff);
    }
}
