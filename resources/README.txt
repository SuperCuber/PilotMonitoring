Pilot Monitoring
================

Install this folder into X-Plane's Resources/plugins directory.

In X-Plane, open Plugins > Pilot Monitoring > Reload all plug-ins to reload
every installed plugin after the selected menu callback returns.

Bind the Pilot Monitoring: hold to capture a voice command action to a joystick
button in X-Plane's control settings. While held, the plugin records from the
Windows default microphone. On release it transcribes the speech, logs the
transcript to Log.txt, matches it against the bundled commands.json config, and
executes the configured X-Plane command or dataref action.
