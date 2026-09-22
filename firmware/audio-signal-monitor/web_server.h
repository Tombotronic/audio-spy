#pragma once

// Starts the HTTP server (port 80). Every route requires HTTP Basic Auth
// against config.webPassword. Routes:
//   GET  /                    HTML/JS UI
//   GET  /files               JSON recording listing
//   GET  /stream?name=X       streamed playback (audio/wav)
//   GET  /download?name=X     forced download (Content-Disposition)
//   POST /delete?name=X       delete a recording
//   GET  /status              JSON: recording/paused/liveRms/threshold/stealth/freeBytes/totalBytes/ip
//   POST /threshold  value=F  set + persist RMS threshold
//   POST /stealth    on=0|1   set + persist stealth mode
//   POST /pause                pause the record-analyze-decide loop
//   POST /resume                resume it
//   POST /forcekeep             force-keep the in-progress chunk (one-shot)
//   POST /bypass     on=0|1   keep everything regardless of threshold (not persisted)
void webServerStart();

// Must be called from the main loop() to process incoming requests
// (WebServer is not interrupt/task driven on its own).
void webServerHandle();
