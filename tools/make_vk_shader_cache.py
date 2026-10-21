#!/usr/bin/env python3
"""Build the Vulkan shader cache consumed by dm/gfx/nvn/nvn_vk.c.

Inputs
  capture/   <key>.control (0x880) + <key>.code   (from shader_NX.bnsh; see extract_shaders)
  spv/       <key>.spv + <key>.res.json           (from dmsh_xlate: Maxwell -> SPIR-V, TargetApi.Vulkan)

Outputs (cache dir)
  shader_index.bin   DVKI v1: header {u32 magic, u32 version, u32 count}
                     + count * { char key[16]; u32 stage; u8 control[0x880] }
  <key>.vkmeta       RHeader {u32 magic=DVKM,version,stage,nb,na,c1n,ncap,local[3],fout,reserved}
                     + nb * RBinding {u32 set,binding,type,kind; i32 slot,sampler; u32 count,flags}
                     + na * {u32 location, u32 shader_type}
                     + ncap * u32
                     + c1n bytes
  <key>.spv          patched SPIR-V (pixel-stage resources moved to descriptor set 1)

Conventions reverse-engineered from nvn_vk.c:
  - descriptor sets: vertex/compute = 0, pixel = 1 (line ~2357, pipeline layout 2 sets)
  - RBinding.kind: 1 support buffer, 2 uniform buffer, 3 storage buffer (flags=writable),
    4 texture (combined = type 1; separate = type 2 + sampler), 6 storage image,
    7 c1 constant buffer, 8 driver constant table (slot 0 or 2), 10 texel buffer
  - texture/image NVN slots decode from the handle word index against profile.nvn_bindings
  - RHeader.caps left empty: the backend then performs no device-feature checks
"""
import hashlib
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "work/shaders/capture"
SPVIN = ROOT / "work/shaders/spv"
OUT = ROOT / "work/shaders/vk-cache"

# Rhythm Heaven profile.nvn_bindings: byte ranges in NVN constant bank 2.
TEXTURE_RANGE = (0x20, 0x120)   # tex handle -> NVN texture slot
IMAGE_RANGE = (0x120, 0x168)    # (backend default; RH profile only pins textures)
SAMPLER_RANGE = (0x168, 0x568)

# Vulkan VkDescriptorType
DT_COMBINED_IMAGE_SAMPLER = 1
DT_SAMPLED_IMAGE = 2
DT_STORAGE_IMAGE = 3
DT_UNIFORM_BUFFER = 6
DT_STORAGE_BUFFER = 7

# Ryujinx AttributeType numeric values (only informational for now)
ATTR_FLOAT, ATTR_SINT, ATTR_UINT = 0, 1, 2
VERTEX_ATTRIBUTE_TYPES = {2: ATTR_UINT}   # profile.vertex_attribute_types


def decode_slot(byte_offset, span):
    if span is None:
        return None
    base, limit = span
    if base <= byte_offset < limit and (byte_offset - base) % 8 == 0:
        return (byte_offset - base) // 8
    return None


def spirv_scan(words, patch_set):
    """Return (resources, input_locations); optionally rewrite DescriptorSet to patch_set."""
    sets, bindings, locations, input_ids = {}, {}, {}, set()
    offset = 5
    while offset < len(words):
        count = words[offset] >> 16
        opcode = words[offset] & 0xFFFF
        if not count:
            raise SystemExit("malformed SPIR-V instruction stream")
        if opcode == 71:  # OpDecorate
            target, dec = words[offset + 1], words[offset + 2]
            value = words[offset + 3]
            if dec == 34:  # DescriptorSet
                if patch_set is not None and value == 0:
                    words[offset + 3] = patch_set
                    value = patch_set
                sets[target] = value
            elif dec == 33:
                bindings[target] = value
            elif dec == 30:
                locations[target] = value
        elif opcode == 59:  # OpVariable
            result, storage = words[offset + 2], words[offset + 3]
            if storage == 1:  # Input
                input_ids.add(result)
        offset += count
    resources = [dict(id=i, set=sets[i], binding=bindings[i]) for i in sorted(sets) if i in bindings]
    attrs = sorted(locations[i] for i in input_ids if i in locations)
    return resources, attrs


