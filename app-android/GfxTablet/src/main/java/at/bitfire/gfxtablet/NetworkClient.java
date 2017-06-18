package at.bitfire.gfxtablet;

import android.content.SharedPreferences;
import android.util.Log;

public class NetworkClient implements Runnable {
	static {
		System.loadLibrary("networkclient");
	}

	static final int GFXTABLET_PORT = 40118;

	String hostName;
	final SharedPreferences preferences;
	private long handle;

	NetworkClient(SharedPreferences preferences) {
		this.preferences = preferences;
		handle = init();
	}
	
	boolean reconfigureNetworking() {
		hostName = preferences.getString(SettingsActivity.KEY_PREF_HOST, "unknown.invalid");
		return setHost(handle, hostName, GFXTABLET_PORT);
	}

	void setSize(int x, int y) {
		setSize(handle, x, y);
	}

	void putEvent(float x, float y, float pressure) {
		// motion event
		putEvent(handle, (byte) 0, x, y, pressure, 0, false);
	}

	void putEvent(float x, float y, float pressure, int button, boolean down) {
		// button event
		putEvent(handle, (byte) 1, x, y, pressure, button, down);
	}

	void close() {
		close(handle);
		handle = 0;
	}
	
	@Override
	public void run() {
		loop(handle);
		close();
		Log.d("GfxTablet", "NetworkClient thread exit");
	}

	native private long init();
	native private void close(long handle);
	native private void setSize(long handle, int x, int y);
	native private boolean setHost(long handle, String host, int port);
	native private void putEvent(long handle, byte type, float x, float y, float pressure, int button, boolean down);
	native private void loop(long handle);
}
