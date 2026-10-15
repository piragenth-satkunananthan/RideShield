package com.rideshield.phone.camera

import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

/**
 * Thread-safe bounded newest-frame queue.
 * Drops older frames when backpressure occurs to guarantee inference workers
 * only process fresh frames within the strict 500ms latency envelope.
 */
class BoundedFrameQueue(
    val capacity: Int = 1
) {
    private val lock = ReentrantLock()
    private val notEmpty = lock.newCondition()
    private val queue = ArrayDeque<CameraFrame>(capacity + 1)

    private val _totalPushed = AtomicLong(0L)
    val totalPushed: Long get() = _totalPushed.get()

    private val _totalDropped = AtomicLong(0L)
    val totalDropped: Long get() = _totalDropped.get()

    /**
     * Enqueues a new camera frame.
     * If the queue is at capacity, the oldest unconsumed frame is discarded.
     */
    fun push(frame: CameraFrame) {
        lock.withLock {
            _totalPushed.incrementAndGet()
            while (queue.size >= capacity) {
                queue.pollFirst() // Discard oldest
                _totalDropped.incrementAndGet()
            }
            queue.addLast(frame)
            notEmpty.signal()
        }
    }

    /**
     * Non-blocking poll for the newest available frame.
     */
    fun poll(): CameraFrame? {
        return lock.withLock {
            queue.pollFirst()
        }
    }

    /**
     * Blocking take waiting for next frame, or null if timeout occurs.
     */
    fun take(timeoutMs: Long): CameraFrame? {
        return lock.withLock {
            if (queue.isEmpty()) {
                val nanos = java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(timeoutMs)
                notEmpty.awaitNanos(nanos)
            }
            queue.pollFirst()
        }
    }

    fun clear() {
        lock.withLock {
            queue.clear()
        }
    }

    val size: Int
        get() = lock.withLock { queue.size }
}
