#!/usr/bin/env python3
"""Check Fortnite grants on the handheld without printing or exporting tokens.

Uses the existing Orbita sign-in locally. Refresh-token rotation is saved
atomically with mode 0600. Does not create or delete a game session.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import urlencode


def request(label, url, body=None, headers=None, form=False):
    data = None
    merged = {"Accept": "application/json"}
    if body is not None:
        data = urlencode(body) if form else json.dumps(body)
        merged["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json"
    merged.update(headers or {})
    # Match Orbita's libcurl transport. Send the configuration on stdin, so
    # access tokens and Authorization headers never appear in process args.
    config = ["url = " + json.dumps(url), "request = " + json.dumps("POST" if data is not None else "GET"),
              'user-agent = "orbita-r36s/0.1"', "max-time = 25", "compressed"]
    config += ["header = " + json.dumps(k + ": " + v) for k, v in merged.items()]
    if data is not None:
        config.append("data = " + json.dumps(data))
    run = subprocess.run(["curl", "--silent", "--config", "-", "--write-out", "\n%{http_code}"],
                         input="\n".join(config), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         universal_newlines=True, timeout=30)
    if run.returncode:
        print(label + ": network_error", flush=True)
        return None
    content, status_text = run.stdout.rsplit("\n", 1)
    status = int(status_text)
    try:
        value = json.loads(content)
    except ValueError:
        print(label + ": HTTP %d invalid_json" % status, flush=True)
        return None
    # Output only error classifications, never response bodies or credentials.
    code = str(value.get("code", value.get("error", "")))[:64]
    code = "".join(c for c in code if c.isalnum() or c == "_")
    print(label + ": HTTP %d%s" % (status, " " + code if code else ""), flush=True)
    return value if status == 200 else None


def main():
    directory = Path(os.environ.get("ORBITA_DATA_DIR", str(Path.home() / ".local/share/orbita")))
    token_file = directory / "TOKEN.TXT"
    refresh = token_file.read_text().strip()
    auth = request("refresh", "https://login.microsoftonline.com/consumers/oauth2/v2.0/token", {
        "client_id": "1f907974-e22b-4810-a9de-d9647380c97e",
        "grant_type": "refresh_token", "refresh_token": refresh,
        "scope": "xboxlive.signin openid profile offline_access",
    }, form=True)
    if not auth:
        return 1
    rotated = auth.get("refresh_token")
    if rotated and rotated != refresh:
        fd, name = tempfile.mkstemp(prefix=".fortnite-check-", dir=str(directory))
        try:
            with os.fdopen(fd, "w") as output:
                output.write(rotated)
                output.flush()
                os.fsync(output.fileno())
            os.replace(name, str(token_file))
        finally:
            if os.path.exists(name):
                os.unlink(name)
    xbl = {"x-xbl-contract-version": "1"}
    user = request("user_auth", "https://user.auth.xboxlive.com/user/authenticate", {
        "Properties": {"AuthMethod": "RPS", "RpsTicket": "d=" + auth["access_token"], "SiteName": "user.auth.xboxlive.com"},
        "RelyingParty": "http://auth.xboxlive.com", "TokenType": "JWT",
    }, xbl)
    if not user:
        return 1
    xsts = request("xsts", "https://xsts.auth.xboxlive.com/xsts/authorize", {
        "Properties": {"SandboxId": "RETAIL", "UserTokens": [user["Token"]]},
        "RelyingParty": "http://gssv.xboxlive.com/", "TokenType": "JWT",
    }, xbl)
    if not xsts:
        return 1
    for offering in ("xgpuweb", "xgpuwebf2p"):
        cloud = request(offering, "https://%s.gssv-play-prod.xboxlive.com/v2/login/user" % offering,
                        {"token": xsts["Token"], "offeringId": offering}, {"x-gssv-client": "XboxComBrowser"})
        if not cloud:
            continue
        regions = cloud.get("offeringSettings", {}).get("regions", [])
        region = next((r for r in regions if r.get("isDefault")), None)
        if not region:
            print(offering + ": missing_default_region", flush=True)
            continue
        catalog = request(offering + "_titles", region["baseUri"].rstrip("/") + "/v2/titles",
                          headers={"Authorization": "Bearer " + cloud["gsToken"], "x-gssv-client": "XboxComBrowser"})
        if catalog is None:
            continue
        titles = catalog.get("results", [])
        matches = [t for t in titles if t.get("titleId") == "FORTNITE" or
                   t.get("details", {}).get("productId", "").upper() == "BT5P2X999VH2"]
        print(offering + ": catalog_count=%d fortnite_count=%d" % (len(titles), len(matches)), flush=True)
        for title in matches:
            details = title.get("details", {})
            safe = {key: details.get(key) for key in ("hasEntitlement", "programs", "userPrograms", "userSubscriptions")}
            print(offering + " Fortnite: " + json.dumps(safe, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.TimeoutExpired):
        # Avoid tracebacks containing provider data or request objects.
        print("fortnite_check: local_or_protocol_error", flush=True)
        raise SystemExit(1)
