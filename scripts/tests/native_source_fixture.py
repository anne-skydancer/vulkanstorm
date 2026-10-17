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
    # Keep byte offsets stable while ignoring comments and string/character data.
    literals=r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    masked=re.sub(literals,lambda match:' '*len(match[0]),source)
    offset=0
    while True:
        begin=masked.find(signature,offset)
        if begin<0:raise AssertionError('Definition not found: '+signature)
        parens=0
        start=None
        for index in range(begin,len(masked)):
            char=masked[index]
            if char=='(':parens+=1
            elif char==')':parens-=1
            elif not parens and char==';':break  # real forward declaration
            elif not parens and char=='{':
                start=index
                break
        if start is not None:break
        offset=begin+len(signature)
    depth=0
    for end in range(start,len(masked)):
        if masked[end]=='{':depth+=1
        elif masked[end]=='}':
            depth-=1
            if not depth:return source[begin:end+1]
    raise AssertionError('Unterminated definition: '+signature)
