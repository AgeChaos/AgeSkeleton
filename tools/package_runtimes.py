"""Build source runtime packages with the shared native core and license notices."""
import argparse
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    # AgeChaos uses its built-in ECS SDK and needs no separate runtime package.
    for target in ('Unity','Cocos','Unreal','Godot'):
        archive=args.output/f'AgeSkeleton-{target}-0.3.0-source.zip'
        if archive.exists(): raise SystemExit(f'Refusing to overwrite {archive}')
        with tempfile.TemporaryDirectory() as work:
            staged=Path(work)/target
            shutil.copytree(ROOT/'Runtimes'/target,staged,ignore=shutil.ignore_patterns('*.meta','bin','build','.godot'))
            if target=='Unreal':
                shared=staged/'AgeSkeleton/Source/ThirdParty/AgeSkeleton'
                shutil.copytree(ROOT/'Runtimes/Native/include',shared/'include')
                shutil.copy(ROOT/'Runtimes/Native/LICENSE',shared/'LICENSE')
                shutil.copytree(ROOT/'Runtimes/Native/third_party',shared/'third_party')
            if target=='Godot':
                shutil.copytree(ROOT/'Runtimes/Native',Path(work)/'Native')
            shutil.copy(ROOT/'Runtimes/LICENSE',staged/'LICENSE')
            shutil.copy(ROOT/'README.md',staged/'README.md')
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as out:
                for f in sorted(Path(work).rglob('*')):
                    if f.is_file():out.write(f,f.relative_to(work).as_posix())
        print(archive)

if __name__=='__main__':main()
