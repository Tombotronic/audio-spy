#pragma once

// Starts the HTTP server (port 80). Every route except the icon and the
// manifest requires HTTP Basic Auth against config.webPassword; every POST
// also requires the header "X-Audio-Spy: 1" (CSRF protection). Routes:
//   GET  /                    HTML/JS UI
//   GET  /icon.png            home screen icon (no auth)
//   GET  /manifest.json       web app manifest (no auth)
//   GET  /files               JSON recording listing
//   GET  /stream?name=X       streamed playback (audio/wav)
//   GET  /download?name=X     forced download (Content-Disposition)
//   GET  /peaks?name=X&n=N    coarse waveform: N peak values (JSON array)
//   POST /delete?name=X       delete a recording
//   POST /deleteall           delete all recordings except the open one
//   GET  /status              JSON: recording/paused/levels/threshold/stealth/bypass/space/ip/uptime/battery/version
//   POST /threshold  value=F  set + persist RMS threshold (0..1)
//   POST /stealth    on=0|1   set + persist stealth mode
//   POST /pause               pause the record-analyze-decide loop
//   POST /resume              resume it
//   POST /bypass     on=0|1   keep everything regardless of threshold (not persisted)
//   POST /forgetwifi          clear the saved network and restart into WiFi setup
void webServerStart();

// Must be called from the main loop() to process incoming requests
// (WebServer is not interrupt/task driven on its own).
void webServerHandle();
