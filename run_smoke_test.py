import pathlib
import signal
import subprocess
import time

root = pathlib.Path(".")
producer_log = open(root / "producer.log", "w")
consumer_log = open(root / "consumer.log", "w")

producer = subprocess.Popen(
    ["build/producer", "--size", "64", "--reset", "--log-interval-ms", "100"],
    stdout=producer_log,
    stderr=subprocess.STDOUT,
)

# The producer creates and initializes SHM first; start consumer immediately after that short setup window.
time.sleep(0.05)

consumer = subprocess.Popen(
    ["build/consumer", "--log-interval-ms", "100"],
    stdout=consumer_log,
    stderr=subprocess.STDOUT,
)

time.sleep(1)

producer.send_signal(signal.SIGINT)
consumer.send_signal(signal.SIGINT)

consumer.wait(timeout=5)
producer.wait(timeout=5)

producer_log.close()
consumer_log.close()

producer_lines = (root / "producer.log").read_text().splitlines()
consumer_lines = (root / "consumer.log").read_text().splitlines()

print("--- producer.log ---")
print("\n".join(producer_lines[-12:]))
print("--- consumer.log ---")
print("\n".join(consumer_lines[-12:]))
