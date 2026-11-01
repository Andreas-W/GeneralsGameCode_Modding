# Starts or stops a second WorldBuilder from the mod folder on its own port, so that scripts can build and scan maps
# while the one used by Claude and by hand stays free. Usage: start_wb.py [stop]
import os, socket, subprocess, sys, time
from town_paths import GAME_DIR, WB_PORT  # enters the work folder

PID = "wb_second.pid"
if len(sys.argv) > 1 and sys.argv[1] == "stop":
    if os.path.exists(PID):
        subprocess.run(["taskkill", "/F", "/PID", open(PID).read().strip()], capture_output=True)
        os.remove(PID)
        print("stopped")
    sys.exit(0)
exe = os.path.join(GAME_DIR, os.environ.get("TOWN_WB_EXE", "WorldBuilderZH.exe"))
p = subprocess.Popen([exe, f"-mcpport:{WB_PORT}", "-ignoreAsserts"], cwd=GAME_DIR)
open(PID, "w").write(str(p.pid))
for _ in range(60):
    try:
        socket.create_connection(("127.0.0.1", WB_PORT), timeout=1).close()
        print("running on port", WB_PORT, "pid", p.pid)
        sys.exit(0)
    except OSError:
        time.sleep(1)
sys.exit("WorldBuilder did not open its port")
