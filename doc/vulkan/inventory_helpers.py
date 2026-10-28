"""Lexical inventory helpers; these do not preprocess or parse full C++/GLSL."""
import re


def mask_source(source, literals=False):
    # Match literals before comments so comment markers inside strings survive.
    tokens = re.compile(r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
    def replace(match):
        text = match.group()
        if not literals and not text.startswith(('//', '/*')):
            return text
        return ''.join('\n' if char == '\n' else ' ' for char in text)
    return tokens.sub(replace, source)


def shader_interfaces(source):
    """Collect global lexical declarations, including qualified interface blocks.

    Parenthesis depth excludes multiline function parameters; brace depth excludes
    function bodies and block members. Conditional branches remain unevaluated.
    """
    code = mask_source(source, literals=True)
    code = re.sub(r'^[ \t]*#[^\n]*(?:\\\n[^\n]*)*', lambda m: '\n' * m.group().count('\n'), code, flags=re.M)
    depths = []
    braces = parentheses = 0
    for char in code:
        depths.append((braces, parentheses))
        if char == '{': braces += 1
        elif char == '}': braces -= 1
        elif char == '(': parentheses += 1
        elif char == ')': parentheses -= 1
    qualifiers = r'(?:(?:layout\s*\([^\n]*?\)|flat|smooth|noperspective|centroid|sample|patch|invariant|precise|highp|mediump|lowp|coherent|volatile|restrict|readonly|writeonly)\s+)'
    pattern = re.compile(r'^[ \t]*' + qualifiers + r'*(?:uniform|in|out|buffer)\b', re.M)
    declarations = []
    for match in pattern.finditer(code):
        if depths[match.start()] != (0, 0):
            continue
        for end in range(match.end(), len(code)):
            if code[end] == ';' and depths[end] == (0, 0):
                declarations.append(' '.join(code[match.start():end + 1].split()))
                break
    return declarations


def gl_candidates(source, commands, wrappers):
    code = mask_source(source, literals=True)
    calls = sorted(set(re.findall(r'\b(gl[A-Z]\w*)\s*\(', code)) & commands)
    identifiers = set(re.findall(r'\b[A-Za-z_]\w*\b', code))
    return calls, bool(identifiers & wrappers or re.search(r'\bgGL\s*\.', code))
