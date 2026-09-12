#!/usr/bin/env python3
"""Isolated loopback fixture for the real NetworkClient integration executable."""
import hashlib
import hmac
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

KEY = b"yataidon-integration-test"
state = {
    "import_requested": True, "username": "", "title": "Test title", "title_bg": 2,
    "chara_color_1": "#123456", "chara_color_2": "#00ab00", "chara_color_3": "#0000ff",
    "chara_head_index": 0, "chara_body_index": 0, "chara_cos_index": 0,
    "chara_is_costume": False, "scores": [],
}
online = True
nonces = set()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, value, status=200):
        data = (json.dumps(value) if not isinstance(value, str) else value).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def signed(self, path, params):
        timestamp = self.headers.get("X-Timestamp", "")
        nonce = self.headers.get("X-Nonce", "")
        canonical = self.command + "\n" + path + "\n"
        canonical += "".join(k + "=" + params[k] + "&" for k in sorted(params))
        canonical += "\n" + timestamp + "\n" + nonce
        expected = hmac.new(KEY, canonical.encode(), hashlib.sha256).hexdigest()
        valid = (hmac.compare_digest(expected, self.headers.get("X-Signature", ""))
                 and timestamp.isdigit() and abs(time.time() - int(timestamp)) < 60
                 and len(nonce) == 32 and nonce not in nonces)
        if valid:
            nonces.add(nonce)
        return valid

    def do_GET(self):
        path = urlsplit(self.path).path
        if not online:
            return self.reply({}, 503)
        if path == "/health":
            if not self.signed(path, {}):
                return self.reply({}, 403)
            return self.reply({"min_client_version": "9.0.0"})
        query = parse_qs(urlsplit(self.path).query)
        if query.get("access_code") != ["test-access"]:
            return self.reply({}, 403)
        if path == "/user":
            return self.reply(state)
        if path == "/poll_song_jump":
            return self.reply({"hash": "test-song"})
        self.reply({}, 404)

    def do_POST(self):
        global online
        parts = urlsplit(self.path)
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        payload = {k: v[0] for k, v in parse_qs(body).items()}
        query = {k: v[0] for k, v in parse_qs(parts.query).items()}
        if parts.path in ("/test/offline", "/test/online"):
            online = parts.path.endswith("/online")
            return self.reply({})
        if not online:
            return self.reply({}, 503)
        if parts.path == "/register_user":
            if not self.signed(parts.path, payload):
                return self.reply({}, 403)
            state["username"] = payload["username"]
            return self.reply("test-access")
        if query.get("access_code") != "test-access":
            return self.reply({}, 403)
        if parts.path == "/clear_import_flag":
            if not self.signed(parts.path, query):
                return self.reply({}, 403)
            state["import_requested"] = False
        elif parts.path == "/update_username":
            state["username"] = payload["username"]
        elif parts.path == "/update_costume":
            for field in ("chara_head_index", "chara_body_index", "chara_cos_index"):
                state[field] = int(payload[field])
            state["chara_is_costume"] = payload["chara_is_costume"] == "true"
        elif parts.path == "/submit_score":
            if not self.signed(parts.path, query):
                return self.reply({}, 403)
            valid = (json.loads(payload["input_log"]) == {"12.500000": 1}
                     and payload["played_at"] == "1700000000"
                     and json.loads(payload["modifiers"]) == {"auto_play": False}
                     and payload["chara_is_costume"] == "true" and payload["chara_cos_index"] == "5")
            if not valid:
                return self.reply({}, 400)
            time.sleep(0.4)  # Verify submit_score returns before the server replies.
            state["scores"] = [{k: (v if k == "hash" else int(v))
                                for k, v in query.items() if k != "access_code"}]
        else:
            return self.reply({}, 404)
        self.reply({})


if __name__ == "__main__":
    print("Network fixture listening on 127.0.0.1:18765", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 18765), Handler).serve_forever()
