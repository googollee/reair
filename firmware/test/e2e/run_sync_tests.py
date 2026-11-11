"""UDP 同步的端到端测试(两块板 + 主机探针)。

用法:
  ~/.espressif/python_env/idf6.0_py3.14_env/bin/python run_sync_tests.py [--only P,X] [--soak 600]

板子 A、B 的串口和 matter-server 节点号在下面的 BOARDS 里改。结果写入 out/sync_test_report.md。
"""

import argparse
import random
import sys
import time
import traceback

from harness import *  # noqa: F401,F403

BOARDS = {"A": ("/dev/cu.usbmodem20143201", 13), "B": ("/dev/cu.usbmodem20143301", 14)}
TIER_PERCENT = {0: 0, 1: 33, 2: 66, 3: 100}
PERIOD = 5.0  # 同步周期(秒)
SETTLE = 1.0

TESTS = []


def test(name):
    def wrap(fn):
        TESTS.append((name, fn))
        return fn

    return wrap


def must(response):
    """准备步骤必须成功,否则测试结论没有意义"""
    assert response in (0, "ok"), f"准备步骤失败: {response}"
    return response


def expect_actual(tier, reverse, fwd):
    if tier == 0:
        return ACTUAL_STOPPED
    return ACTUAL_EXHAUST if bool(reverse) != (fwd == FWD_EXHAUST) else ACTUAL_INTAKE


class Ctx:
    def __init__(self):
        self.A = Board("A", *BOARDS["A"])
        self.B = Board("B", *BOARDS["B"])
        self.M = Matter()
        self.failures = []
        self.notes = []
        # 等两块板启动并联网(打开串口会复位)
        time.sleep(20)
        for b in (self.A, self.B):
            b.get_mac()

    def check(self, cond, message):
        if not cond:
            self.failures.append(message)
        return bool(cond)

    def note(self, text):
        self.notes.append(text)
        print("   ·", text)

    def reboot(self, board):
        """复位并等到 Matter 会话恢复,否则紧接着的 Matter 命令会悄悄失败"""
        t = board.reset()

        def reachable():
            try:
                self.M.read(board.node_id, ["0/40/3"])
                return True
            except Exception:
                return False

        assert wait_for(reachable, 70, 1.0) is not None, f"{board.name}: 重启后 Matter 不可达"
        time.sleep(1.0)
        return t

    def other(self, board):
        return self.B if board is self.A else self.A

    def inject(self, dut, raw):
        """另一块板向 dut 发一条原始报文"""
        self.other(dut).raw(raw)

    def board_by_mac(self, mac):
        return self.A if mac == self.A.get_mac() else self.B

    def normalize(self):
        """两块板都回到:独立、风力关、固定正向、周期 60 秒、正向风向进气、无警告、无测试覆盖"""
        M = self.M
        for b in (self.A, self.B):
            b.set_loss(0, 0)
            b.set_cycle(0)
        for b in (self.A, self.B):
            n = b.node_id
            assert M.role(n, ROLE_INDEPENDENT) == "ok", f"{b.name}: role"
            M.direction(n, DIR_FWD)
            M.fan(n, 0)
            M.period(n, 0)
            M.forward_direction(n, FWD_INTAKE)
            M.clear_warning(n)
        time.sleep(SETTLE)
        for b in (self.A, self.B):
            st = b.state()
            ok = (st["role"], st["tier"], st["rev"], st["actual"], st["warn"], st["mode"], st["fwd"], st["cycle"]) == (
                ROLE_INDEPENDENT, 0, 0, ACTUAL_STOPPED, 0, 0, 0, 60)
            assert ok, f"{b.name}: normalize failed: {st}"

    def settle(self, seconds=SETTLE):
        time.sleep(seconds)


def sync_events(src, dst, since):
    """用日志统计 src 发出的每条报文被 dst 收到的情况。返回 [(t_tx, rev, tier, t_rx 或 None)]"""
    tx_re = r"SYNC-TX rev=(\d) tier=(\d)"
    import re

    mac = src.get_mac().hex()
    rx_lines = []
    for t, line in dst.find(r"SYNC-RX", since=since):
        m = re.search(r"mac=([0-9a-f]{12}) rev=(\d) tier=(\d)", line)
        if m and m.group(1) == mac:
            rx_lines.append((t, int(m.group(2)), int(m.group(3))))
    result = []
    for t, line in src.find(tx_re, since=since):
        m = re.search(tx_re, line)
        rev, tier = int(m.group(1)), int(m.group(2))
        got = None
        for rt, rrev, rtier in rx_lines:
            if rt >= t - 0.05 and (rrev, rtier) == (rev, tier):
                got = rt
                break
        result.append((t, rev, tier, got))
    return result


def flips(events):
    """从 TX 事件里找出风向翻转的时刻(相对上一条报文方向变化)"""
    out = []
    for i in range(1, len(events)):
        if events[i][1] != events[i - 1][1]:
            out.append(events[i])
    return out


# ---------------------------------------------------------------- 报文与接收

