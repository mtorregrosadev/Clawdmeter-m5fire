from pathlib import Path
base=Path('assets_clawd_generated')
for p in sorted(base.glob('*.rgb565')):
    data=p.read_bytes()
    nonzero=sum(1 for i in range(0,len(data),2) if int.from_bytes(data[i:i+2],'little')!=0)
    print(p.name, 'bytes=', len(data), 'pixels=', len(data)//2, 'nonzero=', nonzero)
