package app.cuelight.tv;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.app.Activity;
import android.app.Instrumentation;
import android.app.UiAutomation;
import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

public final class MobileInstrumentation extends Instrumentation {
    private UiAutomation automation;

    @Override
    public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    private AccessibilityNodeInfo find(AccessibilityNodeInfo node, String label) {
        if (node == null) return null;
        CharSequence description = node.getContentDescription();
        if (description != null && description.toString().equals(label)) return node;
        for (int i = 0; i < node.getChildCount(); i++) {
            AccessibilityNodeInfo result = find(node.getChild(i), label);
            if (result != null) return result;
        }
        return null;
    }

    private AccessibilityNodeInfo waitFor(String label) {
        long until = SystemClock.uptimeMillis() + 10000;
        while (SystemClock.uptimeMillis() < until) {
            AccessibilityNodeInfo node = find(automation.getRootInActiveWindow(), label);
            if (node != null) return node;
            SystemClock.sleep(100);
        }
        throw new AssertionError("Control unavailable: " + label);
    }

    private void click(String label) {
        if (!waitFor(label).performAction(AccessibilityNodeInfo.ACTION_CLICK)) throw new AssertionError("Could not activate: " + label);
        SystemClock.sleep(200);
    }

    private void keyboard() {
        long until = SystemClock.uptimeMillis() + 5000;
        while (SystemClock.uptimeMillis() < until) {
            for (AccessibilityWindowInfo window : automation.getWindows())
                if (window.getType() == AccessibilityWindowInfo.TYPE_INPUT_METHOD) return;
            SystemClock.sleep(100);
        }
        throw new AssertionError("Android keyboard did not open");
    }

    @Override
    public void onStart() {
        Bundle result = new Bundle();
        try {
            automation = getUiAutomation();
            AccessibilityServiceInfo info = automation.getServiceInfo();
            info.flags |= AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
            automation.setServiceInfo(info);
            Intent intent = new Intent(Intent.ACTION_MAIN).setClassName(getTargetContext(), "app.cuelight.tv.CuelightActivity")
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivitySync(intent);
            SystemClock.sleep(1500);
            if (find(automation.getRootInActiveWindow(), "Settings") == null) automation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK);
            waitFor("Home");
            for (int i = 0; i < 20; i++) {
                click("Search"); waitFor("Cuelight, search");
                click("Settings"); waitFor("Cuelight, settings");
                click("Home"); waitFor("Cuelight, home");
            }
            click("Settings"); click("Find a setting"); keyboard();
            automation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK);
            SystemClock.sleep(300);
            if (find(automation.getRootInActiveWindow(), "Cancel") != null) click("Cancel");
            result.putString("stream", "Mobile navigation, accessibility actions, and Android keyboard: passed\n");
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("stream", "Mobile regression failed: " + failure.getMessage() + "\n");
            finish(Activity.RESULT_CANCELED, result);
        }
    }
}
