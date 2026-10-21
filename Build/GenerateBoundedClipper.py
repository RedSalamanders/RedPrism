"""Reproduce the isolated bounded Clipper2 engine from exact 2.0.1 sources into a fresh directory."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--clang-format', type=Path, required=True)
parser.add_argument('--format-style', type=Path, required=True)
args = parser.parse_args()
source = args.source.resolve(strict=True)
target = args.output.resolve()
target.mkdir(parents=True, exist_ok=False)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest().upper()
required = {
    'include/clipper2/clipper.h': '763C56ACF64085AF2B5081CA80466210507DF3F49C53F9AAF3CCE5597ED71E36',
    'include/clipper2/clipper.core.h': '91E7C1B418F59DB4A8F30355E75F60E5D26ADFD66C37578BCFD79F76AE8036AB',
    'src/clipper.engine.cpp': '4C66D5CD0C69A1D1CAEE2434A0099A9366771DCF186A8D987AB1B2D294872E98',
    'include/clipper2/clipper.engine.h': 'C3E8B7CABC80F5AB2592DD4401A8C445BD43422139FF216591AAE94DE1B5A175',
    'include/clipper2/clipper.version.h': '0D636F1599B09743496D5C15C7DCB5BC3101BD423D6745BF17E480EDBFFCCDAC',
}
for relative, digest in required.items():
    if sha(source / relative) != digest:
        raise ValueError('Pinned Clipper2 source differs: ' + relative)

def code_mask(text):
    """Keep positions and delimiters; blank comments and quoted strings before identifying loops."""
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), text)

def pair(mask, opening):
    depth = 1
    for position in range(opening + 1, len(mask)):
        depth += (mask[position] == '(') - (mask[position] == ')')
        if depth == 0:
            return position
    raise AssertionError('Unclosed loop header')

def top_level(header, character):
    depth = 0
    positions = []
    for index, char in enumerate(header):
        if char in '([{': depth += 1
        elif char in ')]}': depth -= 1
        elif depth == 0 and char == character: positions.append(index)
    return positions

def checked_loops(text):
    mask = code_mask(text)
    edits = []
    count = {'while': 0, 'for': 0, 'range_for': 0}
    for match in re.finditer(r'\b(while|for)\s*\(', mask):
        opening = mask.find('(', match.start())
        closing = pair(mask, opening)
        header = text[opening + 1:closing]
        if match.group(1) == 'while':
            replacement = '(Workspace::Pulse(), ' + header + ')'
            count['while'] += 1
        else:
            semicolons = top_level(mask[opening + 1:closing], ';')
            if len(semicolons) == 2:
                left, right = semicolons
                condition = header[left + 1:right].strip() or 'true'
                replacement = header[:left + 1] + '(Workspace::Pulse(), ' + condition + ')' + header[right:]
                count['for'] += 1
            else:
                assert not semicolons, header
                colons = [position for position in top_level(mask[opening + 1:closing], ':')
                          if (position == 0 or header[position - 1] != ':') and
                             (position + 1 == len(header) or header[position + 1] != ':')]
                assert len(colons) == 1, header
                colon = colons[0]
                # Upstream ranges are borrowed named paths/containers. Do not shorten temporary lifetime.
                expression = header[colon + 1:].strip()
                assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9.>\-]*', expression), expression
                replacement = header[:colon + 1] + ' CheckedRange(' + expression + ')'
                count['range_for'] += 1
        edits.append((opening + 1, closing, replacement))
    for begin, end, replacement in reversed(edits):
        text = text[:begin] + replacement + text[end:]
    return text, count

manifest = {'scope': 'Deterministic authoring provenance only; runtime/admission/cancellation require their separate gates', 'files': []}
for input_name, output_name in [('include/clipper2/clipper.core.h', 'clipper.core.h'),
                               ('include/clipper2/clipper.engine.h', 'clipper.engine.h'),
                               ('src/clipper.engine.cpp', 'clipper.engine.cpp'),
                               ('include/clipper2/clipper.version.h', 'clipper.version.h')]:
    path = source / input_name
    text = path.read_text(encoding='utf-8-sig')
    text = text.replace('Clipper2Lib', 'RedPrismBoundedClipper')
    text = re.sub(r'\bCLIPPER_(CORE|ENGINE|VERSION)_H\b', r'REDPRISM_BOUNDED_CLIPPER_\1_H', text)
    text = text.replace('"clipper2/clipper.', '"clipper.')
    text = text.replace('std::vector', 'ArenaVector')
    if output_name == 'clipper.core.h':
        text = text.replace('#include <vector>', '#include "Workspace.h"')
        # This private wrapper fixes precision2 and always enables refusal exceptions. The upstream
        # post-DoError clamp is reachable only in its exception-disabled build, not this implementation.
        unreachable = '    precision = precision > 0 ? CLIPPER2_MAX_DEC_PRECISION : -CLIPPER2_MAX_DEC_PRECISION;'
        assert text.count(unreachable) == 1
        text = text.replace(unreachable, '')
        # Known private-engine input/numeric failures use the original job runner boundary.
        text = re.sub(r'throw Clipper2Exception\([^;]+\);', 'Workspace::Refuse();', text)
        for result in ('Point<T1>(pt.x * scale_x, pt.y * scale_y, pt.z)',
                       'Point<T1>(pt.x * scale_x, pt.y * scale_y)',
                       'ScalePath<T1, T2>(path, scale_x, scale_y, error_code)',
                       'Point<T1>(pt)', 'TransformPath<T1, T2>(path)'):
            # Preserve every numerical conversion, including ClipperD precision2's actual binary scale128.
            pattern = r'\{\s*return ' + re.escape(result) + r';\s*\}'
            text, changes = re.subn(pattern, '{ Workspace::Pulse(); return ' + result + '; }', text)
            assert changes == 1, result
    if output_name == 'clipper.engine.h':
        text = text.replace('std::priority_queue<int64_t>', 'ArenaQueue<int64_t>')
        for name in ('Vertex', 'OutPt', 'OutRec', 'Active', 'LocalMinima'):
            text, changes = re.subn(r'\bstruct ' + name + r'\s*\{',
                                   'struct ' + name + ' : ArenaObject<' + name + '> {', text)
            assert changes == 1, name
        text = text.replace('if (splits) delete splits;', 'if (Workspace::IsLive(splits)) delete splits;')
    if output_name == 'clipper.engine.cpp':
        text = text.replace('#include "clipper.h"\n', '')
        # The engine's cleaned-path containment fallback calls this one upstream template.
        # Copy its exact body, rather than exposing the unrelated unbounded wrapper/offset APIs.
        wrappers = (source / 'include/clipper2/clipper.h').read_text(encoding='utf-8-sig')
        helper_begin = wrappers.index('  template <typename T>\n  inline bool Path2ContainsPath1(')
        helper_end = wrappers.index('  template <typename T>\n  inline void RDP(', helper_begin)
        template = wrappers[helper_begin:helper_end]
        containment_anchor = '  inline bool Path2ContainsPath1(OutPt* op1, OutPt* op2)'
        assert text.count(containment_anchor) == 1
        text = text.replace(containment_anchor, template + containment_anchor)
        text = text.replace('std::priority_queue<int64_t>()', 'ArenaQueue<int64_t>()')
        text = text.replace('scanline_list_ = ArenaQueue<int64_t>();', 'scanline_list_.Clear();')
        text = text.replace('std::stable_sort(', 'StableSort(')
        text = text.replace('std::sort(intersect_nodes_.begin(), intersect_nodes_.end(), IntersectListSort);',
                            'std::sort(intersect_nodes_.begin(), intersect_nodes_.end(),\n'
                            '      [](const auto& a, const auto& b) { Workspace::Pulse(); return IntersectListSort(a, b); });')
        text = text.replace('{return a + path.size(); }', '{ Workspace::Pulse(); return a + path.size(); }')
        for owner in ('ClipperBase', 'ReuseableDataContainer64'):
            old = owner + '::~' + owner + '()\n  {\n    Clear();\n  }'
            assert old in text, owner
            text = text.replace(old, owner + '::~' + owner + '()\n  {\n'
                                '    Workspace::Suppress suppression;\n'
                                '    if (!Workspace::IsAborted()) Clear();\n  }')
        # Cover vertex initialization in cancellable units; no operator-new[] cookie or unmatched delete[].
        text = text.replace('new Vertex[total_vertex_count]', 'AllocateVertices(total_vertex_count)')
        text = text.replace('delete[] v;', 'Workspace::Deallocate(v);')
        anchor = '  void AddPaths_(const Paths64& paths, PathType polytype, bool is_open,'
        assert anchor in text
        helper = ('  Vertex* AllocateVertices(size_t count)\n  {\n'
                  '    static_assert(std::is_trivially_destructible_v<Vertex>);\n'
                  '    Vertex* result = ArenaAllocator<Vertex>{}.allocate(count);\n'
                  '    for (size_t index = 0; index < count; ++index)\n'
                  '    { Workspace::Pulse(); ::new (result + index) Vertex; }\n'
                  '    return result;\n  }\n\n')
        text = text.replace(anchor, helper + anchor)
    text, loop_counts = checked_loops(text)
    output = target / output_name
    output.write_text(text, encoding='utf-8', newline='\n')
    subprocess.run([str(args.clang_format), '-i', '--style=file:' + str(args.format_style), str(output)], check=True)
    manifest['files'].append({'upstream': input_name, 'upstream_sha256': sha(path),
                              'generated': output_name, 'generated_sha256': sha(output),
                              'checked_loops': loop_counts})
assert not re.search(r'\bstd::(vector|stable_sort|priority_queue)\b', (target / 'clipper.engine.cpp').read_text())
assert 'catch' not in code_mask((target / 'clipper.engine.cpp').read_text())
(target / 'generation-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(json.dumps(manifest, indent=2))
