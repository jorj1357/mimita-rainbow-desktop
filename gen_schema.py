#!/usr/bin/env python3
"""
Reads effects_schema.jsonc and regenerates the sentinel-delimited sections
in src/shaders.h, src/config.cpp, src/settings.cpp, and src/main.cpp.

Usage: python gen_schema.py
"""

import json, re, os, pathlib

ROOT = pathlib.Path(__file__).resolve().parent

# ── helpers ──

def strip_jsonc(text):
    """Remove // and /* */ comments from JSONC text."""
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    return text

def load_schema():
    with open(ROOT / 'effects_schema.jsonc', 'r', encoding='utf-8') as f:
        return json.loads(strip_jsonc(f.read()))

def cfg_field_name(field):
    """Derive the AppConfig member name from the shader field name."""
    return field[2:]  # strip "g_" prefix

def default_str(d):
    if isinstance(d, bool):
        return 'true' if d else 'false'
    if isinstance(d, float):
        return f'{d}f' if d != int(d) else f'{d}.0f'
    if isinstance(d, int):
        return str(d)
    if isinstance(d, str):
        return f'std::string("{d}")'
    return str(d)

def default_cpp(d, type_hint):
    if isinstance(d, bool):
        return 'true' if d else 'false'
    if isinstance(d, float):
        return f'{d}f' if '.' in str(d) else f'{d}.0f'
    if isinstance(d, int):
        return str(d)
    if isinstance(d, str):
        return f'"{d}"'
    return str(d)

# ── section generators ──

def gen_shader_struct(schema):
    lines = ['struct ShaderConstants {']
    for e in schema:
        lines.append(f'    {e["type"]} {e["field"]};')
    lines.append('};')
    lines.append('')
    # static_assert for total size
    lines.append('static_assert(sizeof(ShaderConstants) <= 256, "ShaderConstants too large for HLSL cbuffer");')
    # per-field offset checks
    offset = 0
    for e in schema:
        lines.append(f'static_assert(offsetof(ShaderConstants, {e["field"]}) == {offset}, "{e["field"]} offset mismatch");')
        offset += 4  # both float and int are 4 bytes in HLSL cbuffer
    return '\n'.join(lines)

def gen_hlsl_cbuffer(schema):
    """Generate the cbuffer declaration inside PS_SRC, 4-per-line."""
    lines = ['cbuffer Constants : register(b0) {']
    buf = []
    for e in schema:
        buf.append(f'{e["type"]} {e["field"]}')
    i = 0
    while i < len(buf):
        chunk = buf[i:i+4]
        lines.append('    ' + '; '.join(chunk) + ';')
        i += 4
    lines.append('};')
    return '\n'.join(lines)

def gen_load_config(schema):
    """Generate the field-read lines inside LoadConfig."""
    lines = []
    for e in schema:
        if 'cfg' not in e or e['cfg'] is None:
            continue
        section, key = e['cfg']
        cfg_f = e.get('cfg_field', cfg_field_name(e['field']))
        d = e.get('def', False)
        is_computed = e.get('computed_load', False)

        if is_computed and 'map' in e:
            mp = e['map']
            if isinstance(mp, dict):
                # enum map: string → int, default from def
                lines.append(f'        std::string cm = gb("{section}","{key}", std::string("{d}"));')
                case_lines = []
                for k, v in mp.items():
                    case_lines.append(f'cm == "{k}" ? {v}')
                def_val = mp.get(str(d) if isinstance(d, str) else d, 0)
                lines.append(f'        cfg.{cfg_f} = ' + ' : '.join(case_lines) + f' : {def_val};')
            elif isinstance(mp, list):
                # list map: index 0 = first entry, also store string for main.cpp lookup
                lines.append(f'        std::string bm = gb("{section}","{key}", std::string("{mp[0]}"));')
                lines.append(f'        cfg.{cfg_f} = 0;')
                for i, val in enumerate(mp):
                    lines.append(f'        if (bm == "{val}") cfg.{cfg_f} = {i};')
                lines.append(f'        cfg.blend_mode = bm;')
        elif is_computed and e['field'] == 'g_trail_decay':
            # trail_decay: computed from trail_frames in main.cpp, not LoadConfig
            continue
        else:
            # standard field read using gb lambda
            if e.get('is_bool'):
                lines.append(f'        cfg.{cfg_f} = gb("{section}","{key}", {default_cpp(d, bool)});')
            else:
                lines.append(f'        cfg.{cfg_f} = gb("{section}","{key}", {default_cpp(d, float)});')
    # trail_frames: needed for g_trail_decay computation, no shader field
    lines.append('        cfg.trail_frames = (int)gb("motion_trail","frames", 10);')
    return '\n'.join(lines)

