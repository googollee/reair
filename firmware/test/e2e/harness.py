"""两块板的端到端测试工具:串口、Matter(通过 matter-server)。

同一个二层网络里只有这两块板,所以一块板用 `reair raw` 命令发原始报文,另一块板的日志当观察端。

依赖 ESP-IDF 的 Python 环境(含 pyserial、websockets):
  ~/.espressif/python_env/idf6.0_py3.14_env/bin/python run_sync_tests.py
matter-server 需要先转发:kubectl -n infra port-forward svc/matter-server 15580:5580
"""

import json
import re
import threading
import time
from collections import deque

import serial
from websockets.sync.client import connect

# 模式取值(与固件 node_endpoints.h 一致)
ROLE_LEADER, ROLE_FOLLOWER, ROLE_INDEPENDENT = 0, 1, 2
ACTUAL_INTAKE, ACTUAL_EXHAUST, ACTUAL_STOPPED = 0, 1, 2
EP_FAN, EP_PERIOD, EP_ROLE, EP_FWD, EP_ACTUAL, EP_WARN, EP_DIR = 1, 2, 3, 4, 5, 6, 7
DIR_FWD, DIR_REV, DIR_ALT = 0, 1, 2  # 风向模式:固定正向、固定反向、往复
FWD_INTAKE, FWD_EXHAUST = 0, 1
FAN_CLUSTER = 514
ATTR_FAN_MODE, ATTR_PERCENT, ATTR_PERCENT_CUR = 0, 2, 3


def now():
    return time.monotonic()


class Board:
    """一块板:串口读线程 + 控制台命令。串口只打开一次,避免反复复位。"""

    def __init__(self, name, port, node_id):
        self.name, self.port, self.node_id = name, port, node_id
        self.serial = serial.Serial(port, 115200, timeout=0.1)
        self.serial.dtr = False
        self.serial.rts = False
        self.lines = deque(maxlen=20000)  # (t, text)
        self.lock = threading.Lock()
        self.stop = False
        self.mac = None
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while not self.stop:
            try:
                data = self.serial.read(4096)
            except Exception:
                time.sleep(0.2)
                continue
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").rstrip("\r")
                with self.lock:
                    self.lines.append((now(), text))

    def snapshot(self):
        with self.lock:
            return list(self.lines)

    def find(self, pattern, since=0.0):
        rx = re.compile(pattern)
        return [(t, l) for t, l in self.snapshot() if t >= since and rx.search(l)]

    def command(self, text):
        self.serial.write((text + "\r\n").encode())

    def state(self, timeout=4.0):
        t0 = now()
        self.command("matter esp reair state")
        while now() - t0 < timeout:
            hits = self.find(r"^STATE ", since=t0)
            if hits:
                return parse_state(hits[-1][1])
            time.sleep(0.05)
        raise TimeoutError(f"{self.name}: no STATE response")

    def reset(self):
        """复位(RTS 脉冲),等待启动完成并联网。返回启动开始的时刻。"""
        t0 = now()
        self.serial.rts = True
        time.sleep(0.1)
        self.serial.rts = False
        deadline = now() + 45
        while now() < deadline:
            if self.find(r"reair\.sync: sync started", since=t0):
                break
            time.sleep(0.2)
        else:
            raise TimeoutError(f"{self.name}: no boot")
        # Wi-Fi 连上之后同步才有意义
        while now() < deadline:
            if self.find(r"IP address changed|Connecting_Succeeded -> Connected", since=t0):
                break
            time.sleep(0.2)
        time.sleep(1.5)
        self.mac = None
        return t0

    def get_mac(self):
        if self.mac is None:
            self.mac = bytes.fromhex(self.state()["mac"])
        return self.mac

    def set_loss(self, tx, rx):
        self.command(f"matter esp reair loss {tx} {rx}")
        time.sleep(0.3)

    def raw(self, data):
        """让这块板原样发出一条报文(任意字节,空报文传 b"")"""
        self.command("matter esp reair raw" + (" " + data.hex() if data else ""))
        time.sleep(0.2)

    def set_cycle(self, seconds):
        self.command(f"matter esp reair cycle {seconds}")
        time.sleep(0.4)


