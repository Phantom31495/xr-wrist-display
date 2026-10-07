package com.zachery.xrwrist.quest;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.util.Log;

import java.util.ArrayList;
import java.util.Locale;

/**
 * v0.4.0: push-to-talk voice control for the wrist display.
 * The native side calls startListening(); results are queued and drained
 * via pollResult(). One-shot recognition per call -- no always-on mic.
 */
public class VoiceHelper {
    private static final String TAG = "XRWristVoice";

    private final Activity activity;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private SpeechRecognizer recognizer;
    private volatile String pendingResult = null;
    private volatile boolean listening = false;

    public VoiceHelper(Activity activity) {
        this.activity = activity;
    }

    public synchronized void startListening() {
        if (listening) return;
        if (!SpeechRecognizer.isRecognitionAvailable(activity)) {
            Log.w(TAG, "speech recognition not available on device");
            return;
        }
        pendingResult = null;
        try {
            if (recognizer != null) {
                try { recognizer.destroy(); } catch (Exception ignored) {}
            }
            recognizer = SpeechRecognizer.createSpeechRecognizer(activity);
            recognizer.setRecognitionListener(new RecognitionListener() {
                @Override public void onReadyForSpeech(Bundle p) {}
                @Override public void onBeginningOfSpeech() {}
                @Override public void onRmsChanged(float v) {}
                @Override public void onBufferReceived(byte[] b) {}
                @Override public void onEndOfSpeech() {}
                @Override public void onPartialResults(Bundle r) {}
                @Override public void onEvent(int e, Bundle b) {}

                @Override public void onError(int error) {
                    Log.w(TAG, "recognition error: " + error);
                    listening = false;
                }

                @Override public void onResults(Bundle results) {
                    ArrayList<String> list =
                        results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                    if (list != null && !list.isEmpty()) {
                        pendingResult = list.get(0);
                        Log.i(TAG, "heard: " + pendingResult);
                    }
                    listening = false;
                }
            });
            Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                    RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, Locale.getDefault());
            intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 3);
            intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
            // Short, command-like utterances.
            intent.putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 1200);
            recognizer.startListening(intent);
            listening = true;
            Log.i(TAG, "listening started");
        } catch (Exception e) {
            Log.w(TAG, "startListening failed: " + e);
            listening = false;
        }
    }

    public synchronized boolean isListening() {
        return listening;
    }

    /** Returns and clears the latest result, or null. */
    public synchronized String pollResult() {
        String r = pendingResult;
        pendingResult = null;
        return r;
    }
}
