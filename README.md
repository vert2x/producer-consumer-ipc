# Producer / Consumer shared-memory demo

This project implements two separate CLI applications that exchange packets through POSIX shared memory and POSIX semaphores.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

Default shared-memory name:

```sh
build/producer --size 64 --reset
build/consumer
```

Custom shared-memory name:

```sh
build/producer --size 64 --reset --shm-name /shm_ipc_demo
build/consumer --shm-name /shm_ipc_demo
```

## CLI options

### Producer

```text
--size <payload_bytes>          required payload size
--reset                         recreate shared memory before start
--log-interval-ms <ms>          log interval in milliseconds
--shm-name <name>               POSIX shared-memory object name
```

### Consumer

```text
--log-interval-ms <ms>          stats interval in milliseconds
--shm-name <name>               POSIX shared-memory object name
```

## Pause / resume behavior

When started in an interactive terminal, both applications support:

- `S` — pause
- `R` — resume

### If the producer is paused

The producer stops generating new packets.
The consumer can continue draining packets already present in the ring buffer.
Once the ring becomes empty, the consumer waits for new data.

### If the consumer is paused

The consumer stops taking packets from the ring buffer.
The producer continues writing until the ring buffer becomes full, then blocks waiting for free space.
After the consumer resumes, flow continues normally.

## Payload size changes

If the producer is restarted with a different payload size for the same SHM name, start it with `--reset` to recreate the shared-memory region:

```sh
build/producer --size 128 --reset --shm-name /shm_ipc_demo
```

After such a reset, restart the consumer too so it attaches to the new shared-memory region.

## What the applications print

### Producer

Producer prints packet metadata periodically according to `--log-interval-ms`.

### Consumer

Consumer validates packet checksums and prints aggregated statistics for each interval:

- total packets received
- total payload bytes received
- packets per second
- bytes per second

Checksum mismatches are printed immediately with packet details.
