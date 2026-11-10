"""Assemble HS2 split KIPs using exactly the linked upstream source defaults."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import build

ROOT = Path(__file__).resolve().parents[1]
QA = ROOT / 'evidence'
STAGE = ROOT / 'staging'
sha = lambda b: hashlib.sha256(b).hexdigest()


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        assert self.data[:6] == b'\x7fELF\x02\x01'
        assert self.unpack('<H', 18)[0] == 183
        phoff, shoff = self.unpack('<QQ', 32)
        phsize, phnum, shsize, shnum, names_index = self.unpack('<5H', 54)
        assert phsize == 56 and shsize == 64
        self.loads = []
        for i in range(phnum):
            row = self.unpack('<IIQQQQQQ', phoff + i * phsize)
            if row[0] == 1:
                self.loads.append({'offset': row[2], 'address': row[3],
                    'file_size': row[5], 'memory_size': row[6], 'flags': row[1]})
        headers = [self.unpack('<IIQQQQIIQQ', shoff + i * shsize) for i in range(shnum)]
        name_section = headers[names_index]
        names = self.slice(name_section[4], name_section[5])
        self.sections = {}
        for row in headers:
            name = names[row[0]:].split(b'\0', 1)[0].decode()
            self.sections[name] = {'type': row[1], 'flags': row[2], 'address': row[3],
                'offset': row[4], 'size': row[5], 'link': row[6], 'entry_size': row[9]}
        self.symbols = {}
        symbols = self.sections['.symtab']
        strings_header = headers[symbols['link']]
        strings = self.slice(strings_header[4], strings_header[5])
        assert symbols['entry_size'] == 24
        for pos in range(symbols['offset'], symbols['offset'] + symbols['size'], 24):
            name, info, other, section, address, size = self.unpack('<IBBHQQ', pos)
            key = strings[name:].split(b'\0', 1)[0].decode()
            if key and section:
                self.symbols[key] = {'address': address, 'size': size, 'type': info & 15}

    def slice(self, offset, size):
        assert 0 <= offset <= offset + size <= len(self.data)
        return self.data[offset:offset + size]

    def unpack(self, pattern, offset):
        return struct.unpack(pattern, self.slice(offset, struct.calcsize(pattern)))

    def at(self, address, size):
        segment = next(p for p in self.loads if p['address'] <= address
                       and address + size <= p['address'] + p['file_size'])
        return self.slice(segment['offset'] + address - segment['address'], size)

    def section_bytes(self, name):
        section = self.sections[name]
        return self.slice(section['offset'], section['size'])


def verify_build(name):
    record = json.loads((QA / (name + '-build.json')).read_text())
    assert record['passed']
    module = ROOT / 'Atmosphere/stratosphere' / name
    assert build.inventory(module) == record['source_files']
    assert build.inventory(ROOT / 'common') == record['shared_files']
    for path, digest in record['artifacts'].items():
        assert sha(Path(path).read_bytes()) == digest
    return module / 'out/nintendo_nx_arm64_armv8a/e1_release'


def stack_frames(module):
    frames = []
    for path in (ROOT / 'Atmosphere/stratosphere' / module / 'build/nintendo_nx_arm64_armv8a/e1_release').glob('*.su'):
        for line in path.read_text().splitlines():
            parts = line.rsplit('\t', 2)
            if len(parts) == 3:
                frames.append({'frame': int(parts[1]), 'function': parts[0], 'kind': parts[2]})
    return sorted(frames, key=lambda x: x['frame'], reverse=True)[:15]


def main():
    build.verify_dependencies()
    helper_out, loader_out = verify_build('hoc_patch'), verify_build('loader')
    helper, loader = Elf(helper_out / 'hoc_patch.elf'), Elf(loader_out / 'loader.elf')
    symbol = '_ZN3ams3ldr3hoc1CE'
    assert symbol not in loader.symbols and 'hoc_hookpayload' not in loader.sections
    entry = helper.symbols[symbol]
    assert entry['size'] == 0x68c8
    default = helper.at(entry['address'], entry['size'])
    cust = default
    assert default[:12] == cust[:12] == b'CUST' + struct.pack('<II', 7, 260)
    assert not (ROOT / 'inputs').exists(), 'No personal settings input belongs in HS2.'
    header = bytearray((helper_out / 'hoc_patch.kip').read_bytes()[:0x100])
    assert header[:4] == b'KIP1'
    assert struct.unpack_from('<Q', header, 16)[0] == 0x4200000000484F43
    header[31] &= ~7
    chunks, cust_offset = [], None
    for index in range(3):
        address, size, stored, _ = struct.unpack_from('<4I', header, 0x20 + index * 16)
        load = next(p for p in helper.loads if p['address'] == address)
        assert load['file_size'] <= size
        chunk = helper.at(address, load['file_size']) + bytes(size - load['file_size'])
        if address <= entry['address'] and entry['address'] + len(cust) <= address + size:
            cust_offset = 0x100 + sum(map(len, chunks)) + entry['address'] - address
        chunks.append(chunk)
        struct.pack_into('<I', header, 0x28 + index * 16, size)
    assert cust_offset is not None
    template = bytes(header) + b''.join(chunks)
    assert template[cust_offset:cust_offset + len(cust)] == default
    final = bytearray(template)
    final[cust_offset:cust_offset + len(cust)] = cust
    restored = bytearray(final)
    restored[cust_offset:cust_offset + len(cust)] = default
    assert bytes(restored) == template and final[31] & 7 == 0

    payload = helper.section_bytes('hoc_hookpayload')
    reference = json.loads((QA / 'baseline-hook-payload.json').read_text())
    assert reference['passed'] and reference['source'] == 'baseline-artifacts/loader.elf'
    assert payload == bytes.fromhex(reference['payload_hex'])
    assert sha(payload) == reference['payload_sha256']
    assert len(payload) == 40 and payload[:8] == bytes(8)
    assert struct.unpack('<8I', payload[8:]) == (0xd503201f, 0xd503201f,
        0x10ffff81, 0xf9400021, 0xb9400822, 0xb9000002, 0xf9400020, 0xd65f03c0)
    # Six copied instructions: ADR references its own pointer slot at -16;
    # the other five are indirect data loads/stores and RET, with no external
    # branch, ADRP, or literal-load reference. The slot is populated by the
    # unchanged HookPayload relocator with the target runtime-data address.
    hook_function = helper.symbols['_ZN3ams3ldr3hoc3pcv6erista21GetEristaMtcTableImplEPj']
    assert hook_function['address'] == helper.sections['hoc_hookpayload']['address'] + 16
    assert hook_function['size'] == 24

    loader_kip = (loader_out / 'loader.kip').read_bytes()
    assert loader_kip[:4] == b'KIP1' and struct.unpack_from('<Q', loader_kip, 16)[0] == 0x0100000000000001
    target = STAGE / 'atmosphere/nexus-hoc'
    target.mkdir(parents=True, exist_ok=True)
    (target / 'hoc.kip').write_bytes(final)
    (target / 'nexus-loader.kip').write_bytes(loader_kip)
    app = ROOT / 'diagnostics/nexus-hoc-status.nro'
    app_target = STAGE / 'switch/nexus-hoc-split/nexus-hoc-status.nro'
    app_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(app, app_target)
    clock_out = ROOT / 'hoc-clk/sysmodule/out/hoc-clk.nsp'
    clock_target = STAGE / 'atmosphere/contents/00FF0000636C6BFF/exefs.nsp'
    clock_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(clock_out, clock_target)
    for name, artifact in (('status', app), ('hoc-clk', clock_out)):
        receipt = json.loads((QA / (name + '-build.json')).read_text())
        assert receipt['passed'] and receipt['artifacts'][artifact.as_posix()] == sha(artifact.read_bytes())
    overlay = ROOT / 'hoc-clk/overlay/out/horizon-oc-overlay.ovl'
    overlay_target = STAGE / 'switch/.overlays/horizon-oc-overlay.ovl'
    overlay_receipt = json.loads((QA / 'overlay-build.json').read_text())
    assert overlay_receipt['passed'] and overlay_receipt['artifacts'][overlay.as_posix()] == sha(overlay.read_bytes())
    overlay_elf = (overlay.parent / 'horizon-oc-overlay.elf').read_bytes()
    assert b'UpdateGui' not in overlay_elf
    assert b'releases/latest/download/dist.zip' not in overlay_elf
    assert b'releases/latest/download/dist_ext.zip' not in overlay_elf
    overlay_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(overlay, overlay_target)
    overlay_library = ROOT / 'hoc-clk/overlay/lib/libultrahand'
    for relative in ('LICENSE', 'SUB_LICENSE', 'libtesla/LICENSE', 'libultra/LICENSE', 'libultra/SUB_LICENSE'):
        destination = STAGE / 'licenses/libultrahand' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(overlay_library / relative, destination)
    record = {'passed': True, 'hardware_verified': False, 'personal_cust_preserved': False,
        'cust_policy': 'Exact upstream HOC 2.6.0 source defaults; no personal table imported.',
        'cust_bytes': len(cust), 'cust_offset': cust_offset, 'cust_sha256': sha(cust),
        'identical_to_elf_template': bytes(final) == template, 'helper_all_segments_uncompressed': True,
        'loader_has_no_oc_engine': True, 'distinct_process_ids': True,
        'hook_payload': {'bytes': len(payload), 'sha256': sha(payload),
            'baseline_byte_identical': True, 'function_instructions': 6,
            'pc_relative_targets_closed': True, 'runtime_pointer_slot_offset': 0},
        'elfs': {p.path.as_posix(): sha(p.data) for p in (helper, loader)},
        'resident_load_bytes': {n: sum(x['memory_size'] for x in e.loads) for n, e in [('loader', loader), ('hoc_patch', helper)]},
        'largest_local_stack_frames': {n: stack_frames(n) for n in ('loader', 'hoc_patch')},
        'artifacts': {p.relative_to(STAGE).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())}
                      for p in (target / 'hoc.kip', target / 'nexus-loader.kip', app_target, clock_target, overlay_target)}}
    (QA / 'assembly-validation.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({k: record[k] for k in ('passed', 'cust_sha256', 'resident_load_bytes', 'artifacts')}, indent=2))


if __name__ == '__main__':
    main()
