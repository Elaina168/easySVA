#!/bin/bash
# ============================================================
# ptz_capture.sh  —  easySVA / GB28181 云台(PTZ)SIP 信令抓包解码器
#
# 链路: 前端点云台 -> 后端 POST /waring/device/ptz/{apeId}
#       -> Java 用【随机源端口 UDP】把 SIP MESSAGE(DeviceControl/PTZCmd)
#          直接发到设备端口(设备1=15060 / 设备2=15062)，设备回 200 OK
#       => 注意: PTZ 不走 5060, 必须抓 15060/15062!
#
# 用法:
#   bash ptz_capture.sh                      # 智能解码模式(默认), 然后去网页点云台
#   bash ptz_capture.sh --raw                # 原始 SIP 报文(tcpdump, 等价 Wireshark 文本)
#   bash ptz_capture.sh --fire 34020000001320000003 right   # 自动发一条"右转+停止"并抓包自测
#   bash ptz_capture.sh --fire gb_34020000001320000002_0100000016 up --duration 8
#   bash ptz_capture.sh -w /tmp/ptz.log      # 同时把纯文本结果写入日志
#
# 可选动作: stop up down left right upleft upright downleft downright zoomin zoomout
# ============================================================
exec python3 - "$@" <<'PY'
import socket, struct, sys, time, re, json, argparse, threading, urllib.request

DEV_PORTS = {15060: "设备1(本地视频球机)", 15062: "设备2(电脑摄像头)"}
ACTION = {0x00:"停止/复位 Stop",0x08:"向上 Tilt Up",0x04:"向下 Tilt Down",
          0x02:"向左 Pan Left",0x01:"向右 Pan Right",0x0A:"左上 Up-Left",0x09:"右上 Up-Right",
          0x06:"左下 Down-Left",0x05:"右下 Down-Right",0x10:"放大 Zoom In",0x20:"缩小 Zoom Out"}
C = dict(R="\033[0m", CY="\033[96m", Y="\033[93m", G="\033[92m", M="\033[95m",
         GR="\033[90m", RD="\033[91m", B="\033[1m")

ap = argparse.ArgumentParser()
ap.add_argument("--raw", action="store_true", help="原始 tcpdump 报文模式")
ap.add_argument("--ports", default="15060,15062,5060")
ap.add_argument("-w","--log", default="/opt/SVA-dev/ptz_capture.log")
ap.add_argument("--fire", nargs="+", metavar=("APE_ID","CMD"), help="自动触发PTZ: APE_ID [CMD] [SPEED]")
ap.add_argument("--duration", type=float, default=0, help="N秒后自动退出(0=按Ctrl+C)")
ap.add_argument("--backend", default="http://127.0.0.1:9114")
a = ap.parse_args()

# ---------- 原始模式: 直接交给 tcpdump ----------
if a.raw:
    ports = " or ".join(f"port {p}" for p in a.ports.split(","))
    os_cmd = f"tcpdump -i lo -A -s0 -l 'udp and ({ports})'"
    print(C["CY"]+"[raw] "+os_cmd+C["R"]); print(C["GR"]+"按 Ctrl+C 结束\n"+C["R"])
    import subprocess
    os.system(os_cmd); sys.exit(0)

logf = open(a.log, "a", encoding="utf-8") if a.log else None
def out(color, text, plain=None):
    print(color+text+C["R"])
    if logf: logf.write((plain or re.sub(r"\033\[[0-9;]*m","",text))+"\n"); logf.flush()

stats = {"down":0,"ack":0,"actions":{},"pending":[]}
seen = {}
def dedup(key):
    now=time.time()
    last=seen.get(key)
    seen.clear() if len(seen)>256 else None
    if last and now-last<0.3: return False
    seen[key]=now; return True

def parse_sip(txt):
    d={}
    m=re.search(r"<DeviceID>\s*([^<]+?)\s*</DeviceID>",txt,re.I); d["dev"]=m.group(1) if m else "-"
    m=re.search(r"<SN>\s*([^<]+?)\s*</SN>",txt,re.I); d["sn"]=m.group(1) if m else "-"
    m=re.search(r"Call-ID:\s*([^\r\n]+)",txt,re.I); d["call"]=m.group(1).strip() if m else "-"
    m=re.search(r"CSeq:\s*(\d+)\s*(\w+)",txt,re.I); d["cseq"]=f"{m.group(1)} {m.group(2)}" if m else "-"
    m=re.search(r"<PTZCmd>\s*([0-9A-Fa-f]{16})\s*</PTZCmd>",txt,re.I); d["hex"]=m.group(1).upper() if m else ""
    return d

