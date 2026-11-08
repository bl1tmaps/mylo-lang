#!/usr/bin/env python3
"""Drives `mylo --dap` through real debugging sessions and checks the replies.

Usage: python tests/test_dap.py path/to/mylo[.exe]

Each case writes a small script to a temporary folder, starts the adapter on it, and talks
to it over the Debug Adapter Protocol the way an editor would.
"""

import json
import os
import subprocess
import sys
import tempfile
import threading
import queue
import time

MYLO = None
TIMEOUT = 10.0


class Session:
    def __init__(self, script_path, *extra_args):
        self.proc = subprocess.Popen(
            [MYLO, *extra_args, "--dap", script_path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.seq = 1
        self.messages = queue.Queue()
        self.events = []
        self.output = []
        self.stderr = []
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stdout(self):
        stream = self.proc.stdout
        while True:
            header = b""
            while not header.endswith(b"\r\n\r\n"):
                ch = stream.read(1)
                if not ch:
                    self.messages.put(None)
                    return
                header += ch
            length = 0
            for line in header.decode().split("\r\n"):
                if line.lower().startswith("content-length:"):
                    length = int(line.split(":")[1])
            body = stream.read(length)
            self.messages.put(json.loads(body))

    def _read_stderr(self):
        for line in self.proc.stderr:
            self.stderr.append(line.decode(errors="replace"))

    def send(self, command, arguments=None):
        seq = self.seq
        self.seq += 1
        msg = {"seq": seq, "type": "request", "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        data = json.dumps(msg).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.proc.stdin.flush()
        return seq

    def _next(self, deadline):
        remaining = deadline - time.time()
        if remaining <= 0:
            raise AssertionError("timed out waiting for the adapter")
        try:
            msg = self.messages.get(timeout=remaining)
        except queue.Empty:
            raise AssertionError("timed out waiting for the adapter")
        if msg is None:
            raise AssertionError("the adapter closed its output. stderr:\n" + "".join(self.stderr))
        if msg.get("type") == "event":
            self.events.append(msg)
            if msg["event"] == "output":
                self.output.append(msg["body"])
        return msg

    def request(self, command, arguments=None, expect_success=True):
        seq = self.send(command, arguments)
        deadline = time.time() + TIMEOUT
        while True:
            msg = self._next(deadline)
            if msg.get("type") == "response" and msg.get("request_seq") == seq:
                if expect_success and not msg.get("success"):
                    raise AssertionError(f"{command} failed: {msg.get('message')}")
                return msg

    def wait_event(self, name):
        for e in self.events:
            if e["event"] == name and not e.get("_seen"):
                e["_seen"] = True
                return e
        deadline = time.time() + TIMEOUT
        while True:
            msg = self._next(deadline)
            if msg.get("type") == "event" and msg["event"] == name:
                msg["_seen"] = True
                return msg

    def stdout_text(self):
        return "".join(o["output"] for o in self.output if o.get("category") == "stdout")

    def start(self, breakpoints=None, stop_on_entry=False, path=None, no_debug=False):
        self.request("initialize", {"adapterID": "mylo", "linesStartAt1": True})
        self.request("launch", {"stopOnEntry": stop_on_entry, "noDebug": no_debug})
        self.wait_event("initialized")
        if breakpoints is not None:
            r = self.request("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": l} for l in breakpoints]})
            self.breakpoint_reply = r["body"]["breakpoints"]
        self.request("configurationDone")

    def close(self):
        try:
            self.request("disconnect")
        except AssertionError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def write_script(folder, name, text):
    path = os.path.join(folder, name)
    with open(path, "w", newline="\n") as f:
        f.write(text)
    return path


def top_frame(s):
    frames = s.request("stackTrace", {"threadId": 1})["body"]["stackFrames"]
    return frames


def variables(s, ref):
    return {v["name"]: v for v in s.request("variables", {"variablesReference": ref})["body"]["variables"]}


def locals_of(s, frame_id=1):
    scopes = s.request("scopes", {"frameId": frame_id})["body"]["scopes"]
    return variables(s, scopes[0]["variablesReference"]), variables(s, scopes[1]["variablesReference"])


PROGRAM = """struct Point {
    x: f64
    y: f64
}

fn add(a: i32, b: i32) -> i32 {
    total: i32 = a + b
    ret total
}

fn outer(n: i32) -> i32 {
    doubled: i32 = add(n, n)
    ret doubled + 1
}

name: string = "mylo"
nums: i32[] = [1, 2, 3]
p: Point = {x = 1.5, y = 2}
result: i32 = outer(4)
print(result)
print(name)
"""