@test("P1 从节点应用全部取值(方向×风力×正向风向)")
def p1(T):
    T.normalize()
    A, M = T.A, T.M
    M.role(A.node_id, ROLE_FOLLOWER)
    T.settle()
    for fwd in (FWD_INTAKE, FWD_EXHAUST):
        M.forward_direction(A.node_id, fwd)
        for rev in (0, 1):
            for tier in (0, 1, 2, 3):
                T.inject(A, build_packet(FAKE_MAC, rev, tier))

                def matched():
                    st = A.state()
                    return (st["tier"], st["rev"]) == (tier, rev)

                took = wait_for(matched, 3)
                T.check(took is not None, f"fwd={fwd} rev={rev} tier={tier}: 未应用")
                if took is not None:
                    st = A.state()
                    T.check(st["actual"] == expect_actual(tier, rev, fwd),
                            f"fwd={fwd} rev={rev} tier={tier}: 实际风向 {st['actual']}")
    view = M.fan_view(A.node_id)
    T.check(view["mode"] == 3 and view["percent"] == 100 and view["current"] == 100,
            f"Matter 风扇属性与同步结果不一致: {view}")


@test("P2 非法报文整个丢弃(长度、版本、保留风力)")
def p2(T):
    T.normalize()
    A, M = T.A, T.M
    M.role(A.node_id, ROLE_FOLLOWER)
    T.settle()
    T.inject(A, build_packet(FAKE_MAC, 1, 2))
    wait_for(lambda: A.state()["tier"] == 2, 3)
    before = A.state()
    good = build_packet(FAKE_MAC, 0, 3)
    bad = []
    for length in (0, 1, 7, 9, 16):
        bad.append(good[:length] if length <= 8 else good + bytes(length - 8))
    for version in (0, 2, 255):
        bad.append(build_packet(FAKE_MAC, 0, 3, version=version))
    for tier in (4, 5, 6, 7):
        bad.append(build_packet(FAKE_MAC, 0, tier))
    for packet in bad:
        T.inject(A, packet)
        time.sleep(0.08)
    T.settle(1.5)
    after = A.state()
    T.check((after["tier"], after["rev"]) == (2, 1), f"状态被非法报文改变: {after['tier']},{after['rev']}")
    T.check(after["inv"] - before["inv"] == len(bad), f"丢弃计数 {after['inv'] - before['inv']} != {len(bad)}")
    T.check(after["rx"] == before["rx"], "非法报文被计入合法接收")


@test("P3 保留位被忽略;P4 无序号,最后一条生效")
def p3(T):
    T.normalize()
    A, M = T.A, T.M
    M.role(A.node_id, ROLE_FOLLOWER)
    T.settle()
    for low in (0x1, 0x8, 0xF):
        T.inject(A, build_packet(FAKE_MAC, 1, 3, low_bits=low))
        took = wait_for(lambda: (A.state()["tier"], A.state()["rev"]) == (3, 1), 2)
        T.check(took is not None, f"保留位 {low:#x} 的报文未被接受")
        T.inject(A, build_packet(FAKE_MAC, 0, 0))
        wait_for(lambda: A.state()["tier"] == 0, 2)
    for tier in (3, 1, 2):
        T.inject(A, build_packet(FAKE_MAC, 0, tier))
        time.sleep(0.05)
    T.settle(1.0)
    T.check(A.state()["tier"] == 2, "乱序/重复报文后应以最后一条为准")


@test("P5 独立节点不接收;P6 主节点收到他人报文置警告但不跟随;P7 自己的 MAC 不触发警告")
def p5(T):
    T.normalize()
    A, M = T.A, T.M
    # 独立
    T.inject(A, build_packet(FAKE_MAC, 1, 3))
    T.settle(2)
    st = A.state()
    T.check((st["tier"], st["rev"], st["warn"]) == (0, 0, 0), f"独立节点不应受影响: {st}")
    # 主
    M.role(A.node_id, ROLE_LEADER)
    M.fan(A.node_id, 1)
    T.settle()
    T.inject(A, build_packet(FAKE_MAC, 1, 3))
    took = wait_for(lambda: A.state()["warn"] == 1, 3)
    T.check(took is not None, "主节点未置警告")
    st = A.state()
    T.check((st["tier"], st["rev"]) == (1, 0), f"主节点不应跟随他人: {st['tier']},{st['rev']}")
    T.check(M.read(A.node_id, [f"{EP_WARN}/80/3"])[f"{EP_WARN}/80/3"] == 1, "Matter 中警告状态应为 Warning")
    T.check(M.clear_warning(A.node_id) == "ok", "清除警告失败")
    T.check(A.state()["warn"] == 0, "清除后应为 None")
    T.inject(A, build_packet(FAKE_MAC, 0, 2))
    T.check(wait_for(lambda: A.state()["warn"] == 1, 3) is not None, "清除后再次收到他人报文应重新警告")
    M.clear_warning(A.node_id)
    # 自己的 MAC
    T.inject(A, build_packet(A.get_mac(), 1, 3))
    T.settle(2.5)
    st = A.state()
    T.check(st["warn"] == 0 and st["tier"] == 1, f"自己发出的报文不应触发警告: {st}")
    # 从节点忽略自己的 MAC
    M.role(A.node_id, ROLE_FOLLOWER)
    T.inject(A, build_packet(A.get_mac(), 1, 3))
    T.settle(2)
    T.check(A.state()["tier"] == 1, "从节点不应应用带自己 MAC 的报文")


