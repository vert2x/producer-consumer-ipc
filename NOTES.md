# Runtime notes

## Pause / resume behavior

Both applications support keyboard control when started in an interactive terminal:

- `S` — pause
- `R` — resume

### Producer paused

When the producer is paused, it stops generating new packets.
The consumer can continue draining packets that are already in the shared-memory ring buffer.
Once the buffer becomes empty, the consumer waits for new data.

### Consumer paused

When the consumer is paused, it stops taking packets from the shared-memory ring buffer.
The producer continues writing until the ring buffer becomes full, then blocks waiting for free space.
After the consumer resumes, packet flow continues normally.

## Payload size changes

If the producer is restarted with a different payload size for the same SHM name, start it with `--reset` to recreate the shared memory region.
After such a reset, restart the consumer as well so it can attach to the new region.

## Byte counters

`total_bytes` and per-interval `bytes` count payload bytes only.
Packet header bytes are not included in these counters.
