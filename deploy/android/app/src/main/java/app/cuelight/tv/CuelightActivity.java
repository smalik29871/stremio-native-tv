package app.cuelight.tv;

import org.libsdl.app.SDLActivity;

/* SDL's activity runs the C app (libmain.so, SDL_main in src/main.c). */
public class CuelightActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
