#!/usr/bin/env python3
"""Decode CRC-protected RSE1/RSM1/RSZ1 records; report incomplete captures."""
import argparse, json, struct, zlib
from pathlib import Path
class LogError(ValueError): pass
def records(data):
    for offset in range(0,len(data),48):
        b=data[offset:offset+48]
        if len(b)!=48: raise LogError(f'incomplete trailing record at {offset}')
        if b[:4] not in (b'RSE1',b'RSM1',b'RSZ1') or struct.unpack_from('<I',b,4)[0]!=48: raise LogError(f'invalid record at {offset}')
        if zlib.crc32(b[:44])!=struct.unpack_from('<I',b,44)[0]: raise LogError(f'CRC mismatch at {offset}')
        result={'offset':offset,'type':b[:4].decode(),'ms':struct.unpack_from('<Q',b,8)[0]}
        if b[:4]==b'RSM1': result.update(zip(('ax_g','ay_g','az_g','gx_dps','gy_dps','gz_dps','flags'),struct.unpack_from('<6fI',b,16)))
        elif b[:4]==b'RSE1':
            result['id']=list(struct.unpack_from('<III',b,16));result['kind']=struct.unpack_from('<H',b,28)[0]
            result['value']=struct.unpack_from('<f',b,32)[0];result['detail']=struct.unpack_from('<I',b,36)[0]
        else:
            result['samples'],result['truncated']=struct.unpack_from('<II',b,16);result['id']=list(struct.unpack_from('<III',b,24))
        yield result
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('file',type=Path);args=p.parse_args()
    count=0;end=None
    try:
        for row in records(args.file.read_bytes()):
            if end is not None: raise LogError('data after end marker')
            if row['type']=='RSM1': count+=1
            if row['type']=='RSZ1': end=row
            print(json.dumps(row))
        if count and (end is None or end['samples']!=count): raise LogError('capture incomplete: missing/mismatched end marker')
    except LogError as exc: p.exit(1,str(exc)+'\n')
if __name__=='__main__': main()
