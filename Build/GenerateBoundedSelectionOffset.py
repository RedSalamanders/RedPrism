"""Reproduce bounded Clipper2 2.0.1 offset sources into a fresh directory."""
from pathlib import Path
import argparse, ast, hashlib, json, re, subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--clang-format', type=Path, required=True)
parser.add_argument('--format-style', type=Path, required=True)
parser.add_argument('--base-engine', type=Path, required=True)
parser.add_argument('--boolean-generator', type=Path, required=True)
args = parser.parse_args()
upstream = args.source.resolve(strict=True)
base = args.base_engine.resolve(strict=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest().upper()
required = {
    'src/clipper.offset.cpp':'EBC01258953212D20FED090A4A740F94926C3C6706A092FEC4C5E9CB5292305A',
    'include/clipper2/clipper.offset.h':'00D0B670F40D030FAD5CD658B71A34B8FF3185673825BBCC9F40E63D9216CBB2',
    'include/clipper2/clipper.h':'763C56ACF64085AF2B5081CA80466210507DF3F49C53F9AAF3CCE5597ED71E36'}
for name,digest in required.items():assert sha(upstream/name)==digest,name
target = args.output.resolve()
target.mkdir(parents=True, exist_ok=False)
book = json.loads((base/'generation-manifest.json').read_text())
for item in book['files']:
    original = base/item['generated']
    assert sha(original)==item['generated_sha256']
    (target/original.name).write_bytes(original.read_bytes())
for name in ['Workspace.h','LICENSE']:
    (target/name).write_bytes((base/name).read_bytes())
# Reuse the exact lexical loop rewriter. Extract only its function definitions, never its CLI/mutations.
original_generator = args.boolean_generator.read_text(encoding='utf-8')
tree = ast.parse(original_generator)
functions = [node for node in tree.body if isinstance(node,ast.FunctionDef)]
assert {node.name for node in functions} == {'code_mask', 'pair', 'top_level', 'checked_loops'}
namespace = {'re':re}
exec(compile(ast.Module(body=functions,type_ignores=[]),'<existing lexical loop rewriter>','exec'),namespace)
wrappers = (upstream/'include/clipper2/clipper.h').read_text()
begin = wrappers.index('  template <typename T>\n  inline Path<T> Ellipse(const Point<T>& center,')
end = wrappers.index('  template <typename T>\n  inline Path<T> SimplifyPath',begin)
ellipse = wrappers[begin:end]
helpers = '''
template <class Iterator> void CheckedOffsetReverse(Iterator first, Iterator last)
{
    while (first != last && first != --last)
    {
        Workspace::Pulse();
        std::iter_swap(first++, last);
    }
}
template <typename T> void CheckedOffsetDuplicates(Path<T>& path, bool closed)
{
    path.erase(std::unique(path.begin(), path.end(), [](const auto& a, const auto& b)
        { Workspace::Pulse(); return a == b; }), path.end());
    if (closed)
        while (path.size() > 1 && path.back() == path.front()) path.pop_back();
}
'''
records=[]
formatter = args.clang_format.resolve(strict=True)
assert formatter.is_file()
for name,outname in [('include/clipper2/clipper.offset.h','clipper.offset.h'),('src/clipper.offset.cpp','clipper.offset.cpp')]:
    text = (upstream/name).read_text()
    text=text.replace('Clipper2Lib','RedPrismBoundedClipper').replace('CLIPPER_OFFSET_H_','REDPRISM_BOUNDED_CLIPPER_OFFSET_H_')
    text=text.replace('"clipper2/clipper.','"clipper.').replace('std::vector','ArenaVector')
    if outname.endswith('.h'):
        assert text.count('ArenaVector<Group> groups_;') == 1
        text=text.replace('ArenaVector<Group> groups_;','ArenaVector<Group> groups_{std::initializer_list<Group>{}};')
    if outname.endswith('.cpp'):
        assert text.count('#include "clipper.h"')==1
        text=text.replace('#include "clipper.h"\n','')
        anchor='const double floating_point_tolerance = 1e-12;'
        assert text.count(anchor)==1
        text=text.replace(anchor,ellipse+helpers+anchor)
        old='for (const Point64& pt : paths[i])'
        assert text.count(old)==1
        text=text.replace(old,'const auto& path = paths[i];\n\t\tfor (const Point64& pt : path)')
        assert text.count('StripDuplicates(p, is_joined);')==1
        text=text.replace('StripDuplicates(p, is_joined);','CheckedOffsetDuplicates(p, is_joined);')
        assert text.count('std::reverse(')==2
        text=text.replace('std::reverse(','CheckedOffsetReverse(')
        # This insignificant-delta branch is outside the command API, but retain bounded iteration too.
        old='copy(group.paths_in.begin(), group.paths_in.end(), back_inserter(*solution));'
        assert text.count(old)==1
        text=text.replace(old,'for (const auto& path : group.paths_in) solution->push_back(path);')
        # Every cleanup union failure must propagate through the existing scheduler allocation boundary.
        for value in ['*solution_tree','*solution']:
            for rule in ['Negative','Positive']:
                old=f'c.Execute(ClipType::Union, FillRule::{rule}, {value});'
                assert text.count(old)==1
                text=text.replace(old,f'{{ if (!c.Execute(ClipType::Union, FillRule::{rule}, {value})) Workspace::Refuse(); }}')
    text,counts=namespace['checked_loops'](text)
    output=target/outname
    output.write_text(text,encoding='utf-8',newline='\r\n')
    subprocess.run([str(formatter),'-i','--style=file:'+str(args.format_style.resolve(strict=True)),str(output)],check=True)
    records.append({'upstream':name,'upstream_sha256':sha(upstream/name),'generated':outname,
        'generated_sha256':sha(output),'checked_loops':counts})
with (target/'offset-generation-manifest.json').open('x') as stream:
    json.dump({'status':'GENERATED_ONLY','files':records,'existing_boolean_manifest_sha256':sha(base/'generation-manifest.json'),
        'numerical_policy':'Normalize at binary128; offset at decimal100; cleanup original Clipper64; Expand clips at binary128.',
        'scope':'Deterministic authoring provenance only; runtime/admission/cancellation require separate tests',
        'boolean_generator_sha256':sha(args.boolean_generator), 'generator_sha256':sha(Path(__file__)), 'runtime_qualified':False},stream,indent=2);stream.write('\n')
print(json.dumps(records,indent=2))
