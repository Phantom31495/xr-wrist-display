package com.zachery.xrwrist.quest;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/**
 * Auto-launches XR Wrist Display when the headset finishes booting, so the
 * wrist display is available without manually opening the app.
 * Declared in AndroidManifest.xml with RECEIVE_BOOT_COMPLETED.
 */
public class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "XRWrist";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            Log.i(TAG, "boot completed; launching XR Wrist Display");
            Intent launch = context.getPackageManager()
                    .getLaunchIntentForPackage(context.getPackageName());
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(launch);
            } else {
                Log.w(TAG, "boot: no launch intent for package");
            }
        }
    }
}
