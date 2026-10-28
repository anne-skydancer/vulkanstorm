"""Check shipped Contacts geometry after the production XML overlay method."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def llsd(node):
    if node.tag == 'map':
        children = list(node)
        return {children[i].text: llsd(children[i + 1]) for i in range(0, len(children), 2)}
    if node.tag == 'array':
        return [llsd(child) for child in node]
    return node.text or ''


def encode(value):
    return value.encode('utf-8').hex() or '_'


def serialize(node):
    yield f'{encode(node.tag)} {len(node.attrib)} {len(node)}'
    for key, value in node.attrib.items():
        yield f'{encode(key)} {encode(value)}'
    for child in node:
        yield from serialize(child)


def deserialize(tokens):
    decode = lambda value: bytes.fromhex(value).decode('utf-8') if value != '_' else ''
    node = ET.Element(decode(next(tokens)))
    attributes, children = int(next(tokens)), int(next(tokens))
    for _ in range(attributes):
        node.set(decode(next(tokens)), decode(next(tokens)))
    for _ in range(children):
        node.append(deserialize(tokens))
    return node


def rectangles(root, width):
    """Resolve the explicit horizontal XUI constraints, then apply follows resize."""
    reference = int(root.get('width'))
    result = {}
    previous = (0, 0)
    for child in root:
        if child.tag == 'panel.string':
            continue
        attrs = child.attrib
        if 'left_delta' in attrs:
            left = previous[0] + int(attrs['left_delta'])
        elif 'left_pad' in attrs:
            left = previous[1] + int(attrs['left_pad'])
        else:
            left = int(attrs.get('left', previous[0]))
        if 'right' in attrs:
            right = int(attrs['right'])
            if right < 0:
                right += reference
            if 'width' in attrs:
                left = right - int(attrs['width'])
        else:
            right = left + int(attrs['width'])
        previous = left, right
        follows = set(attrs.get('follows', '').split('|'))
        if 'all' in follows or 'right' in follows:
            right += width - reference
            if 'all' not in follows and 'left' not in follows:
                left += width - reference
        result[attrs['name']] = left, right
    return result


class ContactsLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        skins = ROOT / 'indra/newview/skins'
        catalog = llsd(ET.parse(skins / 'skins.xml').getroot()[0])
        variants = {('default', '')}
        for skin in catalog:
            variants.add((skin['folder'], ''))
            variants.update((skin['folder'], theme['folder']) for theme in skin['themes'])
        assert {skin for skin, _ in variants} == {p.name for p in skins.iterdir() if p.is_dir()}
        physical_themes = {(p.parent.parent.name, p.name) for p in skins.glob('*/themes/*')
                           if p.is_dir() and ((p / 'colors.xml').exists() or (p / 'xui').is_dir())}
        assert physical_themes <= variants, physical_themes - variants
        languages = sorted({p.name for p in skins.glob('*/xui/*') if p.is_dir()})
        cls.cases = []
        data = []
        for skin, theme in sorted(variants):
            folders = list(dict.fromkeys([skins / 'default', skins / skin] +
                           ([skins / skin / 'themes' / theme] if theme else [])))
            for language in languages:
                for panel in ('friends', 'groups', 'sets'):
                    paths = [folder / 'xui' / locale / f'panel_fs_contacts_{panel}.xml'
                             for folder in folders for locale in dict.fromkeys(['en', language])]
                    layers = [ET.parse(path).getroot() for path in paths if path.exists()]
                    cls.cases.append((skin, theme, language, panel))
                    data.append(str(len(layers)))
                    for layer in layers:
                        data.extend(serialize(layer))
        source = (ROOT / 'indra/llxml/llxmlnode.cpp').read_text(encoding='utf-8')
        begin = source.index('bool LLXMLNode::updateNode(')
        end = source.index('// static', begin)
        production = source[begin:end]
        data = '\n'.join(data)
        fixture = (ROOT / 'scripts/tests/fixtures/contacts_layout.cpp').read_text().replace(
            'PRODUCTION_XML_UPDATE', production)
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise RuntimeError('C++ compiler required for the production XML merge check')
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'contacts.cpp'
            binary = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError(built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], input=str(len(cls.cases)) + '\n' + data + '\n',
                                 capture_output=True, text=True, timeout=30)
            if ran.returncode:
                raise RuntimeError(ran.stdout + ran.stderr)
        tokens = iter(ran.stdout.split())
        cls.layouts = [deserialize(tokens) for _ in cls.cases]
        assert next(tokens, None) is None

    def check_geometry(self, check, panels=('friends', 'groups', 'sets')):
        failures = []
        for case, root in zip(self.cases, self.layouts):
            if case[-1] not in panels:
                continue
            for width in (255, 355, 555):
                rects = rectangles(root, width)
                if not check(root, case[-1], rects, width):
                    failures.append((case, width, rects))
        self.assertEqual(len(failures), 0, f'{len(failures)} invalid layouts; first: {failures[:1]}')

    def test_avatar_list_stays_inside_its_border(self):
        self.check_geometry(lambda root, panel, r, width:
                            r['contact_list'][0] >= r['info_border'][0] + 2
                            and r['contact_list'][1] <= r['info_border'][1] - 2, panels=('sets',))

    def test_management_buttons_leave_room_for_the_action_column(self):
        self.check_geometry(lambda root, panel, r, width:
                            r['add_btn'][0] - r['remove_set_btn'][1] >= 5, panels=('sets',))

    def test_button_focus_borders_stay_inside_the_panel(self):
        self.check_geometry(lambda root, panel, r, width:
                            all(r[b.get('name')][0] >= 3 and r[b.get('name')][1] + 3 <= width
                                for b in root.findall('button')))

    def test_action_columns_leave_room_for_list_focus_borders(self):
        def separated(root, panel, rects, width):
            edge, button = {'friends': ('ls_friend_list', 'im_btn'),
                            'groups': ('info_border', 'chat_btn'),
                            'sets': ('info_border', 'add_btn')}[panel]
            return rects[edge][1] + 3 <= rects[button][0]
        self.check_geometry(separated)


if __name__ == '__main__':
    unittest.main()
