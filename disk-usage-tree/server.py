"""
Disk Usage Tree - 로컬 웹 기반 디스크 사용량 트리 뷰어

루트 디렉토리(예: C:\\)부터 모든 하위 디렉토리가 차지하는 용량을 계산하여
브라우저에서 트리 형태로 보여줍니다. Python 표준 라이브러리만 사용합니다.

사용법:
    python server.py                 # http://127.0.0.1:8765 에서 실행 후 브라우저 자동 열기
    python server.py --port 9000     # 포트 변경
    python server.py --no-browser    # 브라우저 자동 열기 끄기
"""

import argparse
import json
import os
import shutil
import stat
import string
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

IS_WINDOWS = os.name == "nt"
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
STATIC_DIR = Path(__file__).resolve().parent / "static"


# --------------------------------------------------------------------------
# 디렉토리 트리 모델
# --------------------------------------------------------------------------
class Node:
    """디렉토리 하나. 하위 디렉토리만 노드로 저장하고 파일은 합계만 보관한다."""

    __slots__ = ("name", "parent", "children", "file_size", "file_count",
                 "size", "total_files", "total_dirs", "errors")

    def __init__(self, name, parent=None):
        self.name = name
        self.parent = parent
        self.children = []
        self.file_size = 0      # 이 디렉토리에 직접 들어있는 파일들의 크기
        self.file_count = 0
        self.size = 0           # 하위 전체 포함 크기
        self.total_files = 0
        self.total_dirs = 0
        self.errors = 0         # 접근 거부 등으로 읽지 못한 항목 수

    def path(self):
        parts = []
        node = self
        while node is not None:
            parts.append(node.name)
            node = node.parent
        parts.reverse()
        return os.path.join(*parts) if parts else ""


def is_link_or_junction(entry):
    """심볼릭 링크 / 정션(reparse point)은 따라가지 않는다 (중복 계산·무한루프 방지)."""
    try:
        if entry.is_symlink():
            return True
        if IS_WINDOWS:
            attrs = entry.stat(follow_symlinks=False).st_file_attributes
            return bool(attrs & FILE_ATTRIBUTE_REPARSE_POINT)
    except OSError:
        return True
    return False


class Scanner:
    def __init__(self):
        self.lock = threading.Lock()
        self.root = None
        self.root_path = None
        self.running = False
        self.cancel_requested = False
        self.started_at = None
        self.finished_at = None
        self.scanned_files = 0
        self.scanned_dirs = 0
        self.scanned_bytes = 0
        self.current_path = ""
        self.error = None
        self.index = {}  # 정규화된 경로 -> Node

    # ---- 스캔 -----------------------------------------------------------
    def start(self, root_path):
        with self.lock:
            if self.running:
                return False, "이미 스캔이 진행 중입니다."
            root_path = os.path.abspath(root_path)
            if not os.path.isdir(root_path):
                return False, f"디렉토리가 아닙니다: {root_path}"
            self.root_path = root_path
            self.root = None
            self.index = {}
            self.running = True
            self.cancel_requested = False
            self.started_at = time.time()
            self.finished_at = None
            self.scanned_files = self.scanned_dirs = self.scanned_bytes = 0
            self.current_path = root_path
            self.error = None
        threading.Thread(target=self._run, args=(root_path,), daemon=True).start()
        return True, None

    def cancel(self):
        self.cancel_requested = True

    def _run(self, root_path):
        try:
            root = Node(root_path)
            index = {self._key(root_path): root}
            order = []          # 방문 순서 (역순으로 합산하면 후위순회와 동일)
            stack = [root]
            while stack:
                if self.cancel_requested:
                    raise InterruptedError("사용자가 스캔을 취소했습니다.")
                node = stack.pop()
                order.append(node)
                dir_path = node.path()
                self.current_path = dir_path
                try:
                    with os.scandir(dir_path) as it:
                        for entry in it:
                            try:
                                if entry.is_dir(follow_symlinks=False):
                                    if is_link_or_junction(entry):
                                        continue
                                    child = Node(entry.name, node)
                                    node.children.append(child)
                                    index[self._key(os.path.join(dir_path, entry.name))] = child
                                    stack.append(child)
                                    self.scanned_dirs += 1
                                else:
                                    st = entry.stat(follow_symlinks=False)
                                    if stat.S_ISREG(st.st_mode):
                                        node.file_size += st.st_size
                                        node.file_count += 1
                                        self.scanned_files += 1
                                        self.scanned_bytes += st.st_size
                            except OSError:
                                node.errors += 1
                except OSError:
                    node.errors += 1

            # 하위에서 위로 크기 합산
            for node in reversed(order):
                node.size += node.file_size
                node.total_files += node.file_count
                if node.parent is not None:
                    p = node.parent
                    p.size += node.size
                    p.total_files += node.total_files
                    p.total_dirs += node.total_dirs + 1
                    p.errors += node.errors
                node.children.sort(key=lambda c: c.size, reverse=True)

            with self.lock:
                self.root = root
                self.index = index
        except Exception as exc:  # noqa: BLE001
            self.error = str(exc)
        finally:
            self.running = False
            self.finished_at = time.time()

    @staticmethod
    def _key(path):
        return os.path.normcase(os.path.normpath(path))

    # ---- 조회 -----------------------------------------------------------
    def status(self):
        end = self.finished_at or time.time()
        return {
            "running": self.running,
            "rootPath": self.root_path,
            "ready": self.root is not None,
            "scannedFiles": self.scanned_files,
            "scannedDirs": self.scanned_dirs,
            "scannedBytes": self.scanned_bytes,
            "currentPath": self.current_path if self.running else "",
            "elapsed": round(end - self.started_at, 1) if self.started_at else 0,
            "error": self.error,
        }

    def children(self, path, limit):
        with self.lock:
            node = self.root if path is None else self.index.get(self._key(path))
        if node is None:
            return None
        items = [self._describe(c, node.size) for c in node.children[:limit]]
        rest = node.children[limit:]
        return {
            "node": self._describe(node, node.parent.size if node.parent else node.size),
            "children": items,
            "hiddenCount": len(rest),
            "hiddenSize": sum(c.size for c in rest),
            "filesHere": {"size": node.file_size, "count": node.file_count},
        }

    @staticmethod
    def _describe(node, parent_size):
        return {
            "name": node.name,
            "path": node.path(),
            "size": node.size,
            "files": node.total_files,
            "dirs": node.total_dirs,
            "errors": node.errors,
            "hasChildren": bool(node.children) or node.file_count > 0,
            "percent": (node.size / parent_size * 100) if parent_size else 0,
        }


