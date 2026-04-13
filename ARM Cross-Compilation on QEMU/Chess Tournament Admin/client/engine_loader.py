"""
Engine loader for chess-arm-tournament.
Clones a UCI chess engine from GitHub and wraps it with python-chess.
"""
import os
import subprocess
import chess
import chess.engine


def clone_engine(github_url: str, target_dir: str = "/tmp/chess_engine") -> str | None:
    """Clone a GitHub repo containing a UCI chess engine binary."""
    if os.path.exists(target_dir):
        print(f"[Engine] Already cloned at {target_dir}")
        return target_dir
    print(f"[Engine] Cloning {github_url} ...")
    result = subprocess.run(
        ["git", "clone", "--depth=1", github_url, target_dir],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"[Engine] Clone failed:\n{result.stderr}")
        return None
    print(f"[Engine] Cloned to {target_dir}")
    return target_dir


def build_engine(engine_dir: str) -> bool:
    """Attempt to build via Makefile or CMake."""
    makefile = os.path.join(engine_dir, "Makefile")
    cmake    = os.path.join(engine_dir, "CMakeLists.txt")
    if os.path.exists(makefile):
        print("[Engine] Building with make...")
        r = subprocess.run(["make", "-C", engine_dir, "-j4"],
                           capture_output=True, text=True)
        if r.returncode == 0:
            print("[Engine] Build successful ✓")
            return True
        print(f"[Engine] make failed:\n{r.stderr}")
    elif os.path.exists(cmake):
        build_dir = os.path.join(engine_dir, "build")
        os.makedirs(build_dir, exist_ok=True)
        r1 = subprocess.run(["cmake", ".."], cwd=build_dir, capture_output=True)
        r2 = subprocess.run(["cmake", "--build", "."], cwd=build_dir, capture_output=True)
        if r1.returncode == 0 and r2.returncode == 0:
            print("[Engine] CMake build successful ✓")
            return True
    print("[Engine] No build system found, looking for pre-built binary")
    return False


def find_engine_binary(engine_dir: str) -> str | None:
    """Walk repo looking for an executable UCI engine binary."""
    skip_ext = {".py", ".md", ".txt", ".sh", ".yml", ".json", ".cfg",
                ".h", ".hpp", ".c", ".cpp", ".rs", ".toml"}
    for root, dirs, files in os.walk(engine_dir):
        # Skip hidden dirs and common non-binary dirs
        dirs[:] = [d for d in dirs if not d.startswith(".") and d not in
                   {"__pycache__", "node_modules", ".git", "docs", "test", "tests"}]
        for fname in files:
            path = os.path.join(root, fname)
            _, ext = os.path.splitext(fname)
            if ext in skip_ext:
                continue
            if os.access(path, os.X_OK) and os.path.isfile(path):
                print(f"[Engine] Found binary candidate: {path}")
                return path
    return None


def load_engine_from_github(github_url: str, elo: int = 1500) -> "EnginePlayer | None":
    """Full pipeline: clone → build → find binary → wrap."""
    target = f"/tmp/chess_engine_{abs(hash(github_url)) % 100000}"
    engine_dir = clone_engine(github_url, target)
    if engine_dir is None:
        return None
    build_engine(engine_dir)
    binary = find_engine_binary(engine_dir)
    if binary is None:
        print("[Engine] No usable binary found in repo")
        return None
    player = EnginePlayer(binary, elo)
    try:
        player.start()
        return player
    except Exception as e:
        print(f"[Engine] Failed to start: {e}")
        return None


class EnginePlayer:
    """Wraps a UCI engine binary using python-chess."""

    def __init__(self, binary_path: str, elo: int = 1500):
        self.binary_path = binary_path
        self.elo         = elo
        self._engine     = None

    def start(self):
        self._engine = chess.engine.SimpleEngine.popen_uci(self.binary_path)
        try:
            self._engine.configure({
                "UCI_LimitStrength": True,
                "UCI_Elo": self.elo,
            })
        except Exception:
            pass  # Not all engines support strength limiting
        print(f"[Engine] Started ✓  path={self.binary_path}  elo={self.elo}")

    def get_move(self, board: chess.Board, time_limit: float = 1.0) -> str:
        """Return best move in UCI notation."""
        result = self._engine.play(board, chess.engine.Limit(time=time_limit))
        return result.move.uci()

    def stop(self):
        if self._engine:
            self._engine.quit()
            self._engine = None
            print("[Engine] Stopped")
