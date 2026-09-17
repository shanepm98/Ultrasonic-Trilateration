import sys
from pathlib import Path

# Flat (non-package) module layout: make host/ importable regardless of the directory pytest
# is invoked from.
sys.path.insert(0, str(Path(__file__).parent))