def list_roots():
    if IS_WINDOWS:
        if hasattr(os, "listdrives"):  # Python 3.12+
            drives = os.listdrives()
        else:
            drives = [f"{d}:\\" for d in string.ascii_uppercase if os.path.exists(f"{d}:\\")]
    else:
        drives = ["/"]
    result = []
    for d in drives:
        try:
            u = shutil.disk_usage(d)
            result.append({"path": d, "total": u.total, "used": u.used, "free": u.free})
        except OSError:
            result.append({"path": d, "total": None, "used": None, "free": None})
    return result


# --------------------------------------------------------------------------
# HTTP 서버
# --------------------------------------------------------------------------
SCANNER = Scanner()
CONTENT_TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
                 ".css": "text/css; charset=utf-8"}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # 콘솔 로그 최소화
        pass

    def _json(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _static(self, rel):
        file = (STATIC_DIR / rel).resolve()
        if STATIC_DIR not in file.parents or not file.is_file():
            self.send_error(404)
            return
        body = file.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", CONTENT_TYPES.get(file.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        qs = parse_qs(url.query)
        if url.path == "/":
            return self._static("index.html")
        if url.path == "/api/roots":
            roots = list_roots()
            return self._json({"roots": roots, "default": roots[0]["path"] if roots else ""})
        if url.path == "/api/status":
            return self._json(SCANNER.status())
        if url.path == "/api/children":
            path = qs.get("path", [None])[0]
            limit = int(qs.get("limit", ["200"])[0])
            data = SCANNER.children(path, max(1, limit))
            if data is None:
                return self._json({"error": "스캔 결과가 없거나 경로를 찾을 수 없습니다."}, 404)
            return self._json(data)
        if url.path.startswith("/static/"):
            return self._static(url.path[len("/static/"):])
        self.send_error(404)

    def do_POST(self):
        # 다른 사이트에서의 요청(CSRF)을 막기 위해 Origin 확인
        origin = self.headers.get("Origin")
        host = self.headers.get("Host", "")
        if origin and urlparse(origin).netloc != host:
            return self._json({"error": "forbidden"}, 403)
        url = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        payload = json.loads(self.rfile.read(length) or b"{}") if length else {}
        if url.path == "/api/scan":
            ok, err = SCANNER.start(payload.get("path") or "")
            return self._json({"ok": ok, "error": err}, 200 if ok else 400)
        if url.path == "/api/cancel":
            SCANNER.cancel()
            return self._json({"ok": True})
        if url.path == "/api/open":
            # 탐색기에서 폴더 열기 (Windows 전용)
            path = payload.get("path") or ""
            if IS_WINDOWS and os.path.isdir(path):
                os.startfile(path)  # noqa: S606
                return self._json({"ok": True})
            return self._json({"ok": False, "error": "열 수 없습니다."}, 400)
        self.send_error(404)


def main():
    ap = argparse.ArgumentParser(description="디스크 사용량 트리 뷰어 (로컬 웹)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://{args.host}:{args.port}/"
    print(f"Disk Usage Tree 실행 중: {url}  (종료: Ctrl+C)")
    if not args.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n종료합니다.")
    finally:
        server.server_close()


if __name__ == "__main__":
    sys.exit(main())
