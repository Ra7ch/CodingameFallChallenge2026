import subprocess, sys

# Compile bot with debug flags or run bot directly with input
# Let's inspect what happens by running the referee with bot
# Wait, can we compile the referee with mvn?
res = subprocess.run(["mvn", "-v"], capture_output=True, text=True)
print("Maven available:", res.returncode == 0)
