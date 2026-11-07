package app.cuelight.tv;

import android.app.UiModeManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.WindowInsets;
import android.system.ErrnoException;
import android.system.Os;

import org.libsdl.app.SDLActivity;

/* SDL's activity runs the C app (libmain.so, SDL_main in src/main.c); the video plays
 * under it (CuelightPlayer). */
public class CuelightActivity extends SDLActivity {
    private static volatile CuelightAccessibility sAccessibility;
    static native void nativeAccessibilityAction(long version, int id, int action);
    public static boolean accessibilityEnabled() { return sAccessibility != null && sAccessibility.enabled(); }
    public static void accessibilityUpdate(byte[] nodes, long version, String screen) {
        CuelightAccessibility view = sAccessibility;
        if (view != null) view.post(() -> { if (view == sAccessibility) view.update(nodes, version, screen); });
    }
    /* a short buzz for the C app (src/ui/accessibility.c ui_haptic): the system's own, so it follows the
     * phone's touch-feedback setting */
    public static void haptic(int kind) {
        final SDLActivity a = mSingleton;
        if (a == null) return;
        a.runOnUiThread(() -> {
            View v = a.getWindow() != null ? a.getWindow().getDecorView() : null;
            if (v == null) return;
            int c = kind == 0 ? HapticFeedbackConstants.LONG_PRESS : kind == 1 ? HapticFeedbackConstants.CLOCK_TICK
                  : android.os.Build.VERSION.SDK_INT >= 30 ? HapticFeedbackConstants.CONFIRM : HapticFeedbackConstants.VIRTUAL_KEY;
            v.performHapticFeedback(c);
        });
    }
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /* Android's word on memory: the C app's memory watch steps down (src/memwatch.c). */
    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        try { nativeTrimMemory(level); } catch (UnsatisfiedLinkError ignored) { }   // before the app's library is up
    }
    private static native void nativeTrimMemory(int level);

    private boolean tv() {
        UiModeManager ui = (UiModeManager) getSystemService(UI_MODE_SERVICE);
        PackageManager pm = getPackageManager();
        return (ui != null && ui.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION)
            || pm.hasSystemFeature(PackageManager.FEATURE_LEANBACK) || pm.hasSystemFeature("amazon.hardware.fire_tv");
    }

    static boolean sPhone;
    static boolean sHandheld;
    private static native void nativeWindowMetrics(float density, float fontScale, int kind);

    private void updateMetrics() {
        sHandheld = !tv();
        sPhone = phone();
        try { nativeWindowMetrics(getResources().getDisplayMetrics().density, getResources().getConfiguration().fontScale, sHandheld ? sPhone ? 1 : 2 : 0); }
        catch (UnsatisfiedLinkError ignored) { }
    }
    private boolean phone() { return !tv() && getResources().getConfiguration().smallestScreenWidthDp < 600; }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // a phone (its short side under 600 dp): the app draws larger (src/ui/gfx.c). Never a TV:
        // a 1080p Android TV is 540 dp across too
        sPhone = phone();
        try {
            if (!tv()) Os.setenv("SNTV_HANDHELD", sPhone ? "1" : "2", true);
            Os.setenv("SNTV_DENSITY", Float.toString(getResources().getDisplayMetrics().density), true);   // how big a dp is: the upright layouts size their text by it
            Os.setenv("SNTV_FONT_SCALE", Float.toString(getResources().getConfiguration().fontScale), true);
        } catch (ErrnoException ignored) { }
        super.onCreate(savedInstanceState);
        updateMetrics();
        if (mLayout != null && mSurface != null) CuelightPlayer.attach(this, mLayout, mSurface);
        if (mLayout != null && mSurface != null && sHandheld) {
            sAccessibility = new CuelightAccessibility(this);
            mSurface.setImportantForAccessibility(android.view.View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            mLayout.addView(sAccessibility, new android.widget.RelativeLayout.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        }
        if (sHandheld && Build.VERSION.SDK_INT >= 21) {
            getWindow().setStatusBarColor(Color.BLACK);
            getWindow().setNavigationBarColor(Color.BLACK);
            getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.BLACK));   // what shows past the app's view: black, not the theme's
            if (Build.VERSION.SDK_INT >= 28) {   // the player may use the screen past a camera cutout, as video apps do
                android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
                lp.layoutInDisplayCutoutMode = android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                getWindow().setAttributes(lp);
            }
            keepBetweenBars();
        }
        CuelightPlayer.offerUrl(getIntent() != null ? getIntent().getDataString() : null);
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        updateMetrics();
        if (mLayout != null) mLayout.requestApplyInsets();
    }

    @Override
    protected void onDestroy() {
        if (sAccessibility != null && sAccessibility.getContext() == this) sAccessibility = null;
        try { super.onDestroy(); }
        finally { CuelightPlayer.detach(this); }
    }

    /* Android 15 forces edge-to-edge windows. Keep controls clear of system bars and cutouts. */
    @SuppressWarnings("deprecation")
    private void keepBetweenBars() {
        if (mLayout == null || Build.VERSION.SDK_INT < 20) return;
        mLayout.setOnApplyWindowInsetsListener((v, insets) -> {
            int l, t, r, b;
            if (Build.VERSION.SDK_INT >= 30) {
                int types = WindowInsets.Type.displayCutout() | (CuelightPlayer.fullScreen() ? 0 : WindowInsets.Type.systemBars());
                android.graphics.Insets i = insets.getInsets(types);
                l = i.left; t = i.top; r = i.right; b = i.bottom;
            } else {
                l = insets.getSystemWindowInsetLeft(); t = insets.getSystemWindowInsetTop();
                r = insets.getSystemWindowInsetRight(); b = insets.getSystemWindowInsetBottom();
            }
            if (CuelightPlayer.fullScreen() && Build.VERSION.SDK_INT < 30) {
                l = t = r = b = 0;
                if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                    android.view.DisplayCutout c = insets.getDisplayCutout();
                    l = c.getSafeInsetLeft(); t = c.getSafeInsetTop(); r = c.getSafeInsetRight(); b = c.getSafeInsetBottom();
                }
            }
            if (v.getPaddingLeft() != l || v.getPaddingTop() != t || v.getPaddingRight() != r || v.getPaddingBottom() != b) v.setPadding(l, t, r, b);
            return insets;
        });
        mLayout.requestApplyInsets();
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        CuelightPlayer.onPicked(code, result, data);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        CuelightPlayer.offerUrl(intent != null ? intent.getDataString() : null);
    }

    @Override
    protected void onUserLeaveHint() {
        super.onUserLeaveHint();
        CuelightPlayer.onUserLeaving();   /* Home while a video plays: it goes along in a small window */
    }

    @Override
    public void onPictureInPictureModeChanged(boolean in, Configuration c) {
        super.onPictureInPictureModeChanged(in, c);
        CuelightPlayer.onPipChanged(in);
    }
}
