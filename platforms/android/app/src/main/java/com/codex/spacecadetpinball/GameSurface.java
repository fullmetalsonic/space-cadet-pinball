package com.codex.spacecadetpinball;

import android.content.Context;
import org.libsdl.app.SDLSurface;

/** Keeps SDL touch, keyboard and display rotation while disabling unused motion sensors. */
public final class GameSurface extends SDLSurface {
    public GameSurface(Context context) { super(context); }

    @Override public void enableSensor(int sensorType, boolean enabled) {
        // SDL invokes this on first start and resume. This game accepts button/key nudges only.
    }
}
