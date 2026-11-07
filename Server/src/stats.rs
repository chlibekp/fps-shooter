//! Server performance counters, shared lock-free with observers (tests,
//! the stats logger, a future admin endpoint).

use std::sync::atomic::{AtomicU64, Ordering::Relaxed};

#[derive(Default)]
pub struct Stats {
    pub ticks: AtomicU64,
    pub players: AtomicU64,
    pub connections: AtomicU64,
    pub packets_in: AtomicU64,
    pub packets_out: AtomicU64,
    pub bytes_in: AtomicU64,
    pub bytes_out: AtomicU64,
    /// Packets dropped because the simulation thread's inbox was full.
    pub inbox_dropped: AtomicU64,
    /// Packets that failed to decode or were rejected.
    pub packets_invalid: AtomicU64,
    /// Sends that would have blocked (socket buffer full) and were dropped.
    pub send_dropped: AtomicU64,
    /// Late ticks (the loop fell more than one tick behind).
    pub late_ticks: AtomicU64,
    /// Tick work duration in microseconds (last / max since last report).
    pub tick_us_last: AtomicU64,
    pub tick_us_max: AtomicU64,
    pub tick_us_sum: AtomicU64,
    pub tick_count_window: AtomicU64,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct StatsSnapshot {
    pub ticks: u64,
    pub players: u64,
    pub connections: u64,
    pub packets_in: u64,
    pub packets_out: u64,
    pub bytes_in: u64,
    pub bytes_out: u64,
    pub inbox_dropped: u64,
    pub packets_invalid: u64,
    pub send_dropped: u64,
    pub late_ticks: u64,
    pub tick_us_last: u64,
}

impl Stats {
    pub fn snapshot(&self) -> StatsSnapshot {
        StatsSnapshot {
            ticks: self.ticks.load(Relaxed),
            players: self.players.load(Relaxed),
            connections: self.connections.load(Relaxed),
            packets_in: self.packets_in.load(Relaxed),
            packets_out: self.packets_out.load(Relaxed),
            bytes_in: self.bytes_in.load(Relaxed),
            bytes_out: self.bytes_out.load(Relaxed),
            inbox_dropped: self.inbox_dropped.load(Relaxed),
            packets_invalid: self.packets_invalid.load(Relaxed),
            send_dropped: self.send_dropped.load(Relaxed),
            late_ticks: self.late_ticks.load(Relaxed),
            tick_us_last: self.tick_us_last.load(Relaxed),
        }
    }

    pub fn record_tick(&self, us: u64) {
        self.ticks.fetch_add(1, Relaxed);
        self.tick_us_last.store(us, Relaxed);
        self.tick_us_max.fetch_max(us, Relaxed);
        self.tick_us_sum.fetch_add(us, Relaxed);
        self.tick_count_window.fetch_add(1, Relaxed);
    }

    /// Returns (avg_us, max_us) since the previous call and resets the window.
    pub fn take_tick_window(&self) -> (f64, u64) {
        let n = self.tick_count_window.swap(0, Relaxed);
        let sum = self.tick_us_sum.swap(0, Relaxed);
        let max = self.tick_us_max.swap(0, Relaxed);
        (if n > 0 { sum as f64 / n as f64 } else { 0.0 }, max)
    }
}
