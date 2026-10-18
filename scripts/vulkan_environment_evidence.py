"""Independent consumption evidence; not full sky/water/HDR shader parity."""
from pathlib import Path
import hashlib,math

def environment_pixels_passed(report,directory):
 try:
  rows=report['environment_pixels'];images={};root=Path(directory).resolve()
  for name in ('day','night','edit'):
   row=rows[name];relative=f'environment/{name}.rgba';path=root/relative
   if row['file']!=relative or row['width']!=64 or row['height']!=64:return False
   if path.is_symlink()or path.parent.is_symlink()or not path.is_file()or path.resolve().parent!=root/'environment':return False
   data=path.read_bytes()
   if len(data)!=64*64*4 or any(data[i]!=255 for i in range(3,len(data),4)):return False
   filled=sum(data[i:i+3]!=bytes((255,0,255))for i in range(0,len(data),4))
   if filled<=64*64*9//10 or row['filled_pixels']!=filled:return False
   for field in ('ambient','blue_horizon'):
    values=row[field]
    if len(values)!=3 or any(not isinstance(v,(float,int))or not math.isfinite(v)for v in values):return False
   if row['sun_up']is not (name!='night')or row['moon_up']is not (name=='night'):return False
   row['sha256']=hashlib.sha256(data).hexdigest();images[name]=data
  if rows['day']['blue_horizon']==rows['edit']['blue_horizon']:return False
  # Real current day/night and real color edits must affect production pixels.
  # Exclude alpha and require changes above a single UNORM rounding unit.
  for before,after in [('day','night'),('day','edit')]:
   changed=sum(any(abs(images[before][i+c]-images[after][i+c])>1 for c in range(3))for i in range(0,64*64*4,4))
   if changed<64:return False
   report.setdefault('environment_pixel_differences',{})[f'{before}_{after}']=changed
  return True
 except (KeyError,TypeError,ValueError,OSError):return False