# ---------------------------------------------------------------- 主节点发送

def packets_since(T, board, since):
    """board 发出的报文,由另一块板的日志观察:[(t, reverse, tier, None)]"""
    import re

    observer = T.other(board)
    mac = board.get_mac().hex()
    out = []
    for t, line in observer.find(r"SYNC-RX", since=since):
        m = re.search(r"mac=([0-9a-f]{12}) rev=(\d) tier=(\d)", line)
        if m and m.group(1) == mac:
            out.append((t, m.group(2) == "1", int(m.group(3)), None))
    return out


@test("X1 主节点按 5 秒周期发送,内容正确,未收到自己的回环导致警告")
def x1(T):
    T.normalize()
    B, M = T.B, T.M
    M.role(B.node_id, ROLE_LEADER)
    M.fan(B.node_id, 2)
    T.settle()
    t0 = now()
    time.sleep(42)
    pk = packets_since(T, B, t0)
    T.check(len(pk) >= 7, f"42 秒内只收到 {len(pk)} 条")
    gaps = [round(b[0] - a[0], 2) for a, b in zip(pk, pk[1:])]
    T.note(f"间隔(秒): min={min(gaps):.2f} max={max(gaps):.2f} avg={sum(gaps) / len(gaps):.2f}")
    T.check(all(4.4 <= g <= 5.6 for g in gaps), f"间隔超出 5±0.6 秒: {gaps}")
    T.check(all(p[2] == 2 and p[1] is False for p in pk), "报文内容(风力 2,正向)不符")
    st = B.state()
    T.check(st["warn"] == 0, "单个主节点不应有警告")
    T.note(f"主节点 tx={st['tx']} 自身回环 rx_own={st['own']}")


def one_packet_after(T, board, t_change, window=3.0):
    pk = [p for p in packets_since(T, board, t_change - 0.05) if p[0] <= t_change + window]
    return pk


@test("X2 风力变化、方向变化、切换为主都立即发送一次,不连发")
def x2(T):
    T.normalize()
    A, B, M = T.A, T.B, T.M
    M.role(B.node_id, ROLE_LEADER)
    M.fan(B.node_id, 1)
    T.settle()
    # 与周期报文错开:等一条周期报文出现后再操作
    def wait_periodic():
        t = now()
        wait_for(lambda: packets_since(T, B, t), 7)
        time.sleep(0.6)

    wait_periodic()
    t = now()
    M.fan(B.node_id, 3)
    time.sleep(3.0)
    pk = one_packet_after(T, B, t)
    T.check(len(pk) == 1 and pk[0][2] == 3, f"风力变化后应立即发 1 条(风力 3),实际 {[(round(p[0]-t,2), p[2]) for p in pk]}")
    if pk:
        T.note(f"风力变化 → 发出延迟 {pk[0][0] - t:.2f} 秒")
    wait_periodic()
    t = now()
    M.direction(B.node_id, DIR_REV)
    time.sleep(3.0)
    pk = one_packet_after(T, B, t)
    T.check(len(pk) == 1 and pk[0][1] is True, f"方向变化后应立即发 1 条(反向),实际 {[(round(p[0]-t,2), p[1]) for p in pk]}")
    # 独立 → 主
    M.fan(A.node_id, 2)
    T.settle()
    t = now()
    M.role(A.node_id, ROLE_LEADER)
    time.sleep(3.0)
    pk = one_packet_after(T, A, t)
    T.check(len(pk) == 1 and pk[0][2] == 2, f"切换为主后应立即发 1 条,实际 {[(round(p[0]-t,2), p[2]) for p in pk]}")
    M.role(A.node_id, ROLE_INDEPENDENT)


@test("X3 独立节点、从节点不发送")
def x3(T):
    T.normalize()
    A, B, M = T.A, T.B, T.M
    M.fan(A.node_id, 2)
    M.role(B.node_id, ROLE_FOLLOWER)
    T.settle()
    tx_a, tx_b = A.state()["tx"], B.state()["tx"]
    t = now()
    time.sleep(13)
    T.check(not packets_since(T, A, t), "独立节点发送了报文")
    T.check(not packets_since(T, B, t), "从节点发送了报文")
    sa, sb = A.state(), B.state()
    T.check(sa["tx"] == tx_a and sb["tx"] == tx_b, f"发送计数不应增加: A {tx_a}→{sa['tx']} B {tx_b}→{sb['tx']}")


