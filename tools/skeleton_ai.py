"""AgeSkeleton local editor client. No external Python dependencies."""
import argparse
import json
from pathlib import Path
import time
import uuid

class SkeletonClient:
    def __init__(self, session, timeout=30):
        self.directory = Path(session).resolve().parent
        self.timeout = timeout
        self.revision = None

    def call(self, operation, **parameters):
        if self.revision is None and operation not in ("status", "capabilities", "validate"):
            self.call("status")
        request = dict(parameters, operation=operation)
        if self.revision is not None:
            request["revision"] = self.revision
        name = uuid.uuid4().hex
        target = self.directory / (name + ".request.json")
        pending = target.with_suffix(".tmp")
        pending.write_text(json.dumps(request, ensure_ascii=False, allow_nan=False), encoding="utf-8")
        pending.replace(target)
        response = self.directory / (name + ".response.json")
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            if response.exists():
                try:
                    result = json.loads(response.read_text(encoding="utf-8"))
                except PermissionError:
                    # Windows may briefly retain a sharing lock after the atomic rename.
                    time.sleep(.05)
                    continue
                try:
                    response.unlink()
                except PermissionError:
                    pass
                self.revision = result.get("revision", self.revision)
                if not result.get("ok"):
                    raise RuntimeError(json.dumps(result, ensure_ascii=False))
                return result
            time.sleep(.05)
        raise TimeoutError("Editor response timed out; operation may have executed. Inspect status; do not blindly retry.")

    def run(self, steps):
        results = []
        aliases = {}
        def resolve(value):
            if isinstance(value, str) and value.startswith("$"):
                return aliases[value[1:]]
            if isinstance(value, dict):
                return {k: resolve(v) for k, v in value.items()}
            if isinstance(value, list):
                return [resolve(v) for v in value]
            return value
        for index, step in enumerate(steps):
            try:
                params = resolve({k: v for k, v in step.items() if k != "as"})
                result = self.call(**params)
                results.append(result)
                if "as" in step:
                    aliases[step["as"]] = result["entity"]
            except Exception as exc:
                return {"ok": False, "failed_step": index, "error": str(exc), "completed": results,
                        "note": "Completed steps remain undoable; this sequence is not an atomic transaction."}
        return {"ok": True, "completed": results}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("plan", type=Path)
    args = parser.parse_args()
    result = SkeletonClient(args.session).run(json.loads(args.plan.read_text(encoding="utf-8")))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(0 if result["ok"] else 1)
