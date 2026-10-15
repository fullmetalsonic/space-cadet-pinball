package com.codex.spacecadetpinball;

import android.app.AlertDialog;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.res.Configuration;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Bundle;
import android.os.Build;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.util.Log;
import java.util.Locale;
import java.io.File;
import java.io.IOException;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public final class MainActivity extends SDLActivity {
    private int session = 1;
    private boolean foreground, blocked, leftForeground, nativeReady;
    private AlertDialog dialog;
    private Runnable removePlatformBackCallback;
    private AudioManager audio;
    private AudioFocusRequest audioRequest;
    private WindowInsets latestWindowInsets;
    private boolean insetApplyPosted;
    private ViewTreeObserver.OnPreDrawListener insetGeometryObserver;
    private String lastInsetGeometry;
    private final Runnable applyInsetsAfterLayout=()-> {
        insetApplyPosted=false;
        applySurfaceInsets();
    };
    private static native void nativePause(boolean paused, int session);
    private static native void nativeResetInputSession(int session);
    private static native void nativeCancelInputs();
    private static native void nativeFlushSettings();
    private static native void nativeActivateAudio();
    private static native boolean nativeIsEnglish();
    private static native boolean nativeSetAssetRoot(String path);

    @Override protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "SDL2_mixer", "main"}; }
    @Override protected void onCreate(Bundle state) {
        File assetRoot=null; IOException contentError=null;
        try { assetRoot=AssetInstaller.install(new File(getFilesDir(),"pinball-content"),path->getAssets().open(path)); }
        catch(IOException failure) { contentError=failure; }
        super.onCreate(state);
        if (mBrokenLibraries) return;
        if (contentError != null || assetRoot == null || !nativeSetAssetRoot(assetRoot.getAbsolutePath())) {
            // Removing the surface before it is attached prevents SDL_main from starting on partial assets.
            mLayout.removeAllViews(); mBrokenLibraries=true;
            new AlertDialog.Builder(this).setTitle(R.string.content_error_title).setMessage(R.string.content_error)
                .setPositiveButton(R.string.exit,(d,w)->finish()).setCancelable(false).show();
            return;
        }
        audio=(AudioManager)getSystemService(AUDIO_SERVICE);
        audioRequest=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change -> { if (change != AudioManager.AUDIOFOCUS_GAIN) { blockGame(); if (foreground) showResumeDialog(); } })
            .setWillPauseWhenDucked(true).build();
        configureSurfaceInsets();
        if (Build.VERSION.SDK_INT>=33) removePlatformBackCallback=Api33Back.register(this);
    }
    @Override protected void onResume() {
        super.onResume(); foreground = true;
        requestSurfaceInsets();
        if (mBrokenLibraries || audio == null || audioRequest == null || !nativeReady) return;
        if (leftForeground) { blocked = true; showResumeDialog(); }
        else if (requestAudio()) { nativeActivateAudio(); }
        else { blockGame(); showResumeDialog(); }
    }
    @Override protected void onPause() {
        foreground = false; leftForeground = true;
        blockGame();
        if (audio != null && audioRequest != null) audio.abandonAudioFocusRequest(audioRequest);
        super.onPause();
    }
    @Override protected void onDestroy() {
        if (removePlatformBackCallback!=null) { removePlatformBackCallback.run(); removePlatformBackCallback=null; }
        if (dialog != null) { dialog.dismiss(); dialog = null; }
        if (!mBrokenLibraries && nativeReady) { nativeCancelInputs(); nativeFlushSettings(); }
        nativeReady=false;
        latestWindowInsets=null;
        if (mLayout!=null) {
            mLayout.removeCallbacks(applyInsetsAfterLayout);
            if (insetGeometryObserver!=null && mLayout.getViewTreeObserver().isAlive())
                mLayout.getViewTreeObserver().removeOnPreDrawListener(insetGeometryObserver);
        }
        insetApplyPosted=false; insetGeometryObserver=null;
        super.onDestroy();
    }
    @Override public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        requestSurfaceInsets();
        if (!mBrokenLibraries) { blockGame(); if (foreground) showResumeDialog(); }
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) requestSurfaceInsets();
        if (audio == null || audioRequest == null || mBrokenLibraries) return;
        if (!focused && dialog == null) blockGame();
        else if (focused && foreground && blocked && dialog == null) showResumeDialog();
    }
    @Override public void onBackPressed() {
        if (mBrokenLibraries) { super.onBackPressed(); return; }
        if (dialog!=null) return;
        if (isImeVisible()) { SDLActivity.onNativeKeyboardFocusLost(); return; }
        blockGame(); showResumeDialog();
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        // SDL's Surface key listener otherwise consumes legacy BACK before Activity.onBackPressed.
        // Mouse BACK retains SDL's existing mouse handling; IME keys retain its pre-IME hide path.
        if (event.getKeyCode()==KeyEvent.KEYCODE_BACK &&
            (event.getSource() & InputDevice.SOURCE_MOUSE)!=InputDevice.SOURCE_MOUSE &&
            !mBrokenLibraries && dialog==null && !isImeVisible()) {
            if (event.getAction()==KeyEvent.ACTION_UP && !event.isCanceled()) onBackPressed();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    private boolean isImeVisible() {
        if (Build.VERSION.SDK_INT>=30 && latestWindowInsets!=null)
            return latestWindowInsets.isVisible(WindowInsets.Type.ime());
        return SDLActivity.isScreenKeyboardShown();
    }
    // Isolate API-33 types so the API-26 fallback does not load the platform callback class.
    @android.annotation.TargetApi(33)
    private static final class Api33Back {
        static Runnable register(MainActivity activity) {
            android.window.OnBackInvokedDispatcher dispatcher=activity.getOnBackInvokedDispatcher();
            android.window.OnBackInvokedCallback callback=activity::onBackPressed;
            dispatcher.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,callback);
            return ()->dispatcher.unregisterOnBackInvokedCallback(callback);
        }
    }
    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_CANCEL && !mBrokenLibraries) nativeCancelInputs();
        return super.dispatchTouchEvent(event);
    }
    private void configureSurfaceInsets() {
        View decor=getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view,insets)-> {
            latestWindowInsets=insets;
            // Keep DecorView's own fitting/bar handling, then account only for the remaining overlap.
            WindowInsets dispatched=view.onApplyWindowInsets(insets);
            scheduleSurfaceInsets();
            return dispatched;
        });
        mLayout.addOnLayoutChangeListener((view,l,t,r,b,oldL,oldT,oldR,oldB)->scheduleSurfaceInsets());
        requestSurfaceInsets();
    }
    private void scheduleSurfaceInsets() {
        if (insetApplyPosted || mBrokenLibraries || mLayout==null || isDestroyed()) return;
        insetApplyPosted=true;
        if (!mLayout.post(applyInsetsAfterLayout)) insetApplyPosted=false;
    }
    private void requestSurfaceInsets() {
        if (mBrokenLibraries || mLayout==null || isDestroyed()) return;
        getWindow().getDecorView().requestApplyInsets();
    }
    private void applySurfaceInsets() {
        if (latestWindowInsets==null || mBrokenLibraries || mLayout==null || isDestroyed()) return;
        View decor=getWindow().getDecorView();
        if (decor.getWidth()<=0 || decor.getHeight()<=0 || mLayout.getWidth()<=0 || mLayout.getHeight()<=0) return;
        int left,top,right,bottom;
        if (Build.VERSION.SDK_INT>=30) {
            android.graphics.Insets safe=latestWindowInsets.getInsets(WindowInsets.Type.systemBars() |
                WindowInsets.Type.displayCutout() | WindowInsets.Type.mandatorySystemGestures() | WindowInsets.Type.ime());
            left=safe.left; top=safe.top; right=safe.right; bottom=safe.bottom;
        } else {
            left=latestWindowInsets.getSystemWindowInsetLeft(); top=latestWindowInsets.getSystemWindowInsetTop();
            right=latestWindowInsets.getSystemWindowInsetRight(); bottom=latestWindowInsets.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT>=28 && latestWindowInsets.getDisplayCutout()!=null) {
                android.view.DisplayCutout cutout=latestWindowInsets.getDisplayCutout();
                left=Math.max(left,cutout.getSafeInsetLeft()); top=Math.max(top,cutout.getSafeInsetTop());
                right=Math.max(right,cutout.getSafeInsetRight()); bottom=Math.max(bottom,cutout.getSafeInsetBottom());
            }
            if (Build.VERSION.SDK_INT>=29) {
                android.graphics.Insets gestures=latestWindowInsets.getMandatorySystemGestureInsets();
                left=Math.max(left,gestures.left); top=Math.max(top,gestures.top);
                right=Math.max(right,gestures.right); bottom=Math.max(bottom,gestures.bottom);
            }
        }
        int[] location=new int[2]; mLayout.getLocationInWindow(location);
        int[] padding=SurfaceSafeArea.padding(decor.getWidth(),decor.getHeight(),location[0],location[1],
            mLayout.getWidth(),mLayout.getHeight(),left,top,right,bottom);
        if (mLayout.getPaddingLeft()!=padding[0] || mLayout.getPaddingTop()!=padding[1] ||
            mLayout.getPaddingRight()!=padding[2] || mLayout.getPaddingBottom()!=padding[3]) {
            // Resize the actual child SurfaceView; SDL native size and MotionEvent coordinates stay aligned.
            mLayout.setPadding(padding[0],padding[1],padding[2],padding[3]);
            mLayout.requestLayout();
        }
        logSurfaceGeometryAfterLayout();
    }
    private void logSurfaceGeometryAfterLayout() {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE)==0 || insetGeometryObserver!=null) return;
        insetGeometryObserver=()-> {
            ViewTreeObserver observer=mLayout.getViewTreeObserver();
            if (observer.isAlive()) observer.removeOnPreDrawListener(insetGeometryObserver);
            insetGeometryObserver=null;
            if (mBrokenLibraries || isDestroyed() || mSurface==null) return true;
            int[] parentPosition=new int[2],surfacePosition=new int[2];
            mLayout.getLocationInWindow(parentPosition); mSurface.getLocationInWindow(surfacePosition);
            ViewGroup.LayoutParams params=mSurface.getLayoutParams();
            String geometry="window="+getWindow().getDecorView().getWidth()+"x"+getWindow().getDecorView().getHeight()+
                " parent="+parentPosition[0]+","+parentPosition[1]+","+mLayout.getWidth()+","+mLayout.getHeight()+
                " padding="+mLayout.getPaddingLeft()+","+mLayout.getPaddingTop()+","+mLayout.getPaddingRight()+","+mLayout.getPaddingBottom()+
                " surface="+surfacePosition[0]+","+surfacePosition[1]+","+mSurface.getWidth()+","+mSurface.getHeight()+
                " params="+(params==null?"none":params.width+","+params.height)+
                " holder="+mSurface.getHolder().getSurfaceFrame().toShortString();
            if (!geometry.equals(lastInsetGeometry)) { Log.i("PinballSurface",geometry); lastInsetGeometry=geometry; }
            return true;
        };
        mLayout.getViewTreeObserver().addOnPreDrawListener(insetGeometryObserver);
        // Diagnostic draw only; production and same-padding paths do not request another layout.
        mLayout.invalidate();
    }
    private void blockGame() {
        blocked = true; session++;
        if (!mBrokenLibraries && nativeReady) { nativePause(true, session); nativeCancelInputs(); nativeFlushSettings(); }
    }
    private void resumeGame() {
        if (!foreground || mBrokenLibraries || !nativeReady) return;
        if (!requestAudio()) { showResumeDialog(); return; }
        blocked = false; leftForeground = false;
        session++; nativeResetInputSession(session); nativePause(false, session); nativeActivateAudio();
    }
    private boolean requestAudio() { return audio != null && audioRequest != null && audio.requestAudioFocus(audioRequest)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED; }
    /** The SDL event queue must be ready before sending the initial input session. */
    public void onNativeReady() {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            nativeReady=true;
            if (foreground && !blocked && !leftForeground && requestAudio()) {
                session++; nativeResetInputSession(session); nativeActivateAudio();
            } else { blockGame(); if (foreground) showResumeDialog(); }
        });
    }
    private Context textContext() {
        Configuration config = new Configuration(getResources().getConfiguration());
        config.setLocale(nativeIsEnglish() ? Locale.ENGLISH : Locale.KOREAN);
        return createConfigurationContext(config);
    }
    private void showResumeDialog() {
        if (dialog != null || !foreground || isFinishing()) return;
        Context text = textContext();
        dialog = new AlertDialog.Builder(this).setTitle(text.getString(R.string.paused))
            .setMessage(text.getString(R.string.resume_help))
            .setPositiveButton(text.getString(R.string.resume), (d, w) -> { dialog = null; resumeGame(); })
            .setNegativeButton(text.getString(R.string.exit), (d,w) -> { dialog = null; finish(); })
            .setCancelable(false).create();
        dialog.show();
    }
}
