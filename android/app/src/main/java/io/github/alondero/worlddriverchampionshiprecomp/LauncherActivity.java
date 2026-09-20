package io.github.alondero.worlddriverchampionshiprecomp;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/** Bootstrap activity. ROM import and game launch belong here after the native port exists. */
public final class LauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView message = new TextView(this);
        message.setText("World Driver Championship: Recompiled\nAndroid port not initialized");
        setContentView(message);
    }
}
