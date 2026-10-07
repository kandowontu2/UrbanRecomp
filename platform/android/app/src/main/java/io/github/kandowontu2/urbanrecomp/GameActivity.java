package io.github.kandowontu2.urbanrecomp;
import org.libsdl.app.SDLActivity;
public final class GameActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "SDL3", "main" }; }
    @Override protected String[] getArguments() { return new String[] { "--fullscreen" }; }
}
