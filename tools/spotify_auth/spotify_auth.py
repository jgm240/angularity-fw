#!/usr/bin/env python3
"""
One-time OAuth helper for the Spotify Remote Flipper app.

Spotify's Web API doesn't support a headless/device-code auth flow, so
this runs the standard browser-based Authorization Code flow once, on
your computer, and prints a refresh token you type into the Flipper app.
After that, the Flipper renews its own access token directly (a plain
POST, no browser involved) whenever it needs to.

Setup (one time):
1. Create an app at https://developer.spotify.com/dashboard
2. In that app's settings, add this exact Redirect URI:
   http://127.0.0.1:8888/callback
3. Copy the app's Client ID and Client Secret.

Run:
    python3 spotify_auth.py <client_id> <client_secret>

No dependencies beyond the Python standard library.
"""
import base64
import http.server
import json
import sys
import urllib.parse
import urllib.request
import webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing"

auth_code = None


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global auth_code
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        auth_code = params.get("code", [None])[0]
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"Authorized - you can close this tab and return to the terminal.")

    def log_message(self, format, *args):
        pass  # keep stdout clean


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 spotify_auth.py <client_id> <client_secret>")
        sys.exit(1)
    client_id, client_secret = sys.argv[1], sys.argv[2]

    params = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
    }
    auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(params)
    print(f"Opening browser to authorize:\n{auth_url}\n")
    webbrowser.open(auth_url)

    server = http.server.HTTPServer(("127.0.0.1", 8888), CallbackHandler)
    print("Waiting for authorization in the browser...")
    server.handle_request()

    if not auth_code:
        print("No authorization code received - did you cancel, or is the redirect URI wrong?")
        sys.exit(1)

    token_data = urllib.parse.urlencode(
        {
            "grant_type": "authorization_code",
            "code": auth_code,
            "redirect_uri": REDIRECT_URI,
        }
    ).encode()

    basic_auth = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    req = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=token_data,
        headers={
            "Authorization": f"Basic {basic_auth}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
    )
    try:
        with urllib.request.urlopen(req) as resp:
            tokens = json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        print(f"Token exchange failed: {e.read().decode()}")
        sys.exit(1)

    print("\nSuccess! Enter these into the Spotify Remote Flipper app:\n")
    print(f"Client ID:     {client_id}")
    print(f"Client Secret: {client_secret}")
    print(f"Refresh Token: {tokens['refresh_token']}")


if __name__ == "__main__":
    main()
