package app.cuelight.tv;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.RelativeLayout;

import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.PlaybackParameters;
import androidx.media3.common.Player;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.TrackSelectionParameters;
import androidx.media3.common.Tracks;
import androidx.media3.common.VideoSize;
import androidx.media3.common.text.CueGroup;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.analytics.AnalyticsListener;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.ui.CaptionStyleCompat;
import androidx.media3.ui.SubtitleView;

import androidx.core.content.FileProvider;

import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.PlayerConstants;
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.YouTubePlayer;
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.listeners.AbstractYouTubePlayerListener;
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.options.IFramePlayerOptions;
import com.pierfrancescosoffritti.androidyoutubeplayer.core.player.views.YouTubePlayerView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/* The video behind the app (src/video/video_android.c drives it): ExoPlayer on a SurfaceView
 * under SDL's surface, which is translucent and on top, so the hole the player screen draws
 * shows it. Every call from C comes on SDL's thread and is posted to the main thread, where
 * ExoPlayer lives; what C reads is a snapshot taken there ten times a second. */
public final class CuelightPlayer {
    private static final String TAG = "Cuelight";
    // states, as src/video/video.h numbers them
    private static final int IDLE = 0, LOADING = 1, PLAYING = 2, PAUSED = 3, ENDED = 4, ERROR = 5;

    private static Activity sActivity;
    private static Handler sMain;
    private static FrameLayout sBox;          // where the video goes on screen (video_set_display_rect)
    private static SurfaceView sView;
    private static SubtitleView sSubs;        // the file's own subtitle tracks
    private static ExoPlayer sPlayer;
    private static boolean sReady, sKeepAwake;
    private static volatile float sBrightness = -1;   /* the window's own, -1: the system's */
    private static double sStart;
    private static int sRectX, sRectY, sRectW = -1, sRectH = -1;

    // the snapshot C reads (written on the main thread)
    private static volatile double[] sStatus = new double[16];
    private static volatile String sError = "", sCodec = "", sWarning = "";
    private static volatile String[] sAudio = new String[0], sText = new String[0];
    private static int sState = IDLE, sTracksVersion, sWarningVersion;
    private static float sRate = 1, sVolume = 1;
    private static boolean sMuted;
    private static boolean sFirstFrame;
    private static int sFailure, sDroppedFrames;
    private static long sLoadGeneration;