@test("X4 主节点风扇关闭时照常发送,往复继续按周期翻转并立即发送")
def x4(T):
    T.normalize()
    B, M = T.B, T.M
    M.role(B.node_id, ROLE_LEADER)
    M.direction(B.node_id, DIR_ALT)
    B.set_cycle(8)
    t0 = now()
    time.sleep(42)
    pk = packets_since(T, B, t0)
    T.check(all(p[2] == 0 for p in pk), "风扇关闭时风力应为 0")
    changes = [pk[i] for i in range(1, len(pk)) if pk[i][1] != pk[i - 1][1]]
    T.check(len(changes) >= 4, f"42 秒内翻转次数 {len(changes)} 少于 4")
    spans = [round(b[0] - a[0], 2) for a, b in zip(changes, changes[1:])]
    T.note(f"翻转间隔(周期 8 秒): {spans}")
    T.check(all(6.8 <= s <= 9.2 for s in spans), f"翻转间隔偏离 8 秒: {spans}")
    # 翻转报文是立即发出的,不应与 5 秒周期网格对齐:相邻两条报文间隔会小于 5 秒
    gaps = [round(b[0] - a[0], 2) for a, b in zip(pk, pk[1:])]
    T.check(any(g < 4.4 for g in gaps), f"没有看到立即发送的翻转报文: {gaps}")
    B.set_cycle(0)


# ---------------------------------------------------------------- 从节点跟随

def leader_follower(T, leader, follower):
    T.normalize()
    T.M.role(leader.node_id, ROLE_LEADER)
    T.M.role(follower.node_id, ROLE_FOLLOWER)
    T.settle(1.5)