def parse_state(line):
    out = {}
    for token in line.split()[1:]:
        key, _, val = token.partition("=")
        out[key] = val if key == "mac" else int(val)
    return out


class Matter:
    """通过 matter-server 的 websocket 读写节点。"""

    def __init__(self, url="ws://localhost:15580/ws"):
        self.ws = connect(url, max_size=None)
        self.ws.recv()  # server info
        self.mid = 0

    def call(self, command, args, timeout=40):
        self.mid += 1
        mid = str(self.mid)
        self.ws.send(json.dumps({"message_id": mid, "command": command, "args": args}))
        deadline = now() + timeout
        while now() < deadline:
            try:
                msg = json.loads(self.ws.recv(timeout=max(0.1, deadline - now())))
            except TimeoutError:
                break
            if msg.get("message_id") == mid:
                return msg
        raise TimeoutError(f"matter-server: {command} timeout")

    def read(self, node, paths):
        result = self.call("read_attribute", {"node_id": node, "attribute_path": paths})
        if "result" not in result:
            raise RuntimeError(result.get("details", result))
        return result["result"]

    def cached(self, node, path):
        """matter-server 缓存(由订阅更新)里的值"""
        result = self.call("get_node", {"node_id": node})
        return result.get("result", {}).get("attributes", {}).get(path)

    def write(self, node, path, value):
        """返回状态码,0 为成功"""
        result = self.call("write_attribute", {"node_id": node, "attribute_path": path, "value": value})
        if "result" in result and result["result"]:
            return result["result"][0].get("Status")
        return result.get("details", "error")

    def mode(self, node, endpoint, new_mode):
        """ModeSelect 的 ChangeToMode。成功返回 'ok',失败返回错误文字"""
        result = self.call(
            "device_command",
            {"node_id": node, "endpoint_id": endpoint, "cluster_id": 80, "command_name": "ChangeToMode",
             "payload": {"newMode": new_mode}},
        )
        if "result" in result:
            return "ok"
        return result.get("details", "error")

    # 便捷封装
    def fan(self, node, tier):
        return self.write(node, f"{EP_FAN}/{FAN_CLUSTER}/{ATTR_FAN_MODE}", tier)

    def direction(self, node, mode):
        return self.mode(node, EP_DIR, mode)

    def role(self, node, role):
        return self.mode(node, EP_ROLE, role)

    def period(self, node, mode):
        return self.mode(node, EP_PERIOD, mode)

    def forward_direction(self, node, mode):
        return self.mode(node, EP_FWD, mode)

    def clear_warning(self, node):
        return self.mode(node, EP_WARN, 0)

    def fan_view(self, node):
        r = self.read(node, [f"{EP_FAN}/{FAN_CLUSTER}/{a}" for a in (ATTR_FAN_MODE, ATTR_PERCENT, ATTR_PERCENT_CUR)]
                      + [f"{EP_ACTUAL}/80/3"])
        return {
            "mode": r[f"{EP_FAN}/{FAN_CLUSTER}/{ATTR_FAN_MODE}"],
            "percent": r[f"{EP_FAN}/{FAN_CLUSTER}/{ATTR_PERCENT}"],
            "current": r[f"{EP_FAN}/{FAN_CLUSTER}/{ATTR_PERCENT_CUR}"],
            "actual": r[f"{EP_ACTUAL}/80/3"],
        }


def build_packet(mac, reverse, tier, version=1, low_bits=0):
    return bytes([version]) + bytes(mac) + bytes([(0x80 if reverse else 0) | ((tier & 7) << 4) | (low_bits & 0x0F)])


FAKE_MAC = bytes.fromhex("020000000099")


def wait_for(predicate, timeout, interval=0.25):
    """轮询到 predicate() 为真,返回耗时;超时返回 None"""
    t0 = now()
    while now() - t0 < timeout:
        try:
            if predicate():
                return now() - t0
        except TimeoutError:
            pass
        time.sleep(interval)
    return None
