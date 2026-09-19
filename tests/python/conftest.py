import sys
from pathlib import Path

# Ensure the build/dev-release/python path is discoverable
build_python_path = Path(__file__).resolve().parent.parent.parent / "build" / "dev-release" / "python"
if str(build_python_path) not in sys.path:
    sys.path.insert(0, str(build_python_path))