def case_breakpoints_and_variables(folder):
    path = write_script(folder, "main.mylo", PROGRAM)
    s = Session(path)
    try:
        # Line 15 is blank, so that breakpoint moves to the next line with code.
        s.start(breakpoints=[7, 15], path=path)
        assert s.breakpoint_reply[0]["verified"] and s.breakpoint_reply[0]["line"] == 7, s.breakpoint_reply
        assert s.breakpoint_reply[1]["verified"] and s.breakpoint_reply[1]["line"] == 16, s.breakpoint_reply
        stopped = s.wait_event("stopped")
        assert stopped["body"]["reason"] == "breakpoint", stopped
        frames = top_frame(s)
        assert [f["name"] for f in frames] == ["main"] and frames[0]["line"] == 16, frames
        _, global_vars = locals_of(s)
        assert "result" not in global_vars, global_vars   # not declared yet
        s.request("continue", {"threadId": 1})
        stopped = s.wait_event("stopped")
        assert stopped["body"]["reason"] == "breakpoint", stopped
        frames = top_frame(s)
        names = [f["name"] for f in frames]
        assert names == ["add", "outer", "main"], names
        assert frames[0]["line"] == 7 and frames[1]["line"] == 12 and frames[2]["line"] == 19, frames
        assert frames[0]["source"]["path"].lower().replace("\\", "/").endswith("main.mylo"), frames[0]

        local_vars, global_vars = locals_of(s, frames[0]["id"])
        assert local_vars["a"]["value"] == "4" and local_vars["b"]["value"] == "4", local_vars
        outer_locals, _ = locals_of(s, frames[1]["id"])
        assert outer_locals["n"]["value"] == "4", outer_locals
        assert global_vars["name"]["value"] == '"mylo"', global_vars
        assert global_vars["nums"]["variablesReference"] > 0, global_vars["nums"]
        elements = variables(s, global_vars["nums"]["variablesReference"])
        assert [elements[f"[{i}]"]["value"] for i in range(3)] == ["1", "2", "3"], elements
        fields = variables(s, global_vars["p"]["variablesReference"])
        assert fields["x"]["value"] == "1.5" and fields["y"]["value"] == "2", fields

        r = s.request("evaluate", {"expression": "p.x", "frameId": frames[0]["id"]})
        assert r["body"]["result"] == "1.5", r
        r = s.request("evaluate", {"expression": "nums[2]", "frameId": frames[0]["id"]})
        assert r["body"]["result"] == "3", r
        r = s.request("evaluate", {"expression": "nope", "frameId": 1}, expect_success=False)
        assert not r["success"]

        # Step over the addition, then out of add() back to outer().
        s.request("next", {"threadId": 1})
        assert s.wait_event("stopped")["body"]["reason"] == "step"
        assert top_frame(s)[0]["line"] == 8
        s.request("stepOut", {"threadId": 1})
        s.wait_event("stopped")
        frames = top_frame(s)
        assert frames[0]["name"] == "outer", frames

        s.request("continue", {"threadId": 1})
        exited = s.wait_event("exited")
        assert exited["body"]["exitCode"] == 0
        s.wait_event("terminated")
        assert s.stdout_text() == "9\nmylo\n", repr(s.stdout_text())
    finally:
        s.close()


def case_step_in_and_entry(folder):
    path = write_script(folder, "main.mylo", PROGRAM)
    s = Session(path)
    try:
        s.start(stop_on_entry=True)
        assert s.wait_event("stopped")["body"]["reason"] == "entry"
        lines = []
        for _ in range(12):
            s.request("stepIn", {"threadId": 1})
            ev = s.wait_event("stopped")
            frame = top_frame(s)[0]
            lines.append((frame["name"], frame["line"]))
            if frame["name"] == "add":
                break
        assert lines[0] == ("main", 17), lines   # the entry stop was on line 16
        assert ("outer", 12) in lines and ("add", 7) in lines, lines
        assert ("outer", 11) not in lines and ("add", 6) not in lines, lines   # headers are skipped
    finally:
        s.close()


