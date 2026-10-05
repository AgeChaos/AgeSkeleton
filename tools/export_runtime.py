"""Export portable mesh clips through a running AgeSkeleton session."""
import argparse
import json
from pathlib import Path
from skeleton_ai import SkeletonClient

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('session',type=Path)
parser.add_argument('output',type=Path,help='New directory; never overwritten')
parser.add_argument('--entity',type=int,default=0)
parser.add_argument('--fps',type=int,default=30)
args=parser.parse_args()
result=SkeletonClient(args.session,timeout=180).call('export_runtime',entity=args.entity,directory=args.output.resolve().as_posix(),fps=args.fps)
print(json.dumps(result,ensure_ascii=False,indent=2))
