"""Copy one sketch case into a directory arduino-cli can build.

Three callers stage sketches - compile_all.py, profile_build.py and
manual/smoke/smoke.py - and each used to copy just the .ino. That stopped being
enough when the sketches gained testcmd.h, and the failure it produced was one
error message per sketch per board rather than one, so the rule lives here now.

Two files are deliberately left behind:

  sketch.yaml   with it present arduino-cli resolves the platform through the
                profile's platform_index_url and ignores --fqbn, so a build
                meant for the symlinked working tree would silently be built
                against whatever the published index holds. profile_build.py,
                which *wants* profile resolution, passes keep_yaml and then
                rewrites the URL to its own loopback index.
  test_*.py     the host side; nothing on the target needs it.

No pytest, no third-party imports: smoke.py runs under a bare `uv run --script`
whose only dependency is pyserial.
"""
import pathlib
import shutil


def stage_sketch(src: pathlib.Path, dest: pathlib.Path,
                 keep_yaml: bool = False) -> pathlib.Path:
    """Copy the buildable part of a sketch case, and return dest.

    dest has to be named after the sketch: arduino-cli requires the main .ino
    to match the directory it sits in.
    """
    dest.mkdir(parents=True, exist_ok=True)
    for item in sorted(src.iterdir()):
        if item.is_dir():
            continue                       # __pycache__, and nothing else yet
        if item.name.startswith("test_") and item.suffix == ".py":
            continue
        if item.name == "sketch.yaml" and not keep_yaml:
            continue
        shutil.copy(item, dest / item.name)
    return dest
