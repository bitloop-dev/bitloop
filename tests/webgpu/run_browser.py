"""Run the compiled compute test in a real Chromium browser, including its pthread worker."""
import argparse
import functools
import http.server
import json
import pathlib
import subprocess
import threading
import time
import urllib.parse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    build = pathlib.Path(args.build).resolve()
    output = pathlib.Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    completed = threading.Event()
    result = {"browser": args.browser, "build": str(build), "result": "timeout"}

    class Handler(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            super().end_headers()

        def log_message(self, *args):
            pass

        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path != "/result":
                self.send_error(404)
                return
            result["code"] = int(urllib.parse.parse_qs(parsed.query)["code"][0])
            result["result"] = "pass" if result["code"] == 0 else "fail"
            self.send_response(200)
            self.end_headers()
            completed.set()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(build)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    profile = build / ("browser-profile-" + time.strftime("%Y%m%d-%H%M%S"))
    arguments = [args.browser, "--headless=new", "--no-first-run", "--no-default-browser-check",
                 "--enable-logging=stderr", "--user-data-dir=" + str(profile),
                 f"http://127.0.0.1:{server.server_port}/bitloop_webgpu_test.html"]
    started = time.monotonic()
    try:
        with output.with_suffix(".browser.log").open("w") as log:
            process = subprocess.Popen(arguments, stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                completed.wait(45)
            finally:
                process.terminate()
                process.wait(timeout=5)
    finally:
        server.shutdown()
        server.server_close()
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
