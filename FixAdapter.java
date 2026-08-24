/**
 * Quantum-Execution-Engine — FIX Protocol Adapter (Java 17)
 *
 * Industrial object-pool pattern for serializing trade intents
 * into simulated binary FIX session frames.
 *
 * Goals:
 *   - Minimize GC pressure under high message rates
 *   - Reuse StringBuilders and byte buffers
 *   - Thread-safe pool with bounded size
 */

package quantum.fix;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Pooled mutable buffer used for FIX message construction.
 * Returned to the pool after each use to avoid allocation churn.
 */
final class FixBuffer {
    final StringBuilder sb = new StringBuilder(256);
    final ByteBuffer bytes = ByteBuffer.allocate(512);

    void reset() {
        sb.setLength(0);
        bytes.clear();
    }
}

/**
 * Simple fixed-size object pool.
 * Blocks when exhausted (back-pressure) instead of allocating.
 */
final class FixBufferPool {
    private final BlockingQueue<FixBuffer> pool;

    FixBufferPool(int size) {
        this.pool = new ArrayBlockingQueue<>(size);
        for (int i = 0; i < size; i++) {
            pool.offer(new FixBuffer());
        }
    }

    FixBuffer acquire() throws InterruptedException {
        return pool.take();
    }

    void release(FixBuffer buf) {
        buf.reset();
        pool.offer(buf); // never blocks — we created exactly 'size' buffers
    }
}

/**
 * High-level FIX adapter.
 * Demonstrates object pooling + efficient string/buffer reuse.
 */
public final class FixAdapter implements AutoCloseable {

    private final FixBufferPool pool;
    private final AtomicLong messagesSent = new AtomicLong(0);
    private final AtomicLong poolHits = new AtomicLong(0);

    public FixAdapter(int poolSize) {
        if (poolSize < 1) {
            throw new IllegalArgumentException("poolSize must be >= 1");
        }
        this.pool = new FixBufferPool(poolSize);
    }

    /**
     * Serialize a trade into a simplified FIX-like frame.
     * Real production code would use QuickFIX/J or a custom binary encoder.
     */
    public byte[] serializeTrade(String symbol, char side, double qty, double price)
            throws InterruptedException {

        FixBuffer buf = pool.acquire();
        poolHits.incrementAndGet();

        try {
            // Classic FIX tag=value\u0001 style (simplified)
            buf.sb.append("8=FIX.4.4").append('\u0001')
               .append("35=D").append('\u0001')               // NewOrderSingle
               .append("55=").append(symbol).append('\u0001')
               .append("54=").append(side).append('\u0001')   // 1=Buy, 2=Sell
               .append("38=").append(qty).append('\u0001')
               .append("44=").append(price).append('\u0001')
               .append("10=000").append('\u0001');            // checksum placeholder

            byte[] raw = buf.sb.toString().getBytes(StandardCharsets.US_ASCII);
            messagesSent.incrementAndGet();
            return raw;
        } finally {
            pool.release(buf);
        }
    }

    public long getMessagesSent() {
        return messagesSent.get();
    }

    public long getPoolHits() {
        return poolHits.get();
    }

    @Override
    public void close() {
        // Pool is GC-managed; nothing to release explicitly
    }
}
