"""Source extraction for standalone compiled native service regressions."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
class ProductionSources(dict):
    def __missing__(self,name):
        value=(ROOT/name).read_text(encoding='utf-8-sig')
        self[name]=value
        return value
def production_sources():return ProductionSources()
def method(source,signature):
 a=source.index(signature)
 masked=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),source)
 start=masked.index('{',a);depth=0
 for end in range(start,len(masked)):
  if masked[end]=='{':depth+=1
  elif masked[end]=='}':
   depth-=1
   if not depth:return source[a:end+1]
 raise AssertionError(signature)