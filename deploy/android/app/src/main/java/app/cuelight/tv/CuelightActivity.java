package app.cuelight.tv;

import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.WindowInsets;
import android.system.ErrnoException;
import android.system.Os;

import org.libsdl.app.SDLActivity;

/* SDL's activity runs the C app (libmain.so, SDL_main in src/main.c); the video plays
 * under it (CuelightPlayer). */
public class CuelightActivity extends SDLActivity {
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // a phone (its short side under 600 dp): the app draws larger (src/ui/gfx.c)
        try {
            if (getResources().getConfiguration().smallestScreenWidthDp < 600) Os.setenv("SNTV_HANDHELD", "1", true);
            Os.setenv("SNTV_DENSITY", Float.toString(getResources().getDisplayMetrics().density), true);   // how big a dp is: the upright layouts size their text by it
        } catch (ErrnoException ignored) { }
        super.onCreate(savedInstanceState);
        if (mLayout != null && mSurface != null) CuelightPlayer.attach(this, mLayout, mSurface);
        if (getResources().getConfiguration().smallestScreenWidthDp < 600 && Build.VERSION.SDK_INT >= 21) {   // a phone keeps its bars: dark, like the app
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

    /* Android 15 draws an app under its status and navigation bars (edge to edge): the app's
     * view keeps clear of them by their size, so nothing of it hides behind them. The player
     * hides the bars, and the view then has the whole screen again, a camera cutout included
     * (it kept a strip clear for it, which showed beside the picture). */
    @SuppressWarnings("deprecation")
    private void keepBetweenBars() {
        if (mLayout == null || Build.VERSION.SDK_INT < 20) return;
        mLayout.setOnApplyWindowInsetsListener((v, insets) -> {
            int l, t, r, b;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                l = i.left; t = i.top; r = i.right; b = i.bottom;
            } else {
                l = insets.getSystemWindowInsetLeft(); t = insets.getSystemWindowInsetTop();
                r = insets.getSystemWindowInsetRight(); b = insets.getSystemWindowInsetBottom();
            }
            if (CuelightPlayer.fullScreen()) l = t = r = b = 0;
            if (v.getPaddingLeft() != l || v.getPaddingTop() != t || v.getPaddingRight() != r || v.getPaddingBottom() != b) v.setPadding(l, t, r, b);
            return insets;
        });
        mLayout.requestApplyInsets();
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
