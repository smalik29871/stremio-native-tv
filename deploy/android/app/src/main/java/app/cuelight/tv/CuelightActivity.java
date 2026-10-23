package app.cuelight.tv;

import android.os.Bundle;
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // a phone (its short side under 600 dp): the app draws larger (src/ui/gfx.c)
        try {
            if (getResources().getConfiguration().smallestScreenWidthDp < 600) Os.setenv("SNTV_HANDHELD", "1", true);
        } catch (ErrnoException ignored) { }
        super.onCreate(savedInstanceState);
        if (mLayout != null && mSurface != null) CuelightPlayer.attach(this, mLayout, mSurface);
    }
}
