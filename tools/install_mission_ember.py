#!/usr/bin/env python3
"""Install the Ember DLL and all mission Lua files into a closed Linux/Proton game.
Never launches/stops the game, and never modifies settings, saves or generated SDK.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil


def closed():
    if not Path('/proc').is_dir():
        raise RuntimeError('This process guard requires Linux /proc; use a verified manual install elsewhere.')
    for process in Path('/proc').iterdir():
        if not process.name.isdecimal():
            continue
        try:
            name = (process / 'comm').read_text().strip().lower()
        except FileNotFoundError:
            continue
        if 'destiny' in name:
            raise RuntimeError('Game running; installation cancelled. The user must close it manually.')


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True, help='Game bin/x64 directory')
    parser.add_argument('--dll', type=Path, help='Override build/x64/Release/steam_api64.dll')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = args.game.expanduser().resolve()
    if not game.is_dir() or not (game / 'steam_api64.dll').is_file():
        raise RuntimeError('Target must be the existing game bin/x64 directory containing steam_api64.dll')
    dll = args.dll.expanduser().resolve() if args.dll else root / 'build/x64/Release/steam_api64.dll'
    pairs = [(dll, game / 'steam_api64.dll'),
             (root / 'scripts/mission_ember.lua', game / 'Sunrise/scripts/mission_ember.lua')]
    pairs += [(p, game / 'Sunrise/scripts/mission_ember' / p.name)
              for p in sorted((root / 'scripts/mission_ember').glob('*.lua'))]
    if len(pairs) != 18:
        raise RuntimeError('Expected one DLL and 17 mission Lua files; review the deployment list.')
    for source, _ in pairs:
        if not source.is_file():
            raise RuntimeError(f'Missing source: {source}')
    closed()
    manifest = {'created': datetime.datetime.now().astimezone().isoformat(), 'files': [
        {'path': str(dst.relative_to(game)), 'source': str(src), 'sha256': sha(src)}
        for src, dst in pairs]}
    if args.dry_run:
        print(json.dumps(manifest, indent=2))
        return
    backup = root / 'build' / ('ember-install-backup-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    backup.mkdir(parents=True)
    for _, dst in pairs:
        if dst.exists():
            keep = backup / dst.relative_to(game)
            keep.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dst, keep)
    closed()
    for (src, dst), item in zip(pairs, manifest['files']):
        closed()
        dst.parent.mkdir(parents=True, exist_ok=True)
        temporary = dst.with_name(dst.name + '.ember-install-tmp')
        shutil.copy2(src, temporary)
        if sha(temporary) != item['sha256']:
            temporary.unlink()
            raise RuntimeError(f'Source changed during installation: {src}')
        os.replace(temporary, dst)
        if sha(dst) != item['sha256']:
            raise RuntimeError(f'Installed checksum mismatch: {dst}')
    manifest['backup'] = str(backup)
    (root / 'build/ember-installation.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Installed and verified {len(pairs)} files. Backup: {backup}')
    print('Settings, saves and SDK untouched; game not launched.')


if __name__ == '__main__':
    main()