def gen_write_config(schema):
    """Generate the per-effect JSON write statements for WriteConfig."""
    lines = []
    written = set()
    for e in schema:
        if 'cfg' not in e or e['cfg'] is None:
            continue
        section, key = e['cfg']
        if (section, key) in written:
            continue
        written.add((section, key))

        is_computed = e.get('computed_load', False)

        if is_computed and 'combo' in e:
            # combo box: blend mode
            combo_field = e['combo']
            mp = e['map']
            map_arr = mp if isinstance(mp, list) else []
            if map_arr:
                arr_str = ','.join(f'"{v}"' for v in map_arr)
                lines.append(f'        int sel = GetComboSel("{combo_field}");')
                lines.append(f'        static const char* MN[] = {{{arr_str}}};')
                lines.append(f'        if (sel >= 0 && sel < {len(map_arr)}) {{')
                lines.append(f'            j["effects"]["{section}"]["enabled"] = sel > 0;')
                lines.append(f'            j["effects"]["{section}"]["mode"] = MN[sel];')
                lines.append(f'        }}')
            continue
        if is_computed:
            continue

        # Regular field
        ui_check = e.get('ui_check')
        ui_edit = e.get('ui_edit')

        if ui_check:
            lines.append(f'        j["effects"]["{section}"]["{key}"] = GetCheckValue("{ui_check}");')
        elif ui_edit:
            lines.append(f'        j["effects"]["{section}"]["{key}"] = GetEditValue("{ui_edit}");')
        else:
            # static value from schema default
            d = e.get('def', False)
            if isinstance(d, bool):
                lines.append(f'        j["effects"]["{section}"]["{key}"] = {str(d).lower()};')
            else:
                lines.append(f'        j["effects"]["{section}"]["{key}"] = {d};')

    # trail_frames: AppConfig-only, no shader field, compute into g_trail_decay
    lines.append('')
    lines.append('        // motion_trail frames (computed into g_trail_decay, no shader field)')
    lines.append('        int tf = (int)GetEditValue("trail_frames");')
    lines.append('        j["effects"]["motion_trail"]["frames"] = tf;')

    return '\n'.join(lines)

def gen_populate_shader(schema):
    """Generate the ShaderConstants population block in main.cpp."""
    lines = ['        ShaderConstants c = {};']
    for e in schema:
        f = e['field']
        if f == 'g_time':
            lines.append(f'        c.{f} = t;')
            continue
        if f == 'g_pad2':
            lines.append(f'        c.{f} = 0;')
            continue

        cfg_f = e.get('cfg_field', cfg_field_name(f))
        is_computed = e.get('computed_load', False)

        if is_computed and e['field'] == 'g_blend_enabled':
            lines.append('')
            lines.append(f'        // blend mode string → int')
            lines.append(f'        {{')
            mp = e.get('map', [])
            arr_str = ','.join(f'"{v}"' for v in mp)
            lines.append(f'            static const char* MN[] = {{{arr_str}}};')
            lines.append(f'            c.{f} = 0;')
            lines.append(f'            for (int i = 0; i < {len(mp)}; i++) {{ if (cfg.blend_mode == MN[i] && i > 0) {{ c.{f} = i; break; }} }}')
            lines.append(f'        }}')
            continue
        if is_computed and e['field'] == 'g_chroma_mode':
            lines.append(f'        c.{f} = cfg.{cfg_f};')
            continue
        if is_computed and e['field'] == 'g_trail_decay':
            lines.append('        float trailDecay = (float)cfg.trail_frames > 0 ? powf(0.001f, 1.0f / (float)cfg.trail_frames) : 0.0f;')
            lines.append('        c.g_trail_decay = trailDecay;')
            continue

        # Regular field
        if e.get('is_bool'):
            lines.append(f'        c.{f} = cfg.{cfg_f} ? 1 : 0;')
        else:
            lines.append(f'        c.{f} = cfg.{cfg_f};')
    return '\n'.join(lines)

# ── file patching ──

def replace_section(path, marker, content):
    """Replace text between // @@GEN_{marker}_BEGIN@@ and // @@GEN_{marker}_END@@ markers."""
    begin = f'// @@GEN_{marker}_BEGIN@@'
    end   = f'// @@GEN_{marker}_END@@'
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    bpos = text.find(begin)
    epos = text.find(end)
    if bpos == -1 or epos == -1:
        print(f"  WARN: markers {begin}/{end} not found in {path}, skipping")
        return
    bpos = text.index('\n', bpos) + 1  # start after begin line
    before = text[:bpos]
    after = text[epos:]
    with open(path, 'w', encoding='utf-8') as f:
        f.write(before)
        f.write(content)
        if not content.endswith('\n'):
            f.write('\n')
        f.write(after)
    print(f"  OK: {path} [{marker}]")

# ── main ──

def main():
    schema = load_schema()
    print(f"Loaded {len(schema)} fields from effects_schema.jsonc")

    shaders_h  = ROOT / 'src' / 'shaders.h'
    config_cpp = ROOT / 'src' / 'config.cpp'
    settings_cpp = ROOT / 'src' / 'settings.cpp'
    main_cpp   = ROOT / 'src' / 'main.cpp'

    # 1. shaders.h: C++ struct
    struct_content = gen_shader_struct(schema)
    replace_section(shaders_h, 'SHADER_STRUCT', struct_content)

    # 2. shaders.h: HLSL cbuffer
    cbuffer_content = gen_hlsl_cbuffer(schema)
    replace_section(shaders_h, 'HLSL_CBUFFER', cbuffer_content)

    # 3. config.cpp: LoadConfig
    load_content = gen_load_config(schema)
    replace_section(config_cpp, 'LOAD_CONFIG', load_content)

    # 4. settings.cpp: WriteConfig
    write_content = gen_write_config(schema)
    replace_section(settings_cpp, 'WRITE_CONFIG', write_content)

    # 5. main.cpp: ShaderConstants population
    populate_content = gen_populate_shader(schema)
    replace_section(main_cpp, 'POPULATE_SHADER', populate_content)

    print("\nDone. Only edit effects_schema.jsonc — never touch the generated sections.")

if __name__ == '__main__':
    main()
