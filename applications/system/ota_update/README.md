# Angularity Update (WiFi Devboard)

Updates Angularity FW from the Flipper itself, no phone needed.

1. Seat the **WiFi Devboard** running **FlipperHTTP** firmware and set its WiFi once (any FlipperHTTP app can do it).
2. Open **Apps > Tools > Angularity Update > Check and install update**.
3. The app reads `ota/latest.json` from the project repo, shows the version, downloads the
   package in resumable 128 KB chunks (about 20 minutes for 13 MB at 115200 baud), checks its
   SHA-256, unpacks it to `/ext/update`, and hands over to the system updater.

Notes
- Needs about 2.2x the package size free on the SD card, and 30% battery or USB power.
- Back cancels. A cancelled or failed download resumes from where it stopped.
- The devboard cannot follow HTTP redirects, so `latest.json` carries a `wifi_url` that must be a
  direct link (see `tools/make_ota_directory.py --wifi-url` in the qAngularity repo).
- To test against another feed, put an https URL in `/ext/apps_data/ota_update/url.txt`.
- Unverified on hardware: whether the devboard honours HTTP Range requests. If it does not, the
  app falls back to one uninterrupted download.