@test("F1 从节点跟随风力(含 Matter 一致性、订阅缓存)")
def f1(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    for tier in (1, 2, 3, 2, 1, 0):
        t = now()
        M.fan(B.node_id, tier)
        took = wait_for(lambda: A.state()["tier"] == tier, 4, 0.1)
        T.check(took is not None, f"风力 {tier}: 从节点未跟随")
        if took is not None:
            T.note(f"风力→{tier}: 从节点 {now() - t:.2f} 秒内跟随")
        view = M.fan_view(A.node_id)
        T.check((view["mode"], view["percent"], view["current"]) == (tier, TIER_PERCENT[tier], TIER_PERCENT[tier]),
                f"风力 {tier}: Matter 视图 {view}")
        cached = wait_for(lambda: M.cached(A.node_id, f"{EP_FAN}/{FAN_CLUSTER}/{ATTR_FAN_MODE}") == tier, 6, 0.5)
        T.check(cached is not None, f"风力 {tier}: matter-server 订阅缓存未更新")


@test("F2 从节点跟随风向;F3 正向风向换算(同设置同风向,不同设置相反)")
def f2(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.fan(B.node_id, 2)
    for fwd_a in (FWD_INTAKE, FWD_EXHAUST):
        for fwd_b in (FWD_INTAKE, FWD_EXHAUST):
            M.forward_direction(A.node_id, fwd_a)
            M.forward_direction(B.node_id, fwd_b)
            for rev in (False, True):
                M.direction(B.node_id, DIR_REV if rev else DIR_FWD)
                ok = wait_for(lambda: A.state()["rev"] == int(rev) and B.state()["rev"] == int(rev), 4, 0.1)
                T.check(ok is not None, f"fwdA={fwd_a} fwdB={fwd_b} rev={rev}: 未跟随")
                sa, sb = A.state(), B.state()
                T.check(sa["actual"] == expect_actual(2, rev, fwd_a), f"A 实际风向 {sa['actual']}")
                T.check(sb["actual"] == expect_actual(2, rev, fwd_b), f"B 实际风向 {sb['actual']}")
                if fwd_a == fwd_b:
                    T.check(sa["actual"] == sb["actual"], "正向风向相同的两个节点实际风向应相同")
                else:
                    T.check(sa["actual"] != sb["actual"], "正向风向不同的两个节点实际风向应相反")


@test("F4 从节点不跟随风向模式、周期;F5 从节点拒绝直接写入")
def f4(T):
    A, B, M = T.A, T.B, T.M
    T.normalize()
    na = A.node_id
    M.period(na, 2)  # 120 秒
    M.direction(na, DIR_REV)  # 固定反向
    M.forward_direction(na, FWD_EXHAUST)
    M.role(B.node_id, ROLE_LEADER)
    M.period(B.node_id, 1)
    M.direction(B.node_id, DIR_ALT)
    M.fan(B.node_id, 2)
    M.role(na, ROLE_FOLLOWER)
    T.settle(8)
    st = A.state()
    T.check((st["tier"], st["rev"]) == (2, 0), f"从节点应跟随主节点的风力和风向: {st['tier']},{st['rev']}")
    T.check((st["mode"], st["cycle"], st["fwd"]) == (1, 120, 1), f"从节点自己的设置被改: mode={st['mode']} cycle={st['cycle']} fwd={st['fwd']}")
    r = M.read(na, [f"{EP_PERIOD}/80/3", f"{EP_DIR}/80/3"])
    T.check(r == {f"{EP_PERIOD}/80/3": 2, f"{EP_DIR}/80/3": DIR_REV}, f"Matter 中的设置: {r}")
    # F5
    T.check(M.fan(na, 1) != 0, "从节点应拒绝写风力")
    res = M.direction(na, DIR_ALT)
    T.check(res != "ok" and "203" in res, f"从节点应拒绝改风向模式(203): {res}")
    res = M.period(na, 0)
    T.check(res != "ok" and "203" in res, f"从节点应拒绝改周期(203): {res}")
    T.check(M.forward_direction(na, FWD_INTAKE) == "ok", "从节点应允许改正向风向")
    T.check(M.role(na, ROLE_FOLLOWER) == "ok", "从节点应允许改角色")
    T.settle(1)
    st = A.state()
    T.check((st["tier"], st["mode"], st["cycle"]) == (2, DIR_REV, 120), f"拒绝后状态变了: {st}")


@test("F5b 风向模式三选一:往复翻转,固定不翻转,切换立即生效")
def f5b(T):
    B, M = T.B, T.M
    T.normalize()
    nb = B.node_id
    B.set_cycle(6)
    for mode, rev in ((DIR_REV, 1), (DIR_FWD, 0)):
        must(M.direction(nb, mode))
        must(M.fan(nb, 1))
        T.settle(1)
        st = B.state()
        T.check((st["mode"], st["rev"]) == (mode, rev), f"固定模式 {mode}: {st}")
        revs = set()
        for _ in range(16):
            revs.add(B.state()["rev"])
            time.sleep(0.5)
        T.check(revs == {rev}, f"固定模式 {mode} 不应翻转: {revs}")
    must(M.direction(nb, DIR_ALT))
    T.check(wait_for(lambda: B.state()["rev"] == 1, 12, 0.2) is not None, "往复应翻到反向")
    must(M.direction(nb, DIR_REV))
    T.settle(1)
    st = B.state()
    T.check((st["mode"], st["rev"]) == (DIR_REV, 1), f"往复中改成固定反向: {st}")
    must(M.direction(nb, DIR_FWD))
    T.settle(1)
    T.check(B.state()["rev"] == 0, "往复中改成固定正向应立即正向")


@test("F6 往复翻转传到从节点(延迟用日志测量)")
def f6(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.direction(B.node_id, DIR_ALT)
    M.fan(B.node_id, 2)
    B.set_cycle(10)
    t0 = now()
    time.sleep(50)
    ev = sync_events(B, A, t0)
    fl = flips(ev)
    T.check(len(fl) >= 4, f"翻转次数 {len(fl)} 少于 4")
    lat = [round(f[3] - f[0], 3) for f in fl if f[3] is not None]
    T.note(f"翻转报文 发出→收到 延迟(秒): {lat}")
    T.check(len(lat) == len(fl), "有翻转报文没有被从节点收到")
    T.check(all(l <= 0.5 for l in lat), f"翻转延迟过大: {lat}")
    sa, sb = A.state(), B.state()
    T.check(sa["rev"] == sb["rev"], "最终从节点风向应与主节点一致")
    B.set_cycle(0)


@test("F7 主节点静默时从节点保持最后状态,恢复后追上")
def f7(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.direction(B.node_id, DIR_REV)
    M.fan(B.node_id, 3)
    T.check(wait_for(lambda: (A.state()["tier"], A.state()["rev"]) == (3, 1), 4) is not None, "未同步到初始状态")
    B.set_loss(100, 0)
    time.sleep(1)
    M.fan(B.node_id, 1)  # 主节点自己改了,但发不出去
    t = now()
    while now() - t < 26:
        st = A.state()
        T.check((st["tier"], st["rev"]) == (3, 1), f"主节点静默期间从节点状态变了: {st['tier']},{st['rev']}")
        time.sleep(4)
    view = M.fan_view(A.node_id)
    T.check(view["mode"] == 3 and view["actual"] == expect_actual(3, 1, 0), f"Matter 中显示的不是保持的状态: {view}")
    B.set_loss(0, 0)
    took = wait_for(lambda: A.state()["tier"] == 1, 8, 0.2)
    T.check(took is not None, "主节点恢复后从节点没有追上")
    if took is not None:
        T.note(f"恢复发送后 {took:.1f} 秒追上")


@test("F8 从节点重启:持久化角色,风力不保存,等同步")
def f8(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    must(M.fan(B.node_id, 3))
    T.check(wait_for(lambda: A.state()["tier"] == 3, 4) is not None, "未同步")
    # (a) 主节点静默时重启
    B.set_loss(100, 0)
    time.sleep(1)
    tb = T.reboot(A)
    st = A.state()
    T.check(st["role"] == ROLE_FOLLOWER, f"角色应恢复为从: {st['role']}")
    T.check(st["tier"] == 0 and st["actual"] == ACTUAL_STOPPED and st["rev"] == 0,
            f"重启后从节点应静止: tier={st['tier']} actual={st['actual']}")
    time.sleep(10)
    st = A.state()
    T.check(st["tier"] == 0, f"主节点静默时从节点应保持静止: {st['tier']}")
    B.set_loss(0, 0)
    took = wait_for(lambda: A.state()["tier"] == 3, 8, 0.2)
    T.check(took is not None, "主节点恢复后从节点没有跟随")
    # (b) 主节点在发送时重启
    t = T.reboot(A)
    took = wait_for(lambda: A.state()["tier"] == 3, 9, 0.2)
    T.check(took is not None, "从节点重启后没有在一个周期内拿到同步")
    if took is not None:
        T.note(f"从节点启动就绪后 {took:.1f} 秒拿到同步")


@test("F9 主节点重启:恢复设置和风力,往复从正向开始,从节点重新跟随")
def f9(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    nb = B.node_id
    must(M.period(nb, 1))
    must(M.forward_direction(nb, FWD_EXHAUST))
    must(M.direction(nb, DIR_ALT))
    must(M.fan(nb, 3))
    B.set_cycle(6)
    T.check(wait_for(lambda: B.state()["rev"] == 1, 12, 0.2) is not None, "往复未翻到反向")
    T.reboot(B)
    st = B.state()
    T.check((st["role"], st["tier"], st["mode"], st["cycle"], st["fwd"], st["warn"]) == (ROLE_LEADER, 3, 2, 90, 1, 0),
            f"重启后设置不对: {st}")
    T.check(st["rev"] == 0, "往复应从正向开始")
    T.check(st["actual"] == expect_actual(3, 0, 1), f"实际风向 {st['actual']}")
    took = wait_for(lambda: (A.state()["tier"], A.state()["rev"]) == (3, 0), 9, 0.2)
    T.check(took is not None, "主节点重启后从节点没有跟上")


@test("F10 独立节点重启:恢复设置和风力")
def f10(T):
    A, M = T.A, T.M
    T.normalize()
    na = A.node_id
    must(M.period(na, 2))
    must(M.direction(na, DIR_REV))
    must(M.forward_direction(na, FWD_EXHAUST))
    must(M.fan(na, 2))
    T.reboot(A)
    st = A.state()
    T.check((st["role"], st["tier"], st["mode"], st["cycle"], st["fwd"], st["rev"], st["tx"]) ==
            (ROLE_INDEPENDENT, 2, 1, 120, 1, 1, 0), f"重启后状态: {st}")
    T.check(st["actual"] == expect_actual(2, 1, 1), f"实际风向 {st['actual']}")


# ---------------------------------------------------------------- 角色切换

@test("R 角色切换链:独立→主→从→主…,值沿用,发送与接收随角色变化")
def r(T):
    A, B, M = T.A, T.B, T.M
    T.normalize()
    na, nb = A.node_id, B.node_id
    must(M.role(nb, ROLE_FOLLOWER))
    must(M.fan(na, 2))
    must(M.direction(na, DIR_REV))
    T.settle()
    # 独立 → 主:B 开始跟随
    must(M.role(na, ROLE_LEADER))
    T.check(wait_for(lambda: (B.state()["tier"], B.state()["rev"]) == (2, 1), 4, 0.1) is not None,
            "独立→主:从节点没有跟随")
    # 主 → 从:A 保留当前值,停止发送;B 保持
    t = now()
    must(M.role(na, ROLE_FOLLOWER))
    time.sleep(12)
    sa = A.state()
    T.check((sa["tier"], sa["rev"]) == (2, 1), f"主→从:应保留当前值 {sa['tier']},{sa['rev']}")
    T.check(not [p for p in packets_since(T, A, t + 0.3)], "主→从:仍在发送")
    T.check((B.state()["tier"], B.state()["rev"]) == (2, 1), "A 静默期间 B 应保持")
    # B: 从 → 主,保留同步来的风力;A 跟随 B
    must(M.role(nb, ROLE_LEADER))
    st = B.state()
    # 风力是同步来的、无法由自己的设置确定,沿用 2;风向由自己的风向模式(固定正向)决定
    T.check((st["tier"], st["rev"]) == (2, 0), f"从→主:风力沿用、风向按自身模式 {st['tier']},{st['rev']}")
    must(M.fan(nb, 3))
    T.check(wait_for(lambda: A.state()["tier"] == 3, 4, 0.1) is not None, "A 没有跟随新的主节点 B")
    # B: 主 → 独立:停止发送,保留值;A 保持
    t = now()
    must(M.role(nb, ROLE_INDEPENDENT))
    M.fan(nb, 1)  # 独立节点自己改风力,不应影响 A
    time.sleep(12)
    T.check(not packets_since(T, B, t + 0.3), "主→独立:仍在发送")
    T.check(A.state()["tier"] == 3, "B 变独立后 A 应保持最后状态")
    # A: 从 → 独立 保留值,不再跟随;A 再变从后等同步
    must(M.role(na, ROLE_INDEPENDENT))
    must(M.role(nb, ROLE_LEADER))
    must(M.fan(nb, 2))
    time.sleep(7)
    T.check(A.state()["tier"] == 3, "独立节点不应跟随")
    must(M.role(na, ROLE_FOLLOWER))
    st = A.state()
    T.check(st["tier"] == 3, "独立→从:切换瞬间应保留当前值")
    T.check(wait_for(lambda: A.state()["tier"] == 2, 7, 0.2) is not None, "独立→从:之后应跟随主节点")


# ---------------------------------------------------------------- 多主

@test("M1 两个主节点:互相警告、可清除、仍多主则重新警告、消除后不再警告")
def m1(T):
    A, B, M = T.A, T.B, T.M
    T.normalize()
    must(M.role(A.node_id, ROLE_LEADER))
    must(M.role(B.node_id, ROLE_LEADER))
    ok = wait_for(lambda: A.state()["warn"] == 1 and B.state()["warn"] == 1, 8, 0.25)
    T.check(ok is not None, "两个主节点没有都警告")
    if ok is not None:
        T.note(f"{ok:.1f} 秒内互相警告")
    r = M.read(A.node_id, [f"{EP_WARN}/80/3"])
    T.check(r[f"{EP_WARN}/80/3"] == 1, "Matter 中应显示 Warning")
    # 不能手动设为 Warning
    res = M.mode(A.node_id, EP_WARN, 1)
    T.check(res != "ok" and "203" in res, f"控制器不应能把警告设为 Warning: {res}")
    M.clear_warning(A.node_id)
    T.check(A.state()["warn"] == 0, "清除后应为 None")
    T.check(wait_for(lambda: A.state()["warn"] == 1, 8, 0.25) is not None, "仍然多主,清除后应重新警告")
    # 解决冲突
    must(M.role(B.node_id, ROLE_INDEPENDENT))
    M.clear_warning(A.node_id)
    M.clear_warning(B.node_id)
    time.sleep(16)
    T.check(A.state()["warn"] == 0 and B.state()["warn"] == 0, "消除多主后不应再有警告")


@test("M2 从节点不检查来源:被他人报文覆盖后由下一个周期纠正;M3 警告不持久化")
def m2(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.fan(B.node_id, 1)
    T.check(wait_for(lambda: A.state()["tier"] == 1, 4, 0.1) is not None, "未同步")
    T.inject(A, build_packet(FAKE_MAC, 1, 3))
    T.check(wait_for(lambda: (A.state()["tier"], A.state()["rev"]) == (3, 1), 2, 0.1) is not None,
            "从节点应直接应用他人的报文")
    took = wait_for(lambda: (A.state()["tier"], A.state()["rev"]) == (1, 0), 7, 0.2)
    T.check(took is not None, "下一个周期报文应把从节点纠正回来")
    if took is not None:
        T.note(f"被覆盖后 {took:.1f} 秒纠正")
    # M3:A 作为主节点收到他人报文,重启后警告消失
    M.role(B.node_id, ROLE_INDEPENDENT)
    M.role(A.node_id, ROLE_LEADER)
    T.settle()
    T.inject(A, build_packet(FAKE_MAC, 0, 1))
    T.check(wait_for(lambda: A.state()["warn"] == 1, 3) is not None, "未警告")
    T.reboot(A)
    st = A.state()
    T.check(st["warn"] == 0 and st["role"] == ROLE_LEADER, f"重启后警告应消失、角色保留: {st}")


# ---------------------------------------------------------------- 丢包

@test("L1 随机丢包:30% 接收丢包下的翻转同步;L2 全丢后恢复;L3 发送端丢包")
def l1(T):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.direction(B.node_id, DIR_ALT)
    M.fan(B.node_id, 2)
    B.set_cycle(10)
    A.set_loss(0, 30)
    t0 = now()
    time.sleep(75)
    ev = sync_events(B, A, t0)
    fl = flips(ev)
    # 对每次翻转,取到从节点收到的第一个“新方向”报文的时间(之后的周期报文也算)
    lat = []
    for f in fl:
        later = [e for e in ev if e[1] == f[1] and e[0] >= f[0] and e[3] is not None]
        lat.append(round(later[0][3] - f[0], 1) if later else None)
    T.note(f"30% 丢包,翻转同步延迟(秒): {lat}")
    T.check(len(fl) >= 6, f"翻转次数 {len(fl)}")
    T.check(all(l is not None and l <= 22 for l in lat), f"有翻转没有在 22 秒内同步: {lat}")
    sa = A.state()
    T.check(sa["rxd"] > 0, "测试用丢包没有生效")
    B.set_cycle(0)
    M.direction(B.node_id, DIR_FWD)
    A.set_loss(0, 0)
    T.check(wait_for(lambda: (A.state()["rev"], A.state()["tier"]) == (0, 2), 8, 0.2) is not None, "丢包恢复后未收敛")
    # L2:全丢 20 秒,期间主节点改成风力 3
    A.set_loss(0, 100)
    M.fan(B.node_id, 3)
    time.sleep(20)
    T.check(A.state()["tier"] == 2, "全丢期间从节点应保持")
    A.set_loss(0, 0)
    took = wait_for(lambda: A.state()["tier"] == 3, 8, 0.2)
    T.check(took is not None, "恢复接收后应在一个周期内收敛")
    if took is not None:
        T.note(f"全丢恢复后 {took:.1f} 秒收敛")
    # L3:发送端 50% 丢包,改风力
    B.set_loss(50, 0)
    delays = []
    for tier in (1, 2, 1, 3):
        t = now()
        M.fan(B.node_id, tier)
        took = wait_for(lambda: A.state()["tier"] == tier, 25, 0.2)
        T.check(took is not None, f"发送端丢包下风力 {tier} 没有在 25 秒内同步")
        delays.append(None if took is None else round(took, 1))
    T.note(f"发送端 50% 丢包,风力变化同步延迟(秒): {delays}")
    B.set_loss(0, 0)


# ---------------------------------------------------------------- 浸泡

def soak(T, seconds):
    A, B, M = T.A, T.B, T.M
    leader_follower(T, B, A)
    M.direction(B.node_id, DIR_ALT)
    M.fan(B.node_id, 2)
    B.set_cycle(20)
    a0, b0 = A.state(), B.state()
    start = now()
    rng = random.Random(7)
    next_change = start + 15
    mismatches = []  # 持续不一致的时长
    mismatch_since = None
    samples = 0
    heap_min = min(a0["heap"], b0["heap"])
    while now() - start < seconds:
        t = now()
        if t >= next_change:
            M.fan(B.node_id, rng.choice([0, 1, 2, 3]))
            next_change = t + rng.uniform(11, 29)
        sa, sb = A.state(), B.state()
        samples += 1
        heap_min = min(heap_min, sa["heap"], sb["heap"])
        same = (sa["tier"], sa["rev"]) == (sb["tier"], sb["rev"])
        if not same and mismatch_since is None:
            mismatch_since = t
        if same and mismatch_since is not None:
            mismatches.append(t - mismatch_since)
            mismatch_since = None
        if sa["up"] < a0["up"] or sb["up"] < b0["up"]:
            T.check(False, "浸泡期间有板子重启")
            break
        time.sleep(0.4)
    B.set_cycle(0)
    a1, b1 = A.state(), B.state()
    T.note(f"浸泡 {seconds} 秒,采样 {samples} 次,不一致 {len(mismatches)} 次,"
           f"最长 {max(mismatches) if mismatches else 0:.1f} 秒,平均 {sum(mismatches) / len(mismatches) if mismatches else 0:.1f} 秒")
    T.note(f"B 发送 {b1['tx'] - b0['tx']} 条,A 收到 {a1['rx'] - a0['rx']} 条,A 非法 {a1['inv'] - a0['inv']} 条,"
           f"B 发送失败 {b1['txf'] - b0['txf']}")
    T.note(f"堆最小值 {heap_min},起始 A={a0['heap']} B={b0['heap']},结束 A={a1['heap']} B={b1['heap']}")
    T.check(not mismatches or max(mismatches) <= 6.5, f"最长不一致 {max(mismatches):.1f} 秒超过 6.5 秒")
    T.check(a1["inv"] - a0["inv"] == 0, "浸泡期间出现非法报文")
    sent, got = b1["tx"] - b0["tx"], a1["rx"] - a0["rx"]
    T.check(sent > 0 and got >= sent * 0.95 - 1, f"报文送达率低: 发送 {sent} 收到 {got}")
    T.check(b1["txf"] - b0["txf"] == 0, "发送出现失败")
    T.check(a1["warn"] == 0 and b1["warn"] == 0, "单个主节点浸泡期间不应有警告")
    T.check(min(a1["heap"], b1["heap"]) > min(a0["heap"], b0["heap"]) - 8192, "堆明显下降,可能泄漏")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", default="", help="只跑名字以这些前缀开头的测试,逗号分隔,如 P,X1")
    parser.add_argument("--soak", type=int, default=0, help="浸泡测试秒数,0 为不跑")
    parser.add_argument("--report", default="../../../out/sync_test_report.md")
    args = parser.parse_args()
    only = [s for s in args.only.split(",") if s]

    T = Ctx()
    results = []
    selected = [(n, f) for n, f in TESTS if not only or any(n.startswith(p) for p in only)]
    if args.soak:
        selected.append((f"S1 浸泡 {args.soak} 秒", lambda T: soak(T, args.soak)))
    for name, fn in selected:
        print(f"\n▶ {name}")
        T.failures, T.notes = [], []
        started = now()
        try:
            fn(T)
        except Exception as exc:  # 测试本身出错也算失败
            T.failures.append(f"异常: {exc!r}")
            traceback.print_exc()
        status = "PASS" if not T.failures else "FAIL"
        print(f"   {status} ({now() - started:.0f}s)")
        for f in T.failures:
            print("   ✗", f)
        results.append((name, status, now() - started, list(T.failures), list(T.notes)))
    try:
        T.normalize()
    except Exception:
        pass

    lines = ["# UDP 同步端到端测试报告", "", f"时间:{time.strftime('%Y-%m-%d %H:%M:%S')}", "",
             "| 测试 | 结果 | 用时 |", "|---|---|---|"]
    for name, status, took, failures, notes in results:
        lines.append(f"| {name} | {status} | {took:.0f}s |")
    lines.append("")
    for name, status, took, failures, notes in results:
        if failures or notes:
            lines.append(f"## {name}")
            lines += [f"- ✗ {f}" for f in failures] + [f"- {n}" for n in notes]
            lines.append("")
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), args.report)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("\n".join(lines))
    failed = [r for r in results if r[1] == "FAIL"]
    print(f"\n{len(results) - len(failed)}/{len(results)} 通过,报告 {path}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