    /* From CuelightActivity.onCreate, after SDL has made its layout. */
    static void attach(Activity a, ViewGroup layout, SurfaceView sdl) {
        if (sActivity != null) detach(sActivity);
        sActivity = a;
        sMain = new Handler(Looper.getMainLooper());
        sBox = new FrameLayout(a);
        sBox.setBackgroundColor(Color.BLACK);
        sView = new SurfaceView(a);
        sBox.addView(sView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        sSubs = new SubtitleView(a);
        sSubs.setUserDefaultStyle();
        sSubs.setUserDefaultTextSize();
        sBox.setVisibility(View.INVISIBLE);
        layout.addView(sBox, 0, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        // the file's subtitles in the window, over SDL's surface (both surfaces sit behind it)
        layout.addView(sSubs, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        // SDL's surface over the video, see-through where the app draws alpha 0
        sdl.setZOrderMediaOverlay(true);
        sdl.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        sMain.postDelayed(sTick, 100);
    }

    static void detach(Activity a) {
        if (sActivity != a) return;
        if (sMain != null) sMain.removeCallbacksAndMessages(null);
        sGlowOn = false; sGlowBusy = false; sGlowBmp = null;
        if (sPlayer != null) { sPlayer.release(); sPlayer = null; }
        if (sYtView != null) { sYtView.release(); sYtView = null; }
        sYt = null; sYtPending = null; sEmbed = false;
        sPipAllowed = false; sInPip = false; sKeepAwake = false;
        a.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        sActivity = null; sMain = null; sBox = null; sView = null; sSubs = null;
        sReady = false; sState = IDLE;
        sAudio = new String[0]; sText = new String[0];
        sFirstFrame = false; sFailure = 0; sDroppedFrames = 0;
        sError = ""; sCodec = ""; sWarning = "";
        sStatus = new double[16];
    }

    private static void run(Runnable r) {
        Handler h = sMain;
        if (h != null) h.post(() -> { if (h == sMain && sActivity != null) r.run(); });
    }

    private static ExoPlayer player() {
        if (sPlayer != null) return sPlayer;
        DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory()
            .setAllowCrossProtocolRedirects(true)
            .setConnectTimeoutMs(15000).setReadTimeoutMs(20000)
            .setUserAgent("Cuelight/" + BuildConfig.VERSION_NAME);
        DefaultRenderersFactory renderers = new DefaultRenderersFactory(sActivity)
            .setEnableDecoderFallback(true)
            .setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER);
        sPlayer = new ExoPlayer.Builder(sActivity, renderers)
            .setMediaSourceFactory(new DefaultMediaSourceFactory(new DefaultDataSource.Factory(sActivity, http)))
            .setLoadControl(loadControl())
            .build();
        sPlayer.setVideoSurfaceView(sView);
        sPlayer.setTrackSelectionParameters(sPlayer.getTrackSelectionParameters().buildUpon()
            .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());   // the app picks subtitles itself
        sPlayer.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) {
                if (s == Player.STATE_READY && !sReady) {
                    sReady = true;
                }
                if (s == Player.STATE_ENDED) sState = ENDED;
                snapshot();
            }
            @Override public void onIsPlayingChanged(boolean playing) { keepAwake(); snapshot(); }
            @Override public void onPlayerError(PlaybackException e) {
                sState = ERROR;
                int code = e.errorCode;
                sFailure = code == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_FAILED
                    || code == PlaybackException.ERROR_CODE_IO_NETWORK_CONNECTION_TIMEOUT
                    || code == PlaybackException.ERROR_CODE_IO_UNSPECIFIED ? 1
                    : code >= 3000 && code < 6000 ? 3 : 2;
                sError = "Playback failed (" + e.getErrorCodeName() + ")";
                Log.w(TAG, "video: " + sError);
                snapshot();
            }
            @Override public void onTracksChanged(Tracks t) { tracks(t); }
            @Override public void onVideoSizeChanged(VideoSize v) { layout(); snapshot(); }
            @Override public void onRenderedFirstFrame() { sFirstFrame = true; snapshot(); }
            @Override public void onCues(CueGroup cues) { sSubs.setCues(cues.cues); }
        });
        sPlayer.addAnalyticsListener(new AnalyticsListener() {
            @Override public void onDroppedVideoFrames(EventTime time, int count, long elapsedMs) {
                sDroppedFrames += count;
            }
        });
        return sPlayer;
    }

    // ---- from C ----

    public static void load(final String url, final double start, final long generation) {
        if (url.startsWith(EMBED)) { run(() -> { sLoadGeneration = generation; embedLoad(url.substring(EMBED.length()), start); }); return; }
        run(() -> {
            sLoadGeneration = generation;
            embedStop();
            ExoPlayer p = player();
            sReady = false; sStart = start; sError = ""; sCodec = ""; sState = LOADING;
            sFirstFrame = false; sFailure = 0; sDroppedFrames = 0;
            sAudio = new String[0]; sText = new String[0]; sTracksVersion++;
            sSubs.setCues(new ArrayList<>());
            MediaItem.Builder b = new MediaItem.Builder().setUri(Uri.parse(url));
            String path = Uri.parse(url).getPath();
            String low = path != null ? path.toLowerCase(Locale.ROOT) : "";
            if (low.contains(".m3u8") || low.endsWith("/playlist") || low.endsWith("/master")) b.setMimeType(MimeTypes.APPLICATION_M3U8);
            else if (low.endsWith(".mpd")) b.setMimeType(MimeTypes.APPLICATION_MPD);
            p.setMediaItem(b.build(), (long) (Math.max(0, start) * 1000));
            p.setPlaybackParameters(new PlaybackParameters(sRate = 1));
            sMuted = false; sVolume = 1; p.setVolume(1);
            p.setPlayWhenReady(true);
            p.prepare();
            sBox.setVisibility(View.VISIBLE);
            sSubs.setVisibility(View.VISIBLE);
            snapshot();
        });
    }

    public static void play() { run(() -> { if (sEmbed) { if (sYt != null) { if (sState == ENDED) sYt.seekTo(0); sYt.play(); } return; } if (sPlayer != null) { if (sState == ENDED) sPlayer.seekTo(0); sPlayer.play(); snapshot(); } }); }
    public static void pause() { run(() -> { if (sEmbed) { if (sYt != null) sYt.pause(); return; } if (sPlayer != null) { sPlayer.pause(); snapshot(); } }); }
    public static void seek(final double s) { run(() -> { if (sEmbed) { if (sYt != null) sYt.seekTo((float) s); else sStart = s; return; } if (sPlayer != null) { sStart = s; sPlayer.seekTo((long) (s * 1000)); snapshot(); } }); }
    public static void stop() {
        run(() -> {
            embedStop();
            if (sPlayer != null) { sPlayer.stop(); sPlayer.clearMediaItems(); }
            sState = IDLE; sReady = false; sSubs.setCues(new ArrayList<>());
            sBox.setVisibility(View.INVISIBLE);
            sSubs.setVisibility(View.INVISIBLE);
            keepAwake(); snapshot();
        });
    }
    public static void setRate(final double r) { run(() -> { sRate = (float) r; if (sPlayer != null) sPlayer.setPlaybackParameters(new PlaybackParameters(sRate)); }); }
    public static void setVolume(final int percent) { run(() -> { sVolume = percent / 100f; if (sPlayer != null) sPlayer.setVolume(sMuted ? 0 : sVolume); if (sYt != null) sYt.setVolume(percent); }); }
    public static void setMuted(final boolean m) { run(() -> { sMuted = m; if (sPlayer != null) sPlayer.setVolume(m ? 0 : sVolume); if (sYt != null) { if (m) sYt.mute(); else sYt.unMute(); } }); }
    public static void setKeepAwake(final boolean on) { run(() -> { sKeepAwake = on; keepAwake(); }); }

    /* The app's memory stage (0 plenty, 1 getting low, 2 tight): the player's buffers are sized by
     * it when it is built; one standing idle is let go so the next load builds a leaner one. */
    private static int sMemStage;
    public static void setMemoryStage(final int stage) {
        run(() -> {
            if (sMemStage == stage) return;
            sMemStage = stage;
            if (sPlayer != null && !sPlayer.isPlaying() && sPlayer.getPlaybackState() == Player.STATE_IDLE) { sPlayer.release(); sPlayer = null; }
        });
    }

    private static DefaultLoadControl loadControl() {
        DefaultLoadControl.Builder b = new DefaultLoadControl.Builder();
        if (sMemStage >= 2) b.setBufferDurationsMs(8000, 20000, 1500, 3000).setTargetBufferBytes(12 << 20);
        else if (sMemStage == 1) b.setBufferDurationsMs(15000, 30000, 2000, 4000).setTargetBufferBytes(24 << 20);
        return b.build();
    }
    public static void setRect(final int x, final int y, final int w, final int h) { run(() -> { sRectX = x; sRectY = y; sRectW = w; sRectH = h; layout(); }); }

    /* index: group << 8 | track, as tracks() lists them; -1 turns the file's subtitles off. */
    public static void selectTrack(final int kind, final int index) {
        run(() -> {
            if (sPlayer == null) return;
            int type = kind == 0 ? C.TRACK_TYPE_AUDIO : C.TRACK_TYPE_TEXT;
            TrackSelectionParameters.Builder b = sPlayer.getTrackSelectionParameters().buildUpon();
            List<Tracks.Group> groups = sPlayer.getCurrentTracks().getGroups();
            int g = index >> 8, t = index & 0xFF;
            if (index < 0 || g >= groups.size()) { if (type == C.TRACK_TYPE_TEXT) b.setTrackTypeDisabled(type, true); }
            else {
                b.setTrackTypeDisabled(type, false);
                b.setOverrideForType(new TrackSelectionOverride(groups.get(g).getMediaTrackGroup(), t));
            }
            sPlayer.setTrackSelectionParameters(b.build());
        });
    }

    /* size 0..3, position 0 bottom / 1 raised / 2 top, the colour as #RRGGBB or null (the style's own),
     * style as the app's SUBS_STYLE_* (src/ui/widgets.h). The viewer's look wins over the file's own. */
    private static float sSubPad = 0.08f, sSubLift = 0;
    public static void subtitleStyle(final int size, final String colour, final int position, final int style) {
        run(() -> {
            float[] sizes = { 0.045f, 0.055f, 0.068f, 0.082f };
            sSubs.setApplyEmbeddedStyles(false);
            sSubs.setApplyEmbeddedFontSizes(false);
            sSubs.setFractionalTextSize(sizes[Math.max(0, Math.min(3, size))]);
            int fg = Color.WHITE, bg = Color.TRANSPARENT, edge = CaptionStyleCompat.EDGE_TYPE_DROP_SHADOW;
            switch (style) {
                case 1: edge = CaptionStyleCompat.EDGE_TYPE_OUTLINE; break;                   // bold outline
                case 2: fg = 0xFFFFE14D; edge = CaptionStyleCompat.EDGE_TYPE_OUTLINE; break;  // classic yellow
                case 3: bg = 0xFF000000; edge = CaptionStyleCompat.EDGE_TYPE_NONE; break;     // black box
                case 4: edge = CaptionStyleCompat.EDGE_TYPE_OUTLINE; break;                   // bright
                case 5: edge = CaptionStyleCompat.EDGE_TYPE_RAISED; break;                    // raised
                case 6: bg = 0x99000000; edge = CaptionStyleCompat.EDGE_TYPE_NONE; break;     // soft box
                case 7: fg = 0xFF4DD9FF; edge = CaptionStyleCompat.EDGE_TYPE_OUTLINE; break;  // cyan
                default: break;                                                               // soft shadow
            }
            try { if (colour != null && colour.startsWith("#")) fg = Color.parseColor(colour); } catch (IllegalArgumentException ignored) { }
            sSubs.setStyle(new CaptionStyleCompat(fg, bg, Color.TRANSPARENT, edge, Color.BLACK, null));
            sSubPad = position == 2 ? 0.78f : position == 1 ? 0.2f : 0.08f;
            sSubs.setBottomPaddingFraction(Math.max(sSubPad, sSubLift));
        });
    }

    /* While the app's controls show, the file's subtitles go up above them (a fraction of the
     * height from the bottom; 0 back to where the viewer put them). */
    public static void subtitleLift(final float fraction) {
        run(() -> { sSubLift = fraction; sSubs.setBottomPaddingFraction(Math.max(sSubPad, sSubLift)); });
    }

    /* Picture in picture (Android 8+): a playing video goes along into a small window when
     * the viewer leaves (Home), or now when asked (its button). The app draws no controls
     * while it is in there. */
    private static volatile boolean sPipAllowed, sInPip;
    public static void pipAllow(final boolean on) { sPipAllowed = on; run(CuelightPlayer::pipParams); }
    public static boolean inPip() { return sInPip; }
    private static boolean pipSupported() {
        return Build.VERSION.SDK_INT >= 26 && sActivity != null
            && sActivity.getPackageManager().hasSystemFeature(android.content.pm.PackageManager.FEATURE_PICTURE_IN_PICTURE);
    }
    public static boolean pipEnter() {
        if (!pipSupported()) return false;
        run(() -> {
            try { sActivity.enterPictureInPictureMode(pipBuild()); }
            catch (IllegalStateException | IllegalArgumentException e) { Log.w(TAG, "picture in picture: " + e.getMessage()); }
        });
        return true;
    }
    private static void pipParams() {
        if (!pipSupported()) return;
        try { sActivity.setPictureInPictureParams(pipBuild()); }
        catch (IllegalStateException | IllegalArgumentException e) { Log.w(TAG, "picture in picture: " + e.getMessage()); }
    }
    @android.annotation.TargetApi(26)
    private static android.app.PictureInPictureParams pipBuild() {
        android.app.PictureInPictureParams.Builder b = new android.app.PictureInPictureParams.Builder();
        VideoSize v = sPlayer != null ? sPlayer.getVideoSize() : VideoSize.UNKNOWN;
        int w = v.width > 0 ? v.width : 16, h = v.height > 0 ? v.height : 9;
        if (w * 100 > h * 239) { w = 239; h = 100; } else if (h * 100 > w * 239) { w = 100; h = 239; }   /* Android allows 2.39:1 at most */
        b.setAspectRatio(new android.util.Rational(w, h));
        if (Build.VERSION.SDK_INT >= 31) b.setAutoEnterEnabled(sPipAllowed);
        return b.build();
    }
    /* the activity: the viewer is leaving (Android 8 to 11; 12 on enters by itself) */
    static void onUserLeaving() { if (sPipAllowed && Build.VERSION.SDK_INT < 31) pipEnter(); }
    static void onPipChanged(boolean in) { sInPip = in; Log.i(TAG, in ? "picture in picture" : "picture in picture: full screen again"); }

    /* a phone: sideways only, or as it is held. Sideways is the player: the status and
     * navigation bars go (a swipe from the edge shows them a moment); upright they stay, the
     * app drawn between them, so the system's gestures never land on the app's own buttons. */
    public static void landscapeOnly(final boolean on) {
        run(() -> {
            sActivity.setRequestedOrientation(on ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                                                 : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER);
            systemBars(!on);
        });
    }

    private static volatile boolean sFull;   /* the player sideways: the whole screen, no insets */
    static boolean fullScreen() { return sFull; }

    @SuppressWarnings("deprecation")
    static void systemBars(boolean shown) {
        if (!CuelightActivity.sPhone) return;   /* TVs and tablets: full screen throughout, as before */
        sFull = !shown;
        View decor = sActivity.getWindow().getDecorView();
        if (shown) {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_VISIBLE);
            sActivity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        } else {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            sActivity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
        if (Build.VERSION.SDK_INT >= 20) decor.requestApplyInsets();   /* the view's padding again: none in the player */
    }

    /* A link the app was opened with (a stremio:// addon, a manifest address), once. */
    private static volatile String sOpenUrl;
    static void offerUrl(String url) { if (url != null && !url.isEmpty()) { sOpenUrl = url; Log.i(TAG, "opened with a link"); } }
    public static String takeOpenUrl() { String u = sOpenUrl; sOpenUrl = null; return u; }

    /* the phone's media volume, 0..1 (the player's controls; no system slider pops up) */
    public static float deviceVolume() {
        AudioManager am = (AudioManager) sActivity.getSystemService(Context.AUDIO_SERVICE);
        int max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        return max > 0 ? am.getStreamVolume(AudioManager.STREAM_MUSIC) / (float) max : -1;
    }
    public static void setDeviceVolume(final float v) {
        AudioManager am = (AudioManager) sActivity.getSystemService(Context.AUDIO_SERVICE);
        int max = am.getStreamMaxVolume(AudioManager.STREAM_MUSIC), want = Math.round(Math.max(0, Math.min(1, v)) * max);
        try { if (want != am.getStreamVolume(AudioManager.STREAM_MUSIC)) am.setStreamVolume(AudioManager.STREAM_MUSIC, want, 0); }
        catch (SecurityException e) { Log.w(TAG, "volume: not allowed now (Do Not Disturb?)"); }
    }
    /* the screen's brightness for this window, 0..1; -1 gives it back to the system */
    public static float brightness() {
        if (sBrightness >= 0) return sBrightness;
        try { return Settings.System.getInt(sActivity.getContentResolver(), Settings.System.SCREEN_BRIGHTNESS) / 255f; }
        catch (Settings.SettingNotFoundException e) { return 0.5f; }
    }
    public static void setBrightness(final float v) {
        sBrightness = v;
        run(() -> {
            WindowManager.LayoutParams lp = sActivity.getWindow().getAttributes();
            lp.screenBrightness = v < 0 ? WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE : Math.max(0.01f, Math.min(1, v));
            sActivity.getWindow().setAttributes(lp);
        });
    }

    // ---- updating the app: download the new APK, check it, hand it to Android's installer ----

    private static volatile int sUpdState;          /* 0 nothing asked, 1 working, 2 handed to the installer, -1 failed */
    private static volatile String sUpdText = "";
    private static volatile boolean sUpdWaiting;    /* waiting for "Install unknown apps" to be allowed */
    private static File sUpdFile;

    public static void installUpdate(final String url, final String sha256) {
        if (sUpdState == 1) return;
        sUpdState = 1; sUpdText = "Downloading\u2026"; sUpdWaiting = false;
        new Thread(() -> {
            try {
                File dir = new File(sActivity.getCacheDir(), "updates");
                if (!dir.isDirectory() && !dir.mkdirs()) throw new Exception("No room for the download");
                File f = new File(dir, "cuelight-update.apk");
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                c.setInstanceFollowRedirects(true);
                c.setConnectTimeout(15000); c.setReadTimeout(30000);
                c.setRequestProperty("User-Agent", "Cuelight");
                if (c.getResponseCode() != 200) throw new Exception("The download failed (" + c.getResponseCode() + ")");
                long total = c.getContentLengthLong(), got = 0;
                try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(f)) {
                    byte[] b = new byte[64 * 1024];
                    int n;
                    while ((n = in.read(b)) > 0) {
                        out.write(b, 0, n); md.update(b, 0, n); got += n;
                        if (total > 0) sUpdText = "Downloading\u2026  " + (int) (got * 100 / total) + "%";
                    }
                }
                StringBuilder hex = new StringBuilder();
                for (byte x : md.digest()) hex.append(String.format(Locale.ROOT, "%02x", x));
                if (sha256 != null && !sha256.isEmpty() && !hex.toString().equalsIgnoreCase(sha256)) {
                    f.delete();
                    throw new Exception("The download was damaged (its checksum is wrong). Try again.");
                }
                sUpdFile = f;
                run(CuelightPlayer::offerInstall);
            } catch (Exception e) {
                Log.w(TAG, "update: " + e);
                sUpdText = e.getMessage() != null ? e.getMessage() : "The download failed";
                sUpdState = -1;
            }
        }, "cuelight-update").start();
    }

    /* Android asks the viewer: first whether Cuelight may install apps at all (once), then the install itself */
    private static void offerInstall() {
        try {
            if (Build.VERSION.SDK_INT >= 26 && !sActivity.getPackageManager().canRequestPackageInstalls()) {
                sUpdWaiting = true;
                sUpdText = "Allow Cuelight to install apps, then come back here";
                sActivity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + sActivity.getPackageName())));
                return;
            }
            sUpdWaiting = false;
            Uri u = FileProvider.getUriForFile(sActivity, sActivity.getPackageName() + ".updates", sUpdFile);
            Intent i = new Intent(Intent.ACTION_VIEW).setDataAndType(u, "application/vnd.android.package-archive")
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            sActivity.startActivity(i);
            sUpdText = "Confirm the install on the screen Android shows";
            sUpdState = 2;
        } catch (Exception e) {
            Log.w(TAG, "update: " + e);
            sUpdText = "Android's installer did not open: " + e.getMessage();
            sUpdState = -1;
        }
    }

    public static int installState() {
        if (sUpdWaiting && sUpdFile != null && (Build.VERSION.SDK_INT < 26 || sActivity.getPackageManager().canRequestPackageInstalls())) {
            sUpdWaiting = false;   /* allowed now: on to the install */
            run(CuelightPlayer::offerInstall);
        }
        return sUpdState;
    }
    public static String installText() { return sUpdText; }

    // ---- YouTube's own player, for a video YouTube refuses to the app's (src/ui: youtube-embed:ID) ----

    private static final String EMBED = "youtube-embed:";
    private static YouTubePlayerView sYtView;
    private static YouTubePlayer sYt;         // once its page is ready
    private static boolean sEmbed;            // the embedded player is the one playing
    private static String sYtPending;         // asked for before the page was ready
    private static double sYtPos, sYtDur;
    private static boolean sYtBuffering;

    private static void embedLoad(String id, double start) {
        if (sPlayer != null) { sPlayer.stop(); sPlayer.clearMediaItems(); }
        sEmbed = true; sState = LOADING; sError = ""; sCodec = "YouTube player"; sStart = start;
        sYtPos = start; sYtDur = 0; sYtBuffering = true;
        sAudio = new String[0]; sText = new String[0]; sTracksVersion++;
        sView.setVisibility(View.INVISIBLE);
        if (sYtView == null) {
            sYtView = new YouTubePlayerView(sActivity);
            sYtView.setEnableAutomaticInitialization(false);
            sBox.addView(sYtView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            IFramePlayerOptions opts = new IFramePlayerOptions.Builder(sActivity).controls(0).rel(0).ivLoadPolicy(3).ccLoadPolicy(0).fullscreen(0).build();
            sYtPending = id;
            sYtView.initialize(new AbstractYouTubePlayerListener() {
                @Override public void onReady(YouTubePlayer p) {
                    sYt = p;
                    if (sYtPending != null) { p.loadVideo(sYtPending, (float) sStart); sYtPending = null; }
                }
                @Override public void onStateChange(YouTubePlayer p, PlayerConstants.PlayerState st) {
                    if (!sEmbed) return;
                    sYtBuffering = st == PlayerConstants.PlayerState.BUFFERING;
                    if (st == PlayerConstants.PlayerState.PLAYING) sState = PLAYING;
                    else if (st == PlayerConstants.PlayerState.PAUSED) sState = PAUSED;
                    else if (st == PlayerConstants.PlayerState.ENDED) sState = ENDED;
                    keepAwake(); snapshot();
                }
                @Override public void onCurrentSecond(YouTubePlayer p, float s) { if (sEmbed) sYtPos = s; }
                @Override public void onVideoDuration(YouTubePlayer p, float d) { if (sEmbed) sYtDur = d; }
                @Override public void onError(YouTubePlayer p, PlayerConstants.PlayerError e) {
                    if (!sEmbed) return;
                    sState = ERROR;
                    sError = e == PlayerConstants.PlayerError.VIDEO_NOT_PLAYABLE_IN_EMBEDDED_PLAYER ? "YouTube does not allow this video outside its own app"
                           : e == PlayerConstants.PlayerError.VIDEO_NOT_FOUND ? "YouTube says this video is not available" : "YouTube's player: " + e.name();
                    Log.w(TAG, "video: " + sError);
                    snapshot();
                }
            }, false, opts);
        } else if (sYt != null) sYt.loadVideo(id, (float) start);
        else sYtPending = id;
        sYtView.setVisibility(View.VISIBLE);
        sBox.setVisibility(View.VISIBLE);
        Log.i(TAG, "video: YouTube's own player");
        snapshot();
    }

    private static void embedStop() {
        if (!sEmbed) return;
        sEmbed = false; sYtPending = null;
        if (sYt != null) sYt.pause();
        if (sYtView != null) sYtView.setVisibility(View.GONE);
        sView.setVisibility(View.VISIBLE);
    }

    public static double[] status() { return sStatus; }

    /* The living letterbox's light from the picture itself, as it plays: the video surface
     * copied, shrunk to GLOW_W x GLOW_H, about 8 times a second while the app asks for it. */
    static final int GLOW_W = 32, GLOW_H = 18;
    private static Bitmap sGlowBmp;
    private static final int[] sGlowPx = new int[GLOW_W * GLOW_H];
    private static int sGlowSeq;
    private static boolean sGlowOn, sGlowBusy;
    private static final Runnable sGlowTick = new Runnable() {
        @Override public void run() {
            if (!sGlowOn) return;
            glowCapture();
            if (sMain != null) sMain.postDelayed(this, 125);
        }
    };
    public static void glowOn(final boolean on) {
        run(() -> {
            if (on == sGlowOn) return;
            sGlowOn = on;
            if (on && sMain != null) sMain.post(sGlowTick);
        });
    }
    private static void glowCapture() {
        if (sGlowBusy || sEmbed || sPlayer == null || sView == null || sView.getHolder() == null) return;
        if (!sView.getHolder().getSurface().isValid() || sView.getWidth() <= 0) return;
        if (sGlowBmp == null) sGlowBmp = Bitmap.createBitmap(GLOW_W, GLOW_H, Bitmap.Config.ARGB_8888);
        Bitmap bitmap = sGlowBmp;
        sGlowBusy = true;
        try {
            PixelCopy.request(sView, bitmap, result -> {
                if (bitmap != sGlowBmp) return;
                sGlowBusy = false;
                if (result != PixelCopy.SUCCESS) return;
                synchronized (sGlowPx) { bitmap.getPixels(sGlowPx, 0, GLOW_W, 0, 0, GLOW_W, GLOW_H); sGlowSeq++; }
            }, sMain);
        } catch (IllegalArgumentException e) { sGlowBusy = false; }
    }
    /* The latest copy (ARGB) into out; its number, which changes with each new one (0: none yet). */
    public static int glowFrame(int[] out) {
        synchronized (sGlowPx) {
            if (out != null && out.length >= sGlowPx.length) System.arraycopy(sGlowPx, 0, out, 0, sGlowPx.length);
            return sGlowSeq;
        }
    }
    public static String error() { return sError; }
    public static String codec() { return sCodec; }
    public static String warning() { return sWarning; }
    /* "index\tlang\tlabel\tcodec\tselected" per track */
    public static String[] tracks(int kind) { return kind == 0 ? sAudio : sText; }

    // ---- on the main thread ----

    private static final Runnable sTick = CuelightPlayer::tick;
    private static void tick() {
        if (sMain == null || sActivity == null) return;
        snapshot();
        sMain.postDelayed(sTick, 100);
    }

    private static void keepAwake() {
        boolean on = sKeepAwake || (sPlayer != null && sPlayer.isPlaying()) || (sEmbed && sState == PLAYING);
        if (sView != null) sView.setKeepScreenOn(on);
        // the video view is hidden when nothing plays, and a hidden view's flag counts for nothing:
        // the window's own flag keeps the screen on for the screensaver
        if (sActivity != null) {
            if (on) sActivity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else sActivity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    private static void snapshot() {
        if (sEmbed) {
            double[] s = new double[16];
            s[0] = sState; s[1] = sYtPos; s[2] = sYtDur; s[3] = sYtBuffering ? 1 : 0; s[5] = -1;
            s[10] = sTracksVersion; s[11] = sWarningVersion;
            s[12] = sState == ERROR ? 2 : 0; s[13] = sState == PLAYING ? 1 : 0;
            s[15] = sLoadGeneration;
            sStatus = s;
            return;
        }
        ExoPlayer p = sPlayer;
        double[] s = new double[16];
        int state = sState;
        if (p != null && state != ERROR && state != IDLE) {
            int ps = p.getPlaybackState();
            if (ps == Player.STATE_ENDED) state = ENDED;
            else if (!sReady) state = LOADING;
            else state = p.getPlayWhenReady() ? PLAYING : PAUSED;
            sState = state;
        }
        s[0] = state;
        if (p != null && state != IDLE) {
            long pos = p.getCurrentPosition(), dur = p.getDuration(), buf = p.getBufferedPosition();
            s[1] = (sReady ? pos : sStart * 1000) / 1000.0;
            s[2] = dur == C.TIME_UNSET ? 0 : dur / 1000.0;
            s[3] = p.getPlaybackState() == Player.STATE_BUFFERING ? 1 : 0;
            s[4] = p.getBufferedPercentage();
            s[5] = sReady && buf >= pos ? (buf - pos) / 1000.0 : -1;
            Format v = p.getVideoFormat();
            if (v != null) {
                s[6] = v.width; s[7] = v.height; s[8] = v.frameRate > 0 ? Math.round(v.frameRate) : 0; s[9] = v.bitrate > 0 ? v.bitrate : 0;
                Format a = p.getAudioFormat();
                sCodec = name(v) + " " + v.width + "x" + v.height + (a != null ? " / " + name(a) + (a.channelCount > 0 ? " " + a.channelCount + "ch" : "") : "");
            }
        }
        s[10] = sTracksVersion;
        s[11] = sWarningVersion;
        s[12] = sFailure; s[13] = sFirstFrame ? 1 : 0; s[14] = sDroppedFrames;
        s[15] = sLoadGeneration;
        sStatus = s;
    }

    private static String name(Format f) {
        String m = f.sampleMimeType != null ? f.sampleMimeType : f.codecs != null ? f.codecs : "";
        int slash = m.indexOf('/');
        String n = slash >= 0 ? m.substring(slash + 1) : m;
        switch (n) {
            case "avc": return "H264";
            case "hevc": return "HEVC";
            case "x-vnd.on2.vp9": return "VP9";
            case "av01": return "AV1";
            case "mp4a-latm": return "AAC";
            case "ac3": return "AC3";
            case "eac3": return "EAC3";
            case "eac3-joc": return "EAC3 Atmos";
            case "true-hd": return "TrueHD";
            case "vnd.dts": return "DTS";
            case "vnd.dts.hd": return "DTS-HD";
            default: return n.toUpperCase(Locale.ROOT);
        }
    }

    private static void tracks(Tracks t) {
        List<String> au = new ArrayList<>(), tx = new ArrayList<>();
        boolean anyAudio = false, playableAudio = false;
        List<Tracks.Group> groups = t.getGroups();
        for (int g = 0; g < groups.size() && g < 0xFFFFFF; g++) {
            Tracks.Group grp = groups.get(g);
            int type = grp.getType();
            if (type != C.TRACK_TYPE_AUDIO && type != C.TRACK_TYPE_TEXT) continue;
            for (int i = 0; i < grp.length && i < 0xFF; i++) {
                Format f = grp.getTrackFormat(i);
                if (type == C.TRACK_TYPE_AUDIO) { anyAudio = true; if (grp.isTrackSupported(i)) playableAudio = true; }
                String label = f.label != null ? f.label : f.language != null ? new Locale(f.language).getDisplayLanguage() : "";
                String row = (g << 8 | i) + "\t" + clean(f.language) + "\t" + clean(label) + "\t" + clean(name(f)) + "\t" + (grp.isTrackSelected(i) ? 1 : 0);
                (type == C.TRACK_TYPE_AUDIO ? au : tx).add(row);
            }
        }
        sAudio = au.toArray(new String[0]);
        sText = tx.toArray(new String[0]);
        sTracksVersion++;
        if (anyAudio && !playableAudio) {
            sWarning = "This device can't play this file's audio. Try another source.";
            sWarningVersion++;
        }
        snapshot();
    }

    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' '); }

    /* the requested rectangle, the picture letterboxed inside it */
    private static void layout() {
        if (sBox == null) return;
        View parent = (View) sBox.getParent();
        int pw = parent != null ? parent.getWidth() : 0, ph = parent != null ? parent.getHeight() : 0;
        int x = sRectX, y = sRectY, w = sRectW > 0 ? sRectW : pw, h = sRectH > 0 ? sRectH : ph;
        RelativeLayout.LayoutParams box = new RelativeLayout.LayoutParams(Math.max(w, 1), Math.max(h, 1));
        box.leftMargin = x; box.topMargin = y;
        box.rightMargin = -Math.max(0, x + w - pw); box.bottomMargin = -Math.max(0, y + h - ph);   // a zoom may reach past the screen
        sBox.setLayoutParams(box);
        RelativeLayout.LayoutParams subs = new RelativeLayout.LayoutParams(box.width, box.height);
        subs.leftMargin = box.leftMargin; subs.topMargin = box.topMargin; subs.rightMargin = box.rightMargin; subs.bottomMargin = box.bottomMargin;
        sSubs.setLayoutParams(subs);
        VideoSize v = sPlayer != null ? sPlayer.getVideoSize() : VideoSize.UNKNOWN;
        int vw = w, vh = h;
        if (v.width > 0 && v.height > 0 && w > 0 && h > 0) {
            float aspect = v.width * v.pixelWidthHeightRatio / v.height;
            if ((float) w / h > aspect) vw = Math.round(h * aspect); else vh = Math.round(w / aspect);
        }
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(vw, vh, android.view.Gravity.CENTER);
        sView.setLayoutParams(lp);
    }
}
