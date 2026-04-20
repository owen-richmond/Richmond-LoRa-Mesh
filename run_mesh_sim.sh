#!/bin/bash
# Launcher script for mesh_sim.py that uses the virtual environment

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_PYTHON="$SCRIPT_DIR/.venv/bin/python"

if [ ! -f "$VENV_PYTHON" ]; then
    echo "Error: Virtual environment Python not found at $VENV_PYTHON"
    echo "Please run: python3 -m venv .venv && source .venv/bin/activate && pip install PyQt5 numpy"
    exit 1
fi

# Run the simulation with the virtual environment Python
exec "$VENV_PYTHON" "$SCRIPT_DIR/simulation/mesh_sim.py" "$@"