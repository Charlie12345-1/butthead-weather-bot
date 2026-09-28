"""Build and run the production weather parser/scheduler without radio code.

Requires a completed PlatformIO dependency install and `pip install ziglang`.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
output = root / ".pio" / "weather-native"
output.mkdir(parents=True, exist_ok=True)
exe = output / ("weather-core-test.exe" if sys.platform == "win32" else "weather-core-test")
command = [
    sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O1",
    "-Wno-nullability-completeness", "-DBUTTHEAD_WEATHER_BOT=1",
    "-I", str(root / "tools/weather-test-stubs"),
    "-I", str(root / "src/modules"),
    "-I", str(root / ".pio/libdeps/heltec-v4-weather/ArduinoJson/src"),
    str(root / "src/modules/WeatherBotCore.cpp"),
    str(root / "tools/test_weather_core.cpp"), "-o", str(exe),
]
subprocess.run(command, check=True)
fixtures = root / "tools/weather-fixtures"
subprocess.run([
    str(exe), str(fixtures / "forecast.json"), str(fixtures / "london-clear.json"),
    str(fixtures / "active-other-district.json"),
], check=True)