def main():
    res_by_key = {}
    for res_path in sorted(SPVIN.glob("*.res.json")):
        res_by_key[res_path.stem.replace(".res", "")] = json.loads(res_path.read_text())
    controls = sorted(CAPTURE.glob("*.control"))
    if not controls:
        raise SystemExit("no captured shaders in " + str(CAPTURE))
    OUT.mkdir(parents=True, exist_ok=True)
    index_entries = []
    summary = []
    for control_path in controls:
        key = control_path.stem
        control = control_path.read_bytes()
        code = control_path.with_suffix(".code").read_bytes()
        if len(control) != 0x880:
            raise SystemExit(f"bad control size {key}")
        res = res_by_key.get(key)
        if res is None:
            raise SystemExit(f"no translation for {key}")
        stage = {"vs": 0, "ps": 1, "cs": 5}[res["stage"]]
        set_index = 1 if stage == 1 else 0
        spv_path = SPVIN / (key + ".spv")
        words = list(struct.unpack("<%dI" % (spv_path.stat().st_size // 4), spv_path.read_bytes()))
        resources, input_locations = spirv_scan(words, set_index)
        spv_path.write_bytes(struct.pack("<%dI" % len(words), *words))

        cbuf_by_binding = {c["binding"]: c["slot"] for c in res.get("cbufs", [])}
        sbuf_by_binding = {s["binding"]: s for s in res.get("sbufs", [])}
        tex_by_binding = {t["binding"]: t for t in res.get("textures", [])}
        img_by_binding = {i["binding"]: i for i in res.get("images", [])}

        bindings = []
        unmatched = []
        for r in resources:
            binding = r["binding"]
            entry = dict(set=r["set"], binding=binding, type=DT_UNIFORM_BUFFER, kind=1,
                         slot=-1, sampler=-1, count=1, flags=0)
            if binding in cbuf_by_binding:
                bank = cbuf_by_binding[binding]
                if bank == 1:
                    entry.update(kind=7, slot=-1)          # c1 constant buffer
                elif bank == 0 or bank == 2:
                    entry.update(kind=8, slot=bank)        # driver constant table
                elif 3 <= bank <= 16:
                    entry.update(kind=2, slot=bank - 3)    # uniform buffer register
                else:
                    raise SystemExit(f"{key}: bank {bank} unsupported")
            elif binding in tex_by_binding:
                t = tex_by_binding[binding]
                handle_bytes = int(t["handle"]) * 4
                slot = decode_slot(handle_bytes, TEXTURE_RANGE)
                if slot is None:
                    raise SystemExit(f"{key}: texture handle {t['handle']} outside pinned range")
                entry.update(kind=4, slot=slot, sampler=-1,
                             type=DT_SAMPLED_IMAGE if t.get("separate") else DT_COMBINED_IMAGE_SAMPLER)
            elif binding in img_by_binding:
                i = img_by_binding[binding]
                slot = decode_slot(int(i["handle"]) * 4, IMAGE_RANGE)
                if slot is None:
                    slot = decode_slot(int(i["handle"]) * 4, TEXTURE_RANGE)
                if slot is None:
                    raise SystemExit(f"{key}: image handle {i['handle']} outside pinned ranges")
                entry.update(kind=6, slot=slot, type=DT_STORAGE_IMAGE)
            elif binding in sbuf_by_binding:
                s = sbuf_by_binding[binding]
                entry.update(kind=3, type=DT_STORAGE_BUFFER, slot=int(s.get("slot", 0)),
                             flags=1 if (int(s.get("flags", 0)) & 2) else 0)
            elif binding == 0:
                entry.update(kind=1, type=DT_UNIFORM_BUFFER)   # support buffer
            else:
                unmatched.append(binding)
                continue
            bindings.append(entry)
        if unmatched:
            raise SystemExit(f"{key}: unmatched descriptor bindings {unmatched} (needs mapping)")

        # c1 constant segment for kind=7 bindings (RHeader.c1n bytes follow the caps)
        c1 = b""
        if any(b["kind"] == 7 for b in bindings):
            constant, end, start = struct.unpack_from("<III", control, 0x700)
            c1 = code[constant:end]
            if len(c1) > 4096:
                raise SystemExit(f"{key}: c1 segment too large ({len(c1)})")

        attrs = [[loc, VERTEX_ATTRIBUTE_TYPES.get(loc, ATTR_FLOAT)] for loc in input_locations]
        header = dict(magic=0x4D4B5644, version=1, stage=stage, nb=len(bindings), na=len(attrs),
                      c1n=len(c1), ncap=0, local=[0, 0, 0], fout=0, reserved=0)
        blob = struct.pack("<12I", header["magic"], header["version"], header["stage"], header["nb"],
                           header["na"], header["c1n"], header["ncap"], *header["local"],
                           header["fout"], header["reserved"])
        for b in bindings:
            blob += struct.pack("<8I", b["set"], b["binding"], b["type"], b["kind"], b["slot"] & 0xFFFFFFFF,
                                b["sampler"] & 0xFFFFFFFF, b["count"], b["flags"])
        for loc, typ in attrs:
            blob += struct.pack("<2I", loc, typ)
        blob += c1
        (OUT / (key + ".vkmeta")).write_bytes(blob)
        (OUT / (key + ".spv")).write_bytes((SPVIN / (key + ".spv")).read_bytes())
        index_entries.append((key, stage, control))
        summary.append(dict(key=key, stage=res["stage"], nb=len(bindings), na=len(attrs), c1n=len(c1),
                            kinds=[(b["kind"], b["slot"], b["binding"]) for b in bindings]))

    index = struct.pack("<3I", 0x494B5644, 1, len(index_entries))
    for key, stage, control in index_entries:
        index += key.encode("ascii")[:16].ljust(16, b"\0")
        index += struct.pack("<I", stage)
        index += control
    (OUT / "shader_index.bin").write_bytes(index)
    (OUT / "cache_manifest.json").write_text(json.dumps(summary, indent=2))
    print(f"vk cache: {len(index_entries)} shaders -> {OUT}")
    for row in summary:
        print(f"  {row['key']} {row['stage']} nb={row['nb']} na={row['na']} c1n={row['c1n']} {row['kinds']}")


if __name__ == "__main__":
    sys.exit(main())
