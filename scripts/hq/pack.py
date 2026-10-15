"""PBHQv1: RGB-only display sidecar; original metadata/depth remain authoritative."""
import argparse, hashlib, json, struct
from pathlib import Path
from PIL import Image
HEADER=struct.Struct('<8sII32s'); RECORD=struct.Struct('<iIIIii32sQ')
assert HEADER.size==48 and RECORD.size==64
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--scale',type=int,choices=[1,2],required=True);p.add_argument('--images',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    m=json.loads((a.source/'manifest.json').read_text(encoding='utf-8')); assets=sorted(m['assets'],key=lambda x:(x['groupId'],x['resolution']))
    b=bytearray(HEADER.pack(b'PBHQv1\0\0',a.scale,len(assets),bytes.fromhex(m['sourceSha256']))); records=[]
    for asset in assets:
        key=asset['key'];w,h=asset['width'],asset['height'];src=(a.source/(key+'.bgra32le')).read_bytes();original_sha=hashlib.sha256(src).digest();assert original_sha.hex()==asset['originalArgbSha256']
        image=Image.open((a.images or a.source)/(key+'.png')).convert('RGB');assert image.size==(w*a.scale,h*a.scale)
        payload=image.tobytes();assert len(payload)==w*h*a.scale*a.scale*3
        b.extend(RECORD.pack(asset['groupId'],asset['resolution'],w,h,asset['xPosition'],asset['yPosition'],original_sha,len(payload)));b.extend(payload)
        records.append({'key':key,'originalArgbSha256':original_sha.hex(),'rgbPayloadSha256':hashlib.sha256(payload).hexdigest(),'payloadBytes':len(payload)})
    footer=hashlib.sha256(b).digest();b.extend(footer);a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('xb') as f:f.write(b)
    # Independent byte traversal checks complete declared length and footer.
    data=a.out.read_bytes();magic,scale,count,datsha=HEADER.unpack_from(data);assert magic==b'PBHQv1\0\0' and count==len(assets)
    offset=HEADER.size
    for asset in assets:
        gid,res,w,h,x,y,original_sha,size=RECORD.unpack_from(data,offset);offset+=RECORD.size
        assert (gid,res,w,h,x,y)==(asset['groupId'],asset['resolution'],asset['width'],asset['height'],asset['xPosition'],asset['yPosition'])
        assert size==w*h*scale*scale*3 and original_sha.hex()==asset['originalArgbSha256'];offset+=size
    assert offset+32==len(data) and hashlib.sha256(data[:offset]).digest()==data[offset:]
    info={'schema':1,'format':'PBHQv1','scale':scale,'records':count,'sourceDatSha256':datsha.hex(),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'footerSha256':footer.hex(),'originalMetadataUnchanged':True,'recordsManifest':records}
    a.out.with_suffix(a.out.suffix+'.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in info.items() if k!='recordsManifest'}))
if __name__=='__main__':main()
