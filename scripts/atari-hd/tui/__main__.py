"""Entry point for the atari-hd TUI.

Runnable two ways:

    python scripts/atari-hd/tui            # directory invocation
    cd scripts/atari-hd && python -m tui   # package invocation

Directory invocation runs this file as a top-level script with no
package context, so a `from .app import main` would fail. We inject
the package's parent directory onto sys.path and use an absolute
import; that works for both invocation styles.
"""

import os
import sys


_PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PARENT not in sys.path:
    sys.path.insert(0, _PARENT)

from tui.app import main  # noqa: E402


if __name__ == "__main__":
    sys.exit(main())
