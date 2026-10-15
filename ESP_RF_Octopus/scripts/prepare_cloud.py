from pathlib import Path
import runpy
Import('env')
runpy.run_path(str(Path(env['PROJECT_DIR']).parent / 'tools/firmware_package.py'))['prepare']()
