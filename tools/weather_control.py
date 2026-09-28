"""Local USB control for Butthead's weather module. Commands never go over LoRa."""
import argparse
import time
import serial

from meshtastic import portnums_pb2
from meshtastic.serial_interface import SerialInterface
from meshtastic.stream_interface import StreamInterface
from pubsub import pub


class WeatherSerialInterface(SerialInterface):
    def connect(self):
        # Keep ESP32-S3 native USB reset lines inactive before opening the port.
        self.stream = serial.Serial(port=None, baudrate=115200, exclusive=True, timeout=0.5, write_timeout=0)
        self.stream.dtr = False
        self.stream.rts = False
        self.stream.port = self.devPath
        self.stream.open()
        self.stream.flush()
        time.sleep(0.1)
        StreamInterface.connect(self)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["status", "preview", "test", "on", "off"])
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--node", required=True, help="Expected node ID, for example !12345678")
    parser.add_argument("--wait", type=int, default=45)
    args = parser.parse_args()

    def log(line, interface=None):
        if "Weather" in str(line):
            print(line, flush=True)

    pub.subscribe(log, "meshtastic.log.line")
    with WeatherSerialInterface(devPath=args.port, timeout=45) as interface:
        expected = int(args.node.removeprefix("!"), 16)
        if interface.myInfo.my_node_num != expected:
            raise SystemExit("Unexpected node: no command sent.")
        if interface.myInfo.pio_env != "heltec-v4-weather":
            raise SystemExit("Weather firmware is not installed: no command sent.")
        # Also work immediately after a cold start; the module starts at 30 seconds.
        time.sleep(35)
        interface.sendData(
            ("weather:" + args.command).encode(),
            destinationId=expected,
            portNum=portnums_pb2.PRIVATE_APP,
            wantAck=False,
            wantResponse=False,
        )
        print(f"Sent local {args.command} command to {args.node} on {args.port}.", flush=True)
        time.sleep(args.wait)


if __name__ == "__main__":
    main()