def decode_hex(h):
    b=[int(h[i:i+2],16) for i in range(0,16,2)]
    code=b[3]; pan=b[4]; tilt=b[5]; zoom=(b[6]>>4)&0x0F
    calc=sum(b[:7])%256; ok=(calc==b[7])
    name=ACTION.get(code,f"未知码 0x{code:02X}")
    return code,name,pan,tilt,zoom,b[7],ok

def fire_thread():  # 自动触发一条指令并在1s后停止
    try:
        ape=a.fire[0]; cmd=(a.fire[1].lower() if len(a.fire)>1 else "right")
        spd=int(a.fire[2]) if len(a.fire)>2 else 32
        time.sleep(1.0)
        req=urllib.request.Request(a.backend+"/login",
            data=json.dumps({"username":"admin","password":"admin123"}).encode(),
            headers={"Content-Type":"application/json"})
        tok=json.load(urllib.request.urlopen(req,timeout=5)).get("token","")
        def post(c):
            body=json.dumps({"command":c,"speed":spd}).encode()
            r=urllib.request.Request(f"{a.backend}/waring/device/ptz/{ape}",data=body,
                headers={"Content-Type":"application/json","Authorization":f"Bearer {tok}"})
            print(C["GR"]+f"[fire] POST ptz {ape} {c} -> "+
                  urllib.request.urlopen(r,timeout=5).read().decode()[:140]+C["R"])
        out(C["M"], f"—— 自动触发: {ape} / {cmd} ——")
        post(cmd); time.sleep(1.0); post("stop")
    except Exception as e:
        out(C["RD"], f"[fire] 触发失败: {e}")

def main():
    try:
        s=socket.socket(socket.AF_INET,socket.SOCK_RAW,socket.IPPROTO_UDP)
    except PermissionError:
        print("需要 root 权限, 请用: sudo bash ptz_capture.sh"); sys.exit(1)
    ports=set(int(x) for x in a.ports.split(","))
    out(C["CY"], f"[PTZ 抓包] 监听 lo UDP 端口 {sorted(ports)}  (PTZ 实际走 15060/15062)  Ctrl+C 结束")
    out(C["GR"], "现在可到 实时监控 页点云台方向键；或用 --fire 自动触发。\n")
    if a.fire: threading.Thread(target=fire_thread,daemon=True).start()
    t0=time.time()
    try:
        while True:
            if a.duration and time.time()-t0>a.duration: break
            data,_=s.recvfrom(65535)
            ihl=(data[0]&0x0F)*4
            if len(data)<ihl+8: continue
            src=".".join(map(str,data[12:16])); dst=".".join(map(str,data[16:20]))
            sport,dport=struct.unpack("!HH",data[ihl:ihl+4])
            if sport not in ports and dport not in ports: continue
            payload=data[ihl+8:]
            if not dedup((sport,dport,payload[:48])): continue
            txt=payload.decode("utf-8","replace")
            is_ptz = ("PTZCmd" in txt) or ("DeviceControl" in txt)
            is_ack = txt.startswith("SIP/2.0")
            if not (is_ptz or is_ack): continue
            ts=time.strftime("%H:%M:%S")
            if dport in DEV_PORTS and ("MESSAGE" in txt.split("\r\n")[0] or is_ptz):  # 下行 平台->设备
                d=parse_sip(txt); stats["down"]+=1
                if d["hex"]:
                    code,name,pan,tilt,zoom,csum,ok=decode_hex(d["hex"]); stats["actions"][name]=stats["actions"].get(name,0)+1
                    stats["pending"].append(d["call"])
                    out(C["Y"], f"{ts} ▼ 平台→{DEV_PORTS.get(dport,dport)}  DeviceControl  设备={d['dev']} SN={d['sn']}")
                    out(C["B"]+C["G"], f"    PTZCmd={d['hex']}  → 【{name}】 水平速度={pan} 垂直速度={tilt} 变焦={zoom} 校验={'OK' if ok else '错误!'}", )
                else:
                    out(C["Y"], f"{ts} ▼ 平台→设备 其他MANSCDP: {txt.splitlines()[0]}")
            elif sport in DEV_PORTS and is_ack:  # 上行 设备应答
                line=txt.splitlines()[0]
                ok200="200" in line; stats["ack"]+=1
                if stats["pending"]: stats["pending"].pop(0)
                out(C["G"] if ok200 else C["RD"], f"{ts} ▲ 设备→平台  应答 {line.strip()}")
    except KeyboardInterrupt:
        pass
    finally:
        out(C["CY"], f"\n==== 统计: 下发PTZ={stats['down']}  设备应答={stats['ack']}  未应答={len(stats['pending'])} ====")
        for k,v in stats["actions"].items(): out(C["CY"], f"    {k}: {v} 次")
        if logf: logf.close()

main()
PY
