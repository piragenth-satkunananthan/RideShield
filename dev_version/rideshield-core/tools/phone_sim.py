#!/usr/bin/env python3
"""Synthetic phone producer for bench testing. Does not run an AI model."""
import argparse, json, os, struct, time
WIRE = struct.Struct('<4sBBBBIIQIBBH32s')
DEFAULT_TOKEN = 'bench-token-change-before-use-01'
def packet(kind, session, seq, phone_ms=0, source=0, flags=0, hazards=0,
           auxiliary=0, zone=0, confidence=0, token=b''):
    if not 1 <= kind <= 4 or not 0 <= source <= 2: raise ValueError('invalid type/source')
    if not 1 <= session <= 0xffffffff or not 1 <= seq <= 0xffffffff: raise ValueError('session/sequence out of range; reconnect before wrap')
    if not 0 <= phone_ms <= 2**52: raise ValueError('timestamp out of range')
    if not 0 <= flags <= 15 or not 0 <= hazards <= 7 or not 0 <= confidence <= 100 or not 0 <= zone <= 3: raise ValueError('invalid observation')
    if isinstance(token, str): token = token.encode('ascii')
    if kind == 1:
        if len(token) != 32: raise ValueError('AUTH token must be exactly 32 ASCII bytes')
    elif token: raise ValueError('token allowed only in AUTH')
    if kind == 4:
        if source == 0 or auxiliary > 10000 or auxiliary < 0: raise ValueError('invalid camera clock/source')
        if source == 1 and (hazards & 4 or zone) or source == 2 and hazards & 3: raise ValueError('source/hazard mismatch')
    elif source or flags or hazards or zone or confidence: raise ValueError('observation fields on control packet')
    if kind in (1, 3) and auxiliary: raise ValueError('auxiliary must be zero')
    return WIRE.pack(b'RSW1', kind, source, flags, hazards, session, seq, phone_ms, auxiliary, zone, confidence, 0, token)
def monotonic_ms(): return int(time.monotonic()*1000)
def main():
    import websocket
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', default='ws://192.168.4.1:81')
    p.add_argument('--scenario', choices=['pothole','wet','combined','rear','clear','stale'], default='pothole')
    p.add_argument('--seconds', type=float, default=15)
    args = p.parse_args()
    ws = websocket.create_connection(args.url, timeout=3, http_proxy_host=None)
    try:
        welcome=json.loads(ws.recv())
        if welcome.get('type')!='welcome' or welcome.get('version')!=1: raise RuntimeError('unsupported controller')
        session=welcome['session']; seq=0
        def send(kind, **fields):
            nonlocal seq
            seq+=1
            ws.send_binary(packet(kind,session,seq,**fields))
        send(1,token=os.environ.get('RIDESHIELD_PHONE_TOKEN',DEFAULT_TOKEN))
        ws.settimeout(0.02)
        end=time.monotonic()+args.seconds; heartbeat=observation=0.; synced=False
        print('SYNTHETIC observations only; this is not camera/AI validation.')
        while time.monotonic()<end:
            try:
                raw=ws.recv()
                if not raw: raise RuntimeError('controller disconnected')
                message=json.loads(raw)
                if message.get('type')=='sync':
                    send(2,phone_ms=monotonic_ms(),auxiliary=message['id']);synced=True
                else: print(message)
            except websocket.WebSocketTimeoutException: pass
            now=time.monotonic()
            if now>=heartbeat: send(3,phone_ms=monotonic_ms());heartbeat=now+1
            if synced and now>=observation:
                bits={'pothole':1,'wet':2,'combined':3,'rear':4,'clear':0,'stale':1}[args.scenario]
                age=1500 if args.scenario=='stale' else 30
                send(4,phone_ms=monotonic_ms()-age,source=2 if args.scenario=='rear' else 1,
                     flags=11,hazards=bits,auxiliary=10,confidence=90)
                observation=now+0.1
    finally: ws.close()
if __name__=='__main__': main()
