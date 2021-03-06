package org.helllabs.android.xmp;


public class Xmp {
	public static final int XMP_PLAYER_AMP = 0;			/* Amplification factor */
	public static final int XMP_PLAYER_MIX = 1;			/* Stereo mixing */
	public static final int XMP_PLAYER_INTERP = 2;		/* Interpolation type */
	public static final int XMP_PLAYER_DSP = 3;			/* DSP effect flags */
	public static final int XMP_PLAYER_TIMING = 4;			/* DSP effect flags */

	public static final int XMP_INTERP_NEAREST = 0;		/* Nearest neighbor */
	public static final int XMP_INTERP_LINEAR = 1;		/* Linear (default) */
	public static final int XMP_INTERP_SPLINE = 2;		/* Cubic spline */
	
	public static final int XMP_DSP_LOWPASS = 1 << 0;	/* Lowpass filter effect */

	public static final int XMP_TIMING_VBLANK = 1 << 0;	/* VBlank timing (no CIA tempo setting) */
	
	public static final int XMP_FORMAT_MONO = 1 << 2;
	
	public native int init();
	public native int deinit();
	public native static boolean testModule(String name, ModInfo info);
	public native int loadModule(String name);
	public native int releaseModule();
	public native int startPlayer(int start, int rate, int flags);
	public native int endPlayer();
	public native int playFrame();	
	public native int getBuffer(short buffer[]);
	public native int nextPosition();
	public native int prevPosition();
	public native int setPosition(int n);
	public native int stopModule();
	public native int restartModule();
	public native int seek(int time);
	public native int time();
	public native int mute(int chn, int status);
	public native void getInfo(int[] values);
	public native void setMixer(int parm, int val);
	public native int getLoopCount();
	public native void getModVars(int[] vars);
	public native static String getVersion();
	public native String getModName();
	public native String getModType();
	public native String[] getFormats();
	public native String[] getInstruments();
	public native void getChannelData(int[] volumes, int[] finalvols, int[] pans, int[] instruments, int[] keys, int[] periods);
	public native void getPatternRow(int pat, int row, byte[] rowNotes, byte[] rowInstruments);
	public native void getSampleData(boolean trigger, int ins, int key, int period, int chn, int width, byte[] buffer);
	
	static {
		System.loadLibrary("xmp");
	}
}
