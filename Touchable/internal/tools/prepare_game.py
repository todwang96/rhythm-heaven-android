#!/usr/bin/env python3
"""Inspect or extract a local Switch base NSP, optionally with an update. Do not modify the source."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

from game_formats import GameError, read_exact
from nca_header import gamecard_header
from workspace import ROOT, DEPENDENCIES, UPSTREAM, load_game
SIGNATURE_SIZES = {
    0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
    0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80, 0x10006: 0x40,
}


def read_pfs0(package):
    """Read the bounded file table. Reject unsafe names and overlapping data."""
    total = package.stat().st_size
    with package.open("rb") as stream:
        magic, count, strings_size, reserved = struct.unpack("<4sIII", read_exact(stream, 16))
        if magic != b"PFS0" or reserved or not 0 < count <= 10000:
            raise GameError("The input is not a supported PFS0 NSP.")
        if not 0 < strings_size <= 4 * 1024 * 1024:
            raise GameError("The PFS0 string table size is invalid.")
        records = [struct.unpack("<QQII", read_exact(stream, 24)) for _ in range(count)]
        strings = read_exact(stream, strings_size)
        base = stream.tell()
        entries, names = [], set()
        for offset, size, name_offset, padding in records:
            if padding or name_offset >= len(strings):
                raise GameError("The PFS0 entry is invalid.")
            end = strings.find(b"\0", name_offset)
            if end < 0:
                raise GameError("A PFS0 file name has no terminator.")
            try:
                name = strings[name_offset:end].decode("utf-8")
            except UnicodeDecodeError as exc:
                raise GameError("A PFS0 file name is not UTF-8.") from exc
            if (not name or name in (".", "..") or "/" in name or "\\" in name
                    or ":" in name or any(ord(c) < 32 for c in name)
                    or name.casefold() in names):
                raise GameError("The NSP has an unsafe or duplicate file name.")
            names.add(name.casefold())
            if base + offset + size > total:
                expected = max(base + o + s for o, s, _, _ in records)
                raise GameError("The NSP is truncated: its file table needs " + str(expected)
                                + " bytes, but the file has " + str(total)
                                + ". Copy or dump the complete file again.")
            entries.append({"name": name, "offset": base + offset, "size": size})
        previous_end = base
        for entry in sorted(entries, key=lambda e: e["offset"]):
            if entry["offset"] < previous_end:
                raise GameError("The NSP has overlapping entries.")
            previous_end = entry["offset"] + entry["size"]
    return entries


def ticket_data(data):
    """Return ticket fields. Keep the encrypted title key out of reports."""
    if len(data) < 4:
        raise GameError("The ticket is truncated.")
    signature = struct.unpack_from("<I", data)[0]
    base = SIGNATURE_SIZES.get(signature)
    if base is None or len(data) < base + 0x170:
        raise GameError("The ticket has an unsupported layout.")
    return {
        "rights_id": data[base + 0x160:base + 0x170].hex(),
        "key_type": data[base + 0x141],
        "key_generation": data[base + 0x145],
        "encrypted_title_key": data[base + 0x40:base + 0x50].hex(),
    }


def inspect(package):
    entries = read_pfs0(package)
    tickets = []
    with package.open("rb") as stream:
        for entry in entries:
            if entry["name"].endswith(".nca") and entry["size"] >= 0x204:
                stream.seek(entry["offset"] + 0x200)
                entry["clear_nca_header"] = read_exact(stream, 4) in (b"NCA0", b"NCA2", b"NCA3")
            if entry["name"].endswith(".tik"):
                if entry["size"] > 65536:
                    raise GameError("The ticket is too large.")
                stream.seek(entry["offset"])
                ticket = ticket_data(read_exact(stream, entry["size"]))
                tickets.append({k: v for k, v in ticket.items() if k != "encrypted_title_key"})
    return {
        "package": str(package), "bytes": package.stat().st_size,
        "container": "PFS0", "entries": entries, "tickets": tickets,
        "validation": "Container bounds only. Game identity, content hashes, and signatures are not yet verified.",
    }


def run_hactool(hactool, keys, arguments, encrypted_title_key=None):
    command = [str(hactool), "-k", str(keys), "--disablekeywarns", "--suppresskeys"]
    if encrypted_title_key:
        command.append("--titlekey=" + encrypted_title_key)
    command.extend(str(a) for a in arguments)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # Do not print raw tool output or command arguments. They can contain keys.
    if result.returncode:
        known_errors = (
            "Invalid NCA header! Are keys correct?", "Failed to get PFS0 header size!",
            "Failed to read PFS0 header!", "Failed to read RomFS header!",
            "Failed to read NCA header!", "Unable to match rights id to titlekey.",
        )
        detail = next((s for s in known_errors if s in result.stdout), "Content read failed.")
        raise GameError("hactool: " + detail)
    return result.stdout


def write_nca_keyset(source, destination):
    """Use only NCA keys. Do not pass unrelated console keys to hactool."""
    sizes = {
        "header_key": 64, "header_key_source": 64, "header_kek_source": 32,
        "aes_kek_generation_source": 32, "aes_key_generation_source": 32,
        "key_area_key_application_source": 32, "key_area_key_ocean_source": 32,
        "key_area_key_system_source": 32, "titlekek_source": 32,
    }
    selected = {}
    for line in source.read_text(encoding="utf-8-sig").splitlines():
        name, separator, value = line.partition("=")
        name, value = name.strip().lower(), value.split("#", 1)[0].strip()
        if not separator:
            continue
        expected = sizes.get(name)
        if re.fullmatch(r"(?:master_key|titlekek|key_area_key_(?:application|ocean|system))_[0-9a-f]{2}", name):
            expected = 32
        if expected is None:
            continue
        if len(value) != expected or not re.fullmatch(r"[0-9a-fA-F]+", value):
            raise GameError("Invalid NCA key field: " + name + ". The original key file was not changed.")
        if name in selected and selected[name] != value.lower():
            raise GameError("Conflicting NCA key field: " + name)
        selected[name] = value.lower()
    if "header_key" not in selected:
        raise GameError("The key set has no header_key.")
    fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as stream:
        for name, value in sorted(selected.items()):
            stream.write(name + " = " + value + "\n")
    return bytes.fromhex(selected["header_key"])


def copy_ncas(package, ncas, nca_dir, header_key):
    """Copy NCA entries and check each SHA-256 against its content ID."""
    content_hashes, gamecard_conversions = {}, []
    with package.open("rb") as stream:
        for entry in ncas:
            stream.seek(entry["offset"])
            head = read_exact(stream, min(0xC00, entry["size"]))
            original = gamecard_header(head, header_key)
            digest, remaining = hashlib.sha256(head), entry["size"] - len(head)
            restored = hashlib.sha256(original) if original else None
            with (nca_dir / entry["name"]).open("xb") as target:
                target.write(head)
                while remaining:
                    chunk = read_exact(stream, min(4 * 1024 * 1024, remaining))
                    target.write(chunk)
                    digest.update(chunk)
                    if restored: restored.update(chunk)
                    remaining -= len(chunk)
            value = digest.hexdigest()
            if value[:32] != entry["name"][:32].lower():
                # A cartridge dump converted to NSP differs only in the distribution type byte.
                if not restored or restored.hexdigest()[:32] != entry["name"][:32].lower():
                    raise GameError("An NCA hash does not match its content ID.")
                value = restored.hexdigest()
                gamecard_conversions.append(entry["name"])
            content_hashes[entry["name"]] = value
    return content_hashes, gamecard_conversions


def package_tickets(package, entries):
    """Return {rights ID: encrypted title key} for the common tickets in a package."""
    titles = {}
    with package.open("rb") as stream:
        for entry in entries:
            if entry["name"].endswith(".tik"):
                if entry["size"] > 65536:
                    raise GameError("The ticket is too large.")
                stream.seek(entry["offset"])
                ticket = ticket_data(read_exact(stream, entry["size"]))
                if ticket["key_type"] != 0:
                    raise GameError("This script requires a common ticket or an already extracted game.")
                rights = ticket["rights_id"]
                if rights in titles and titles[rights] != ticket["encrypted_title_key"]:
                    raise GameError("The NSP contains conflicting tickets.")
                titles[rights] = ticket["encrypted_title_key"]
    return titles


def package_ncas(package):
    entries = read_pfs0(package)
    ncas = [e for e in entries if e["name"].endswith(".nca")]
    if not ncas or any(not re.fullmatch(r"[a-fA-F0-9]{32}(?:\.cnmt)?\.nca", e["name"]) for e in ncas):
        raise GameError("The NSP has no supported NCA file set.")
    return entries, ncas


def extract(package, keys, hactool, output, title_id, game_id):
    if not keys.is_file():
        raise GameError("The prod.keys file was not found.")
    if not hactool.is_file() or not os.access(hactool, os.X_OK):
        raise GameError("Build hactool first: tools/bootstrap.sh")
    if output.exists():
        raise GameError("The output path already exists. Select a new --out path.")
    entries, ncas = package_ncas(package)
    titles = package_tickets(package, entries)
    game_tickets = [key for rights, key in titles.items() if rights.startswith(title_id)]
    if len(game_tickets) > 1:
        raise GameError("Expected one ticket for this base game.")
    header_title_key = game_tickets[0] if game_tickets else None
    output.parent.mkdir(parents=True, exist_ok=True)
    required = max(package.stat().st_size * 8, 20 * 1024**3)
    if shutil.disk_usage(output.parent).free < required:
        raise GameError("Extraction requires at least 20 GiB or eight times the package size in free space.")
    with tempfile.TemporaryDirectory(prefix="." + game_id + "-extract-", dir=output.parent) as temporary:
        temp = Path(temporary)
        nca_keys = temp / "nca.keys"
        header_key = write_nca_keyset(keys, nca_keys)
        nca_dir = temp / "nca"
        nca_dir.mkdir()
        print("Copy and hash the NCA files...", flush=True)
        content_hashes, gamecard_conversions = copy_ncas(package, ncas, nca_dir, header_key)
        programs = []
        for entry in ncas:
            nca = nca_dir / entry["name"]
            # hactool also parses sections in info mode. Supply the base ticket.
            info = run_hactool(hactool, nca_keys, ["-i", "-t", "nca", nca], header_title_key)
            content = re.search(r"^Content Type:\s*(\S+)", info, re.M)
            title = re.search(r"^Title ID:\s*([0-9a-fA-F]{16})", info, re.M)
            if not content or not title:
                raise GameError("An NCA header could not be decrypted. Check prod.keys.")
            if content[1] != "Program":
                continue
            if title[1].lower() != title_id:
                raise GameError("The NSP contains a Program for a different game.")
            rights = re.search(r"^Rights ID:\s*([0-9a-fA-F]{32})", info, re.M)
            title_key = titles.get(rights[1].lower()) if rights else None
            if rights and not title_key:
                raise GameError("The NSP has no matching common ticket for its Program.")
            if "BKTR" in info:
                raise GameError("This script supports a base NSP. Updates require separate patch handling.")
            programs.append((nca, title_key))
        if len(programs) != 1:
            raise GameError("Expected exactly one base Program NCA.")
        stage = temp / "game"
        stage.mkdir()
        nca, title_key = programs[0]
        print("Decrypt ExeFS and the raw RomFS section...", flush=True)
        raw_section, header_path = temp / "romfs.section", temp / "nca.header"
        info = run_hactool(hactool, nca_keys, [
            "-r", "-t", "nca", "--exefsdir=" + str(stage / "ExeFS"),
            "--romfs=" + str(raw_section), "--header=" + str(header_path), nca,
        ], title_key)
        if "BKTR" in info:
            raise GameError("Patch content is not supported by this base-game extractor.")
        header = header_path.read_bytes()
        fs_headers = [header[0x400 + 512 * i:0x600 + 512 * i] for i in range(4)
                      if struct.unpack_from("<I", header, 0x240 + 16 * i)[0]
                      and header[0x402 + 512 * i] == 0]
        if len(fs_headers) != 1:
            raise GameError("Expected one RomFS section in the Program.")
        print("Expand the RomFS storage layer...", flush=True)
        from romfs_storage import expand_romfs
        romfs_image = temp / "romfs.bin"
        compression = expand_romfs(raw_section, fs_headers[0], romfs_image)
        print("Extract the expanded RomFS files...", flush=True)
        run_hactool(hactool, nca_keys, ["-t", "romfs", "--outdir=" + str(stage / "RomFS"), romfs_image])
        main = stage / "ExeFS/main"
        if not main.is_file():
            raise GameError("Extraction did not produce ExeFS/main.")
        with main.open("rb") as stream:
            if stream.read(4) != b"NSO0":
                raise GameError("ExeFS/main is not an NSO module.")
        romfs = stage / "RomFS"
        if not romfs.is_dir() or not any(p.is_file() for p in romfs.rglob("*")):
            raise GameError("Extraction did not produce game assets.")
        report = {
            "game_id": game_id, "title_id": title_id, "package": package.name,
            "program_nca": nca.name, "content_sha256": content_hashes,
            "gamecard_conversion": sorted(gamecard_conversions),
            "romfs_storage": compression,
            "status": "extracted; no iOS compatibility has been established",
        }
        (stage / "extraction.json").write_text(json.dumps(report, indent=2) + "\n")
        stage.rename(output)
    print("Extracted to " + str(output))


def program_ncas(nca_dir, ncas, header_key, title_ids):
    """Return {content type: [(path, title ID, rights ID)]} from the decrypted NCA headers."""
    from patch_storage import decrypt_header
    found = {}
    for entry in ncas:
        path = nca_dir / entry["name"]
        header = decrypt_header(path, header_key)
        title = "%016x" % struct.unpack_from("<Q", header, 0x210)[0]
        if title not in title_ids:
            raise GameError("The package contains content for a different game: " + title)
        found.setdefault(header[0x205], []).append((path, title, header[0x230:0x240].hex()))
    return found


def read_meta(hactool, nca_keys, meta_nca, directory):
    """Return the update version, type, and content IDs from its content meta."""
    run_hactool(hactool, nca_keys, ["-t", "nca", "--section0dir=" + str(directory), meta_nca])
    files = list(directory.glob("*.cnmt"))
    if len(files) != 1:
        raise GameError("The update has no single content meta file.")
    data = files[0].read_bytes()
    title, version, kind = struct.unpack_from("<QIB", data, 0)
    extended, count = struct.unpack_from("<HH", data, 0xE)
    contents = []
    for index in range(count):
        record = data[0x20 + extended + 0x38 * index:0x20 + extended + 0x38 * (index + 1)]
        contents.append({"id": record[0x20:0x30].hex(), "type": record[0x36]})
    application = struct.unpack_from("<Q", data, 0x20)[0] if kind == 0x81 else None
    return {"title_id": "%016x" % title, "version": version, "type": kind,
            "application_id": "%016x" % application if application is not None else None, "contents": contents}


def read_display_version(hactool, nca_keys, control_nca, directory):
    run_hactool(hactool, nca_keys, ["-t", "nca", "--romfsdir=" + str(directory), control_nca])
    nacp = directory / "control.nacp"
    if not nacp.is_file() or nacp.stat().st_size < 0x3070:
        return None
    return nacp.read_bytes()[0x3060:0x3070].split(b"\0", 1)[0].decode("ascii", "replace")


def extract_update(base_package, update_package, keys, hactool, output, title_id, game_id):
    """Extract base + update content. The update RomFS must match its own IVFC hash tree."""
    from patch_storage import Nca, PatchSection, read_exefs, read_keys, verify_ivfc
    from romfs_storage import expand_romfs
    if not keys.is_file():
        raise GameError("The prod.keys file was not found.")
    if not hactool.is_file() or not os.access(hactool, os.X_OK):
        raise GameError("Build hactool first: tools/bootstrap.sh")
    if output.exists():
        raise GameError("The output path already exists. Select a new --out path.")
    patch_title = "%016x" % (int(title_id, 16) | 0x800)
    base_entries, base_ncas = package_ncas(base_package)
    update_entries, update_ncas = package_ncas(update_package)
    titles = package_tickets(base_package, base_entries)
    for rights, value in package_tickets(update_package, update_entries).items():
        if titles.setdefault(rights, value) != value:
            raise GameError("The packages contain conflicting tickets.")
    output.parent.mkdir(parents=True, exist_ok=True)
    required = base_package.stat().st_size * 4 + update_package.stat().st_size * 4 + 20 * 1024**3
    if shutil.disk_usage(output.parent).free < required:
        raise GameError("Update extraction requires about four times both package sizes plus 20 GiB of free space.")
    with tempfile.TemporaryDirectory(prefix="." + game_id + "-update-", dir=output.parent) as temporary:
        temp = Path(temporary)
        nca_keys = temp / "nca.keys"
        header_key = write_nca_keyset(keys, nca_keys)
        key_set = read_keys(nca_keys)
        base_dir, update_dir = temp / "base-nca", temp / "update-nca"
        base_dir.mkdir()
        update_dir.mkdir()
        print("Copy and hash the update NCA files...", flush=True)
        update_hashes, update_conversions = copy_ncas(update_package, update_ncas, update_dir, header_key)
        update_content = program_ncas(update_dir, update_ncas, header_key, {title_id, patch_title})
        if len(update_content.get(0, [])) != 1 or len(update_content.get(1, [])) != 1:
            raise GameError("Expected one Program and one content meta NCA in the update.")
        meta = read_meta(hactool, nca_keys, update_content[1][0][0], temp / "meta")
        if meta["type"] != 0x81 or meta["title_id"] != patch_title or meta["application_id"] != title_id:
            raise GameError("The update package is not a patch for this game.")
        patch_path, _, patch_rights = update_content[0][0]
        if patch_path.name[:32] not in {item["id"] for item in meta["contents"]}:
            raise GameError("The update content meta does not list its Program NCA.")
        display_version = None
        if update_content.get(2):
            path, _, rights = update_content[2][0]
            display_version = read_display_version(hactool, nca_keys, path, temp / "control")
        print("Copy and hash the base Program NCA...", flush=True)
        base_hashes, base_conversions, base_program = {}, [], None
        for entry in base_ncas:
            single_dir = temp / ("probe-" + entry["name"])
            single_dir.mkdir()
            # Read only the header first. Copy the full base Program after it is identified.
            with base_package.open("rb") as stream:
                stream.seek(entry["offset"])
                (single_dir / entry["name"]).write_bytes(read_exact(stream, min(0xC00, entry["size"])))
            content = program_ncas(single_dir, [entry], header_key, {title_id})
            shutil.rmtree(single_dir)
            if 0 in content:
                if base_program is not None:
                    raise GameError("Expected exactly one base Program NCA.")
                base_program = entry
        if base_program is None:
            raise GameError("The base package has no Program NCA.")
        base_hashes, base_conversions = copy_ncas(base_package, [base_program], base_dir, header_key)
        base_path = base_dir / base_program["name"]
        base_rights = program_ncas(base_dir, [base_program], header_key, {title_id})[0][0][2]
        if base_rights not in titles or patch_rights not in titles:
            raise GameError("The packages have no matching common tickets for their Programs.")
        print("Decrypt the base RomFS section...", flush=True)
        base_section = temp / "base.romfs.section"
        run_hactool(hactool, nca_keys, ["-r", "-t", "nca", "--romfs=" + str(base_section), base_path], titles[base_rights])
        patch = Nca(patch_path, key_set, titles[patch_rights])
        sections = patch.sections()
        romfs_sections = [index for index, fs_type, hash_type, _ in sections if fs_type == 0 and hash_type == 3]
        exefs_sections = [index for index, fs_type, _, _ in sections if fs_type == 1]
        if len(romfs_sections) != 1 or len(exefs_sections) != 1:
            raise GameError("Expected one ExeFS and one RomFS section in the update Program.")
        stage = temp / "game"
        stage.mkdir()
        print("Decrypt and check the update ExeFS...", flush=True)
        exefs_files = read_exefs(patch, exefs_sections[0], stage / "ExeFS")
        print("Merge the update RomFS section with the base section...", flush=True)
        romfs_patch = PatchSection(patch, romfs_sections[0])
        virtual_section = temp / "update.romfs.section"
        sources = romfs_patch.write_virtual(base_section, virtual_section)
        base_section.unlink()
        print("Check the merged section against the update IVFC hash tree...", flush=True)
        blocks = verify_ivfc(virtual_section, romfs_patch.fs)
        print("Expand the RomFS storage layer...", flush=True)
        romfs_image = temp / "romfs.bin"
        compression = expand_romfs(virtual_section, romfs_patch.fs, romfs_image)
        virtual_section.unlink()
        print("Extract the expanded RomFS files...", flush=True)
        run_hactool(hactool, nca_keys, ["-t", "romfs", "--outdir=" + str(stage / "RomFS"), romfs_image])
        romfs_image.unlink()
        main = stage / "ExeFS/main"
        if not main.is_file() or main.read_bytes()[:4] != b"NSO0":
            raise GameError("The update ExeFS has no main NSO module.")
        if not any(p.is_file() for p in (stage / "RomFS").rglob("*")):
            raise GameError("Extraction did not produce game assets.")
        report = {
            "game_id": game_id, "title_id": title_id, "update_title_id": patch_title,
            "base_package": base_package.name, "update_package": update_package.name,
            "update_version": meta["version"], "display_version": display_version,
            "base_program_nca": base_path.name, "update_program_nca": patch_path.name,
            "content_sha256": {**base_hashes, **update_hashes},
            "gamecard_conversion": sorted(base_conversions + update_conversions),
            "exefs_files": exefs_files,
            "patch_storage": {"indirect_entries": len(romfs_patch.indirect), "aes_ctr_ex_entries": len(romfs_patch.aes_entries),
                              "ranges": sources, "virtual_bytes": romfs_patch.virtual_size, "ivfc_blocks_checked": blocks},
            "romfs_storage": compression,
            "status": "extracted; the merged RomFS matches the update IVFC hash tree",
        }
        (stage / "extraction.json").write_text(json.dumps(report, indent=2) + "\n")
        stage.rename(output)
    print("Extracted update %d (%s) to %s" % (meta["version"], display_version or "unknown display version", output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("inspect", "extract"):
        child = sub.add_parser(command)
        child.add_argument("package", type=Path)
        child.add_argument("--game", default="silksong", help="Registered game ID.")
        if command == "inspect":
            child.add_argument("--json", type=Path)
        else:
            child.add_argument("--keys", type=Path, required=True)
            child.add_argument("--hactool", type=Path, default=DEPENDENCIES / "hactool/hactool")
            child.add_argument("--out", type=Path, help="Default: work/<game>/base.")
    child = sub.add_parser("extract-update", help="Extract a base NSP with one update NSP applied.")
    child.add_argument("package", type=Path, help="Base game NSP.")
    child.add_argument("update", type=Path, help="Update NSP. Updates are cumulative; use the newest one.")
    child.add_argument("--game", default="silksong", help="Registered game ID.")
    child.add_argument("--keys", type=Path, required=True)
    child.add_argument("--hactool", type=Path, default=DEPENDENCIES / "hactool/hactool")
    child.add_argument("--out", type=Path, required=True, help="New output directory, for example work/<game>/v655360.")
    args = parser.parse_args()
    try:
        package = args.package.expanduser().resolve()
        if args.command == "inspect":
            report = inspect(package)
            data = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
            if args.json:
                args.json.parent.mkdir(parents=True, exist_ok=True)
                args.json.write_text(data)
            print(data, end="")
        elif args.command == "extract-update":
            config = load_game(args.game)
            extract_update(package, args.update.expanduser().resolve(), args.keys.expanduser().resolve(),
                           args.hactool.expanduser().resolve(), args.out.resolve(), config.require_title_id(), config.id)
        else:
            config = load_game(args.game)
            extract(package, args.keys.expanduser().resolve(), args.hactool.expanduser().resolve(),
                    (args.out or config.base_dir).resolve(), config.require_title_id(), config.id)
    except (GameError, OSError) as exc:
        print("Error: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
