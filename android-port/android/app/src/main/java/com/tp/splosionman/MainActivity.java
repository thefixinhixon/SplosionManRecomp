package com.tp.splosionman;

import org.libsdl.app.SDLActivity;

/**
 * 'Splosion Man (ReXGlue recomp) on Android.
 *
 * All native code lives in libmain.so: the statically linked SDL3, the
 * ReXGlue runtime (librexruntime.so, resolved as a dependency) and the
 * recompiled game. The Xenos GPU plugin (librexgpu-xenos.so) ships in the
 * same native library directory and is dlopen()ed by the runtime.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "main"
        };
    }
}
