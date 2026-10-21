#!/usr/bin/env python3
"""Rewrite and verify the supported Switch modules. Do not create an IPA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from analyze_game import load_nso
from game_formats import GameError
from workspace import ROOT, DEPENDENCIES, UPSTREAM, load_game


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def run(arguments, environment, log):
    with log.open("w") as stream:
        result = subprocess.run(arguments, env=environment, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise GameError("The command failed. See " + str(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="silksong", help="Registered game ID. A legacy input path selects silksong.")
    parser.add_argument("--input", type=Path, help="Extracted input. Default: the configured content directory.")
    parser.add_argument("--out", type=Path, help="Default: work/<game>/patched.")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1: parser.error("--jobs must be positive")
    upstream = UPSTREAM
    try:
        legacy = "/" in args.game or "\\" in args.game or Path(args.game).is_absolute()
        if legacy and args.input: raise GameError("Use --game <ID> with --input <path>, or the legacy --game <path> form.")
        config = load_game("silksong" if legacy else args.game)
        # Both names select the generic native ruleset. Module hashes in the profile pin each game.
        if config.require_adapted("rewrite") not in ("silksong-switch", "switch-native"):
            raise GameError("No rewrite implementation is registered for " + config.id + ".")
        game = (Path(args.game) if legacy else args.input or config.content_dir).resolve()
        output = (args.out or config.patched_dir).resolve()
        profile = config.profile
        if not profile.get("modules") or not profile.get("upstream_revision"):
            raise GameError("The rewrite profile needs module hashes and an upstream revision.")
        revision = subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
        if revision != profile["upstream_revision"]: raise GameError("The upstream revision does not match the profile.")
        for name, expected in profile["modules"].items():
            actual = load_nso(game / "ExeFS" / name)
            if any(actual[key] != expected[key] for key in ("sha256", "build_id")):
                raise GameError("Unsupported game module: " + name)
        if output.exists() and not args.verify_only:
            raise GameError("The output already exists. Use --verify-only or select a new --out path.")
        if args.verify_only and not output.is_dir(): raise GameError("The output directory does not exist.")
        environment = os.environ.copy()
        objdump = subprocess.check_output(["xcrun", "--find", "llvm-objdump"], text=True).strip()
        environment.update(DM_ROOT=str(config.build_dir.parent.parent.parent), DM_VCACHE=str(config.build_dir / "verify-cache"), LLVM_BIN=str(Path(objdump).parent),
                           DM_RULESET="native", DM_PRIVATE_GATES="0", DM_INDIRECT_TRAPS="1")
        log_dir = config.logs_dir / "rewrite"
        log_dir.mkdir(parents=True, exist_ok=True)
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".cpu-rewrite-", dir=output.parent) as temp:
            stage = output if args.verify_only else Path(temp) / "modules"
            stage.mkdir(exist_ok=True)
            report = {"game_id": config.id, "status": "CPU checks only; no game boot test", "modules": {},
                      "upstream_revision": revision, "signal_bridge_tests": "See " + str(config.reports_dir / "iphone-cpu-probe.json"),
                      "trap_equivalence": "Signal effects are modeled in Unicorn. The C handler has a separate native test."}
            for name in profile["runtime_modules"]:
                source = game / "ExeFS" / name
                if not args.verify_only:
                    print("Rewrite " + name, flush=True)
                    run([sys.executable, str(upstream / "dm/tools/patch_nso.py"), str(source), str(stage), name],
                        environment, log_dir / (name + ".rewrite.log"))
                print("Verify " + name, flush=True)
                run([sys.executable, str(upstream / "dm/verify/structck.py"), str(stage), name, str(source)],
                    environment, log_dir / (name + ".structure.log"))
                run([sys.executable, str(upstream / "dm/verify/equiv.py"), str(stage), name, str(source),
                     "--jobs", str(args.jobs), "--mutate", "100", "--tsv", str(log_dir / (name + ".equivalence.tsv"))],
                    environment, log_dir / (name + ".equivalence.log"))
                files = {p.name: digest(p) for p in sorted(stage.glob(name + ".*")) if p.is_file() and p.suffix != ".log"}
                report["modules"][name] = {"source_sha256": profile["modules"][name]["sha256"],
                                           "output_sha256": files, "structure": "passed", "equivalence": "passed"}
            for relative in ("dm/tools/patch_nso.py", "dm/verify/structck.py", "dm/verify/equiv.py"):
                report.setdefault("current_tool_sha256", {})[relative] = digest(upstream / relative)
            (stage / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
            if not args.verify_only: stage.rename(output)
        print("CPU checks passed: " + str(output))
        return 0
    except (GameError, OSError, subprocess.CalledProcessError) as error:
        print("Error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__": sys.exit(main())
