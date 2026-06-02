import subprocess
import time
import signal
import pathlib

root = pathlib.Path('.')
producer_log = open(root / 'producer.log', 'w')
consumer_log = open(root / 'consumer.log', 'w')

producer = subprocess.Popen(
    ['build/producer', '--size', '64', '--reset', '--log-interval-ms', '200'],
    stdout=producer_log,
    stderr=subprocess.STDOUT,
)

time.sleep(0.2)

consumer = subprocess.Popen(
    ['build/consumer', '--log-interval-ms', '200'],
    stdout=consumer_log,
    stderr=subprocess.STDOUT,
)

time.sleep(0.8)

consumer.send_signal(signal.SIGINT)
producer.send_signal(signal.SIGINT)

consumer.wait(timeout=5)
producer.wait(timeout=5)

producer_log.close()
consumer_log.close()

print('--- producer.log ---')
print('\n'.join((root / 'producer.log').read_text().splitlines()[:12]))
print('--- consumer.log ---')
print('\n'.join((root / 'consumer.log').read_text().splitlines()[:12]))
