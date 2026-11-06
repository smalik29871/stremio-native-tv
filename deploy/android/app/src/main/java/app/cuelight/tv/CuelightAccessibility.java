package app.cuelight.tv;

import android.content.Context;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

final class CuelightAccessibility extends View {
    private static final class Node {
        int id;
        String label;
        Rect bounds;
        boolean longClick;
        boolean clickable;
    }

    private final AccessibilityManager manager;
    private List<Node> nodes = new ArrayList<>();
    private long version;
    private String screen = "";
    private int focused = View.NO_ID, hovered = View.NO_ID;
    private final AccessibilityManager.AccessibilityStateChangeListener stateListener = enabled -> CuelightActivity.nativeAccessibilityAction(0, -1, 0);

    CuelightAccessibility(Context context) {
        super(context);
        manager = (AccessibilityManager) context.getSystemService(Context.ACCESSIBILITY_SERVICE);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setWillNotDraw(true);
        if (manager != null) manager.addAccessibilityStateChangeListener(stateListener);
    }

    boolean enabled() { return manager != null && manager.isEnabled(); }

    @Override
    protected void onDetachedFromWindow() {
        if (manager != null) manager.removeAccessibilityStateChangeListener(stateListener);
        super.onDetachedFromWindow();
    }

    void update(byte[] bytes, long generation, String page) {
        if (!enabled() || generation < version) return;
        List<Node> next = new ArrayList<>();
        try {
            JSONArray array = new JSONArray(new String(bytes, StandardCharsets.UTF_8));
            for (int i = 0; i < array.length(); i++) {
                JSONObject value = array.getJSONObject(i);
                JSONArray bounds = value.getJSONArray("bounds");
                Node node = new Node();
                node.id = value.getInt("id"); node.label = value.getString("label");
                node.bounds = new Rect(bounds.getInt(0), bounds.getInt(1), bounds.getInt(2), bounds.getInt(3));
                node.longClick = value.optBoolean("longClick");
                node.clickable = value.optBoolean("clickable", true);
                next.add(node);
            }
        } catch (JSONException ignored) { return; }
        boolean changed = !screen.equals(page);
        if (changed) {
            if (hovered != View.NO_ID) event(hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
            if (focused != View.NO_ID) event(focused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
            hovered = View.NO_ID;
        }
        int nativeFocus = !changed && focused > 0 && focused <= nodes.size() ? nodes.get(focused - 1).id : View.NO_ID;
        screen = page;
        nodes = next; version = generation; focused = View.NO_ID;
        for (int i = 0; i < nodes.size(); i++) if (nodes.get(i).id == nativeFocus) { focused = i + 1; break; }
        sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
    }

    private void event(int id, int type) {
        if (!enabled() || getParent() == null) return;
        AccessibilityEvent event = AccessibilityEvent.obtain(type);
        event.setPackageName(getContext().getPackageName());
        event.setClassName("android.widget.Button");
        event.setSource(this, id);
        if (id > 0 && id <= nodes.size()) event.getText().add(nodes.get(id - 1).label);
        getParent().requestSendAccessibilityEvent(this, event);
    }

    @Override
    public boolean dispatchHoverEvent(MotionEvent event) {
        if (manager == null || !manager.isTouchExplorationEnabled()) return super.dispatchHoverEvent(event);
        int next = View.NO_ID;
        if (event.getAction() != MotionEvent.ACTION_HOVER_EXIT)
            for (int i = nodes.size() - 1; i >= 0; i--) if (nodes.get(i).bounds.contains((int) event.getX(), (int) event.getY())) { next = i + 1; break; }
        if (next != hovered) {
            if (next != View.NO_ID) event(next, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
            if (hovered != View.NO_ID) event(hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
            hovered = next;
        }
        return next != View.NO_ID;
    }

    private final AccessibilityNodeProvider provider = new AccessibilityNodeProvider() {
        @Override
        public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
            if (id == View.NO_ID) {
                AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(CuelightAccessibility.this);
                onInitializeAccessibilityNodeInfo(info);
                info.setClassName("android.view.ViewGroup");
                info.setContentDescription("Cuelight, " + screen);
                info.setScrollable(true);
                info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
                info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
                for (int i = 0; i < nodes.size(); i++) info.addChild(CuelightAccessibility.this, i + 1);
                return info;
            }
            if (id < 1 || id > nodes.size()) return null;
            Node node = nodes.get(id - 1);
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
            info.setSource(CuelightAccessibility.this, id);
            info.setParent(CuelightAccessibility.this);
            info.setPackageName(getContext().getPackageName());
            info.setClassName(node.clickable ? "android.widget.Button" : "android.widget.TextView");
            info.setContentDescription(node.label);
            info.setBoundsInParent(node.bounds);
            int[] location = new int[2]; getLocationOnScreen(location);
            Rect screen = new Rect(node.bounds); screen.offset(location[0], location[1]);
            info.setBoundsInScreen(screen);
            info.setEnabled(true); info.setVisibleToUser(isShown()); info.setFocusable(true); info.setClickable(node.clickable);
            info.setAccessibilityFocused(focused == id);
            info.addAction(focused == id ? AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS : AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
            if (node.clickable) info.addAction(AccessibilityNodeInfo.ACTION_CLICK);
            if (node.longClick) { info.setLongClickable(true); info.addAction(AccessibilityNodeInfo.ACTION_LONG_CLICK); }
            return info;
        }

        @Override
        public AccessibilityNodeInfo findFocus(int focus) { return focused == View.NO_ID ? null : createAccessibilityNodeInfo(focused); }

        @Override
        public boolean performAction(int id, int action, Bundle arguments) {
            if (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD || action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) {
                CuelightActivity.nativeAccessibilityAction(version, -1, action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ? 3 : 4);
                return true;
            }
            if (id < 1 || id > nodes.size()) return false;
            if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
                if (focused == id) return false;
                if (focused != View.NO_ID) event(focused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                focused = id; event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED); return true;
            }
            if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
                if (focused != id) return false;
                focused = View.NO_ID; event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED); return true;
            }
            if (action == AccessibilityNodeInfo.ACTION_CLICK || action == AccessibilityNodeInfo.ACTION_LONG_CLICK) {
                if (!nodes.get(id - 1).clickable) return false;
                CuelightActivity.nativeAccessibilityAction(version, nodes.get(id - 1).id, action == AccessibilityNodeInfo.ACTION_CLICK ? 1 : 2);
                event(id, AccessibilityEvent.TYPE_VIEW_CLICKED); return true;
            }
            return false;
        }
    };

    @Override
    public AccessibilityNodeProvider getAccessibilityNodeProvider() { return provider; }
}
