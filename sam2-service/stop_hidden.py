from __future__ import annotations

import http.client
import time


def request_shutdown() -> bool:
    connection = http.client.HTTPConnection("127.0.0.1", 8765, timeout=0.5)
    try:
        connection.request(
            "POST",
            "/shutdown",
            body=b"{}",
            headers={"Content-Type": "application/json"},
        )
        response = connection.getresponse()
        response.read()
        return 200 <= response.status < 300
    except OSError:
        return False
    finally:
        connection.close()


def main() -> int:
    for _ in range(20):
        if request_shutdown():
            return 0
        time.sleep(0.25)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
