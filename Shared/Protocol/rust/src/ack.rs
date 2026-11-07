//! Packet sequence numbers, ack bitfields and RTT estimation.
//!
//! Every `Data` packet carries its own 16-bit sequence number plus the newest
//! sequence received from the peer (`ack`) and a 32-bit field acknowledging the
//! 32 packets before it. A packet is therefore acked redundantly by up to 33
//! subsequent packets, so acks survive heavy loss.

use std::time::{Duration, Instant};

/// True if `a` is newer than `b`, accounting for wrap-around.
#[inline]
pub fn seq_greater(a: u16, b: u16) -> bool {
    (a > b && a - b <= 32768) || (a < b && b - a > 32768)
}

const WINDOW: usize = 256;

#[derive(Clone, Copy)]
struct SentRecord {
    seq: u16,
    valid: bool,
    acked: bool,
    sent_at: Instant,
}

/// Per-connection ack bookkeeping.
pub struct AckState {
    next_seq: u16,
    remote_latest: u16,
    remote_bits: u32,
    has_remote: bool,
    sent: Box<[SentRecord; WINDOW]>,
    /// Smoothed round-trip time.
    pub rtt: Duration,
    rtt_initialized: bool,
    pub packets_sent: u64,
    pub packets_acked: u64,
}

impl AckState {
    pub fn new() -> Self {
        let now = Instant::now();
        Self {
            next_seq: 0,
            remote_latest: 0,
            remote_bits: 0,
            has_remote: false,
            sent: Box::new([SentRecord { seq: 0, valid: false, acked: false, sent_at: now }; WINDOW]),
            rtt: Duration::from_millis(100),
            rtt_initialized: false,
            packets_sent: 0,
            packets_acked: 0,
        }
    }

    /// Allocates the sequence number for an outgoing packet and returns
    /// `(seq, ack, ack_bits)` to put in its header.
    pub fn next_outgoing(&mut self, now: Instant) -> (u16, u16, u32) {
        let seq = self.next_seq;
        self.next_seq = self.next_seq.wrapping_add(1);
        self.sent[seq as usize % WINDOW] = SentRecord { seq, valid: true, acked: false, sent_at: now };
        self.packets_sent += 1;
        (seq, self.remote_latest, self.remote_bits)
    }

    /// Processes the header of a received packet.
    ///
    /// Returns `false` if the packet is a duplicate or too old (> 32 behind)
    /// and must be ignored. `on_acked` is called once for every local packet
    /// that became acknowledged.
    pub fn on_incoming(
        &mut self,
        seq: u16,
        ack: u16,
        ack_bits: u32,
        now: Instant,
        mut on_acked: impl FnMut(u16),
    ) -> bool {
        // Remote sequence tracking.
        if !self.has_remote {
            self.has_remote = true;
            self.remote_latest = seq;
            self.remote_bits = 0;
        } else if seq_greater(seq, self.remote_latest) {
            let shift = seq.wrapping_sub(self.remote_latest) as u32;
            self.remote_bits = if shift >= 32 { 0 } else { (self.remote_bits << shift) | (1u32 << (shift - 1)) };
            if shift > 32 {
                // gap larger than the bitfield: older packets are simply unacked
            } else if shift == 32 {
                self.remote_bits = 1u32 << 31;
            }
            self.remote_latest = seq;
        } else {
            let back = self.remote_latest.wrapping_sub(seq) as u32;
            if back == 0 || back > 32 {
                return false;
            }
            let bit = 1u32 << (back - 1);
            if self.remote_bits & bit != 0 {
                return false;
            }
            self.remote_bits |= bit;
        }

        // Local acks.
        self.ack_one(ack, now, &mut on_acked);
        for i in 0..32u16 {
            if ack_bits & (1 << i) != 0 {
                self.ack_one(ack.wrapping_sub(i + 1), now, &mut on_acked);
            }
        }
        true
    }

    fn ack_one(&mut self, seq: u16, now: Instant, on_acked: &mut impl FnMut(u16)) {
        let rec = &mut self.sent[seq as usize % WINDOW];
        if !rec.valid || rec.seq != seq || rec.acked {
            return;
        }
        rec.acked = true;
        self.packets_acked += 1;
        let sample = now.saturating_duration_since(rec.sent_at);
        if !self.rtt_initialized {
            self.rtt = sample;
            self.rtt_initialized = true;
        } else {
            // EWMA, alpha = 1/8 (as TCP's SRTT).
            self.rtt = (self.rtt * 7 + sample) / 8;
        }
        on_acked(seq);
    }
}

impl Default for AckState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wraparound_compare() {
        assert!(seq_greater(1, 0));
        assert!(seq_greater(0, 65535));
        assert!(!seq_greater(65535, 0));
        assert!(!seq_greater(5, 5));
    }

    /// Two endpoints exchanging packets with loss; every packet the receiver
    /// got must eventually be acked to the sender, and nothing else.
    #[test]
    fn acks_with_loss_and_wrap() {
        let mut a = AckState::new();
        let mut b = AckState::new();
        a.next_seq = 65500; // exercise wrap-around
        let now = Instant::now();
        let mut delivered = std::collections::HashSet::new();
        let mut acked = std::collections::HashSet::new();
        for i in 0..400u32 {
            let (seq, ack, bits) = a.next_outgoing(now);
            let lost = i % 3 == 1 || i % 7 == 0;
            if !lost {
                assert!(b.on_incoming(seq, ack, bits, now, |_| {}));
                delivered.insert(seq);
                // duplicate is rejected
                assert!(!b.on_incoming(seq, ack, bits, now, |_| {}));
            }
            // b replies every packet (reply never lost here)
            let (bseq, back, bbits) = b.next_outgoing(now);
            a.on_incoming(bseq, back, bbits, now, |s| {
                acked.insert(s);
            });
        }
        assert!(acked.is_subset(&delivered));
        // Everything except possibly the very last few is acked.
        assert!(delivered.len() - acked.len() <= 1, "{} vs {}", delivered.len(), acked.len());
    }

    #[test]
    fn out_of_order_is_accepted_once() {
        let mut b = AckState::new();
        let now = Instant::now();
        assert!(b.on_incoming(10, 0, 0, now, |_| {}));
        assert!(b.on_incoming(12, 0, 0, now, |_| {}));
        assert!(b.on_incoming(11, 0, 0, now, |_| {}));
        assert!(!b.on_incoming(11, 0, 0, now, |_| {}));
        assert!(!b.on_incoming(12u16.wrapping_sub(40), 0, 0, now, |_| {}));
        assert_eq!(b.remote_latest, 12);
        assert_eq!(b.remote_bits & 0b11, 0b11);
    }
}
