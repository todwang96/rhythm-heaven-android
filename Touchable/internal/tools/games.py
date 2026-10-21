#!/usr/bin/env python3
"""List game adapters, create a scaffold, or check registered games."""

import argparse
import json
from pathlib import Path
import sys

from workspace import (ROOT, WorkspaceError, check_game, load_game, registered_games,
                       registry_identity_errors, validate_game_id, validate_registry)


def scaffold(root, game, display_name=None, product_name=None, bundle_id=None, title_id=None):
    """Create metadata only. Do not copy another game's runtime or resources."""
    game = validate_game_id(game)
    directory = Path(root).resolve() / "games" / game
    if directory.exists():
        raise WorkspaceError("The game directory already exists: " + str(directory))
    product = product_name or "".join(part.capitalize() for part in game.split("-"))
    manifest = {
        "schema_version": 1, "id": game, "display_name": display_name or product,
        "product_name": product, "bundle_id": bundle_id or "com.example.nitenphone." + game,
        "build_version": 1, "title_id": title_id, "profile": "profile.json",
        "status": "not-adapted", "save_namespace": game,
        "module_names": [], "runtime_sources": [], "common_runtime_sources": [],
        "ios_sources": [], "compile_defines": [], "adapters": {},
        "ios": {"deployment_target": "26.0"},
        "host": {"deployment_target": "26.0"},
    }
    profile = {"title_id": title_id, "runtime_modules": [], "modules": {},
               "status": "Not adapted. Add module hashes and an adapter before building."}
    # Check metadata in a temporary sibling first. Keep the requested directory absent on failure.
    import tempfile
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".game-scaffold-", dir=directory.parent) as temporary:
        staged_root = Path(temporary)
        stage = staged_root / "games" / game
        stage.mkdir(parents=True)
        (stage / "game.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        (stage / "profile.json").write_text(json.dumps(profile, indent=2) + "\n")
        candidate = load_game(game, staged_root)
        validate_registry(root, candidate)
        for name in ("runtime", "ios", "tests"):
            (stage / name).mkdir()
            (stage / name / ".gitkeep").touch()
        (stage / "README.md").write_text(
            "# " + manifest["display_name"] + "\n\n"
            "Status: not adapted. This scaffold cannot run a game.\n\n"
            "1. Set the title ID in game.json and profile.json.\n"
            "2. Extract your game to work/" + game + "/base.\n"
            "3. Analyze its modules and services.\n"
            "4. Add and test this game's runtime and iOS adapter.\n"
            "5. Set the module hashes, source lists, and adapter fields.\n"
            "6. Set status to adapted only after the adapter is tested.\n\n"
            "See ../../docs/add-game.md. Keep keys and game data out of games/.\n"
        )
        stage.rename(directory)
    return directory


def check_registry(root=ROOT, game=None):
    ids = [game] if game else registered_games(root)
    errors, rows = registry_identity_errors(root), []
    for game_id in ids:
        try:
            config = load_game(game_id, root)
            rows.append({"id": game_id, "status": config.data["status"], "bundle_id": config.bundle_id})
            errors.extend(check_game(config))
        except WorkspaceError as error:
            if str(error) not in errors: errors.append(str(error))
    if not ids:
        errors.append("No games are registered.")
    return rows, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    listing = sub.add_parser("list")
    listing.add_argument("--json", action="store_true")
    new = sub.add_parser("new")
    new.add_argument("id")
    new.add_argument("--name")
    new.add_argument("--product-name")
    new.add_argument("--bundle-id")
    new.add_argument("--title-id")
    check = sub.add_parser("check")
    check.add_argument("--game")
    args = parser.parse_args()
    try:
        if args.command == "new":
            print("Created: " + str(scaffold(ROOT, args.id, args.name, args.product_name, args.bundle_id, args.title_id)))
            print("Status: not-adapted. Extraction and analysis are available. Builds require a tested adapter.")
        elif args.command == "list":
            rows = [{"id": game, "name": (config := load_game(game)).display_name,
                     "status": config.data["status"], "bundle_id": config.bundle_id}
                    for game in registered_games()]
            if args.json: print(json.dumps(rows, ensure_ascii=False, indent=2))
            else:
                for row in rows: print(row["id"] + "\t" + row["status"] + "\t" + row["name"])
        else:
            rows, errors = check_registry(ROOT, args.game)
            for row in rows: print(row["id"] + ": " + row["status"])
            for error in errors: print("Error: " + error, file=sys.stderr)
            return 1 if errors else 0
    except (WorkspaceError, OSError) as error:
        print("Error: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__": sys.exit(main())