def case_memory_requests(folder):
    path = write_script(folder, "main.mylo", PROGRAM)
    s = Session(path, "-X", "stack_size=4k")
    try:
        s.start(breakpoints=[8], path=path)
        s.wait_event("stopped")
        state = s.request("myloVmState")["body"]
        assert state["stackCapacity"] == 4096, state
        assert state["strCount"] >= 1 and state["maxStrings"] == 10000, state
        assert state["depth"] == 3 and state["arenas"][0]["id"] == 0, state
        stack = s.request("myloStack", {"start": 0, "count": 100})["body"]
        assert stack["total"] == state["sp"] + 1 and len(stack["frames"]) == 3, stack
        arena = s.request("myloArena", {"arena": 0, "start": 0, "count": 16})["body"]
        assert len(arena["slots"]) == 16 and arena["head"] > 0, arena
        strings = s.request("myloStrings", {"start": 0, "count": 50})["body"]
        assert any(x["text"] == "mylo" for x in strings["strings"]), strings
        code = s.request("myloDisassemble", {"count": 20})["body"]
        assert any(i["address"] == code["ip"] for i in code["instructions"]), code
        s.request("continue", {"threadId": 1})
        s.wait_event("terminated")
    finally:
        s.close()


def case_runtime_error(folder):
    path = write_script(folder, "boom.mylo", "fn f(n: i32) -> i32 {\n  ret f(n + 1)\n}\nprint(f(0))\n")
    s = Session(path, "-X", "stack_size=256")
    try:
        s.start()
        stopped = s.wait_event("stopped")
        assert stopped["body"]["reason"] == "exception", stopped
        assert "stack_size" in stopped["body"]["text"], stopped
        frames = top_frame(s)
        assert frames[0]["name"] == "f" and len(frames) > 10, frames[:3]
        s.request("continue", {"threadId": 1})
        assert s.wait_event("exited")["body"]["exitCode"] == 1
    finally:
        s.close()


def case_compile_error(folder):
    path = write_script(folder, "bad.mylo", "x: i32 = 1\nprint(x\n")
    s = Session(path)
    try:
        s.request("initialize", {"adapterID": "mylo"})
        s.request("launch", {"stopOnEntry": False})
        err = s.wait_event("myloCompileError")
        assert err["body"]["line"] >= 2 and err["body"]["message"], err
        assert s.wait_event("exited")["body"]["exitCode"] == 1
    finally:
        s.close()


def case_pause(folder):
    path = write_script(folder, "spin.mylo", "i: i32 = 0\nforever {\n  i = i + 1\n}\n")
    s = Session(path)
    try:
        s.start()
        time.sleep(0.3)
        s.request("pause", {"threadId": 1})
        assert s.wait_event("stopped")["body"]["reason"] == "pause"
        _, global_vars = locals_of(s)
        assert int(global_vars["i"]["value"]) > 0, global_vars
    finally:
        s.close()


def case_imports_have_their_own_lines(folder):
    lib = write_script(folder, "lib.mylo", "fn helper() -> i32 {\n  v: i32 = 41\n  ret v + 1\n}\n")
    path = write_script(folder, "main.mylo", 'import "lib.mylo"\nx: i32 = 0\nx = helper()\nprint(x)\n')
    s = Session(path)
    try:
        s.request("initialize", {"adapterID": "mylo"})
        s.request("launch", {"stopOnEntry": False})
        s.wait_event("initialized")
        s.request("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": 3}]})
        r = s.request("setBreakpoints", {"source": {"path": lib}, "breakpoints": [{"line": 3}]})
        assert r["body"]["breakpoints"][0]["verified"], r
        s.request("configurationDone")
        s.wait_event("stopped")
        f = top_frame(s)[0]
        assert f["line"] == 3 and f["source"]["path"].lower().endswith("main.mylo"), f
        s.request("continue", {"threadId": 1})
        s.wait_event("stopped")
        f = top_frame(s)[0]
        assert f["line"] == 3 and f["source"]["path"].lower().endswith("lib.mylo") and f["name"] == "helper", f
        s.request("continue", {"threadId": 1})
        s.wait_event("terminated")
        assert s.stdout_text() == "42\n", s.stdout_text()
    finally:
        s.close()


def case_no_debug(folder):
    path = write_script(folder, "main.mylo", PROGRAM)
    s = Session(path)
    try:
        s.start(breakpoints=[7], path=path, no_debug=True)
        assert s.wait_event("exited")["body"]["exitCode"] == 0
        assert not any(e["event"] == "stopped" for e in s.events)
    finally:
        s.close()


def main():
    global MYLO
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    MYLO = os.path.abspath(sys.argv[1])
    cases = [v for k, v in sorted(globals().items()) if k.startswith("case_")]
    failed = 0
    for case in cases:
        with tempfile.TemporaryDirectory() as folder:
            try:
                case(folder)
                print(f"[PASS] {case.__name__}")
            except Exception as e:
                failed += 1
                print(f"[FAIL] {case.__name__}: {e}")
    print(f"{len(cases) - failed} of {len(cases)} DAP cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
