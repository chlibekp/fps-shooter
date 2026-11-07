//! Reliable, ordered message channel piggybacked on `Data` packets.
//!
//! Wire format of the reliable section (directly after the `DataHeader`):
//!
//! ```text
//! u8  count
//! repeated count times:
//!   u16 msg_seq
//!   u8  msg_type
//!   u16 payload_len
//!   [u8; payload_len]
//! ```
//!
//! Unacked messages are resent (in any outgoing packet) when they have not
//! been sent for `resend_after`. A message is acked when any packet that
//! carried it is acked.

use crate::ack::seq_greater;
use crate::bytes::{Reader, Writer};
use crate::DecodeError;
use std::collections::VecDeque;
use std::time::{Duration, Instant};

const PACKET_WINDOW: usize = 256;
const RECV_WINDOW: usize = 256;
/// Maximum messages waiting for ack before the connection is considered broken.
pub const MAX_PENDING: usize = 1024;
pub const MAX_MSG_PAYLOAD: usize = 512;

struct Pending {
    seq: u16,
    ty: u8,
    payload: Vec<u8>,
    last_sent: Option<Instant>,
}

pub struct ReliableSender {
    next_seq: u16,
    pending: VecDeque<Pending>,
    /// For each in-flight packet sequence: which message sequences it carried.
    in_flight: Vec<(u16, Vec<u16>)>,
}

#[derive(Debug, PartialEq, Eq, thiserror::Error)]
pub enum SendError {
    #[error("reliable queue overflow")]
    Overflow,
    #[error("payload too large")]
    TooLarge,
}

impl ReliableSender {
    pub fn new() -> Self {
        Self {
            next_seq: 0,
            pending: VecDeque::new(),
            in_flight: (0..PACKET_WINDOW).map(|_| (0u16, Vec::new())).collect(),
        }
    }

    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }

    pub fn queue(&mut self, ty: u8, payload: Vec<u8>) -> Result<(), SendError> {
        if payload.len() > MAX_MSG_PAYLOAD {
            return Err(SendError::TooLarge);
        }
        if self.pending.len() >= MAX_PENDING {
            return Err(SendError::Overflow);
        }
        self.pending.push_back(Pending {
            seq: self.next_seq,
            ty,
            payload,
            last_sent: None,
        });
        self.next_seq = self.next_seq.wrapping_add(1);
        Ok(())
    }

    /// Writes the reliable section for packet `packet_seq`, using at most
    /// `budget` bytes (the count byte is always written).
    pub fn write(
        &mut self,
        w: &mut Writer,
        packet_seq: u16,
        now: Instant,
        resend_after: Duration,
        budget: usize,
    ) {
        let count_at = w.len();
        w.u8(0);
        let slot = &mut self.in_flight[packet_seq as usize % PACKET_WINDOW];
        slot.0 = packet_seq;
        slot.1.clear();
        let mut used = 1usize;
        let mut count = 0u8;
        for p in self.pending.iter_mut() {
            if count == u8::MAX {
                break;
            }
            if let Some(t) = p.last_sent {
                if now.saturating_duration_since(t) < resend_after {
                    continue;
                }
            }
            let size = 5 + p.payload.len();
            // Always allow one message per packet so an oversize message can
            // never stall the channel.
            if used + size > budget && count > 0 {
                break;
            }
            w.u16(p.seq);
            w.u8(p.ty);
            w.u16(p.payload.len() as u16);
            w.bytes(&p.payload);
            p.last_sent = Some(now);
            slot.1.push(p.seq);
            used += size;
            count += 1;
        }
        w.patch_u8(count_at, count);
    }

    /// Call when a packet sequence was acknowledged by the peer.
    pub fn on_packet_acked(&mut self, packet_seq: u16) {
        let slot = &mut self.in_flight[packet_seq as usize % PACKET_WINDOW];
        if slot.0 != packet_seq || slot.1.is_empty() {
            return;
        }
        let acked = std::mem::take(&mut slot.1);
        self.pending.retain(|p| !acked.contains(&p.seq));
        // Give the vector's allocation back to the slot for reuse.
        let mut v = acked;
        v.clear();
        self.in_flight[packet_seq as usize % PACKET_WINDOW].1 = v;
    }
}

impl Default for ReliableSender {
    fn default() -> Self {
        Self::new()
    }
}

/// Receives reliable messages and releases them strictly in order.
pub struct ReliableReceiver {
    next_expected: u16,
    buffer: Vec<Option<(u8, Vec<u8>)>>,
    ready: VecDeque<(u8, Vec<u8>)>,
}

impl ReliableReceiver {
    pub fn new() -> Self {
        Self {
            next_expected: 0,
            buffer: (0..RECV_WINDOW).map(|_| None).collect(),
            ready: VecDeque::new(),
        }
    }

    /// Parses the reliable section of a received packet.
    pub fn read(&mut self, r: &mut Reader) -> Result<(), DecodeError> {
        let count = r.u8()?;
        for _ in 0..count {
            let seq = r.u16()?;
            let ty = r.u8()?;
            let len = r.u16()? as usize;
            if len > MAX_MSG_PAYLOAD {
                return Err(DecodeError::Invalid("reliable payload too large"));
            }
            let payload = r.take(len)?;
            if seq == self.next_expected {
                self.ready.push_back((ty, payload.to_vec()));
                self.next_expected = self.next_expected.wrapping_add(1);
                // Drain buffered successors.
                loop {
                    let idx = self.next_expected as usize % RECV_WINDOW;
                    match self.buffer[idx].take() {
                        Some(m) => {
                            self.ready.push_back(m);
                            self.next_expected = self.next_expected.wrapping_add(1);
                        }
                        None => break,
                    }
                }
            } else if seq_greater(seq, self.next_expected)
                && (seq.wrapping_sub(self.next_expected) as usize) < RECV_WINDOW
            {
                let idx = seq as usize % RECV_WINDOW;
                if self.buffer[idx].is_none() {
                    self.buffer[idx] = Some((ty, payload.to_vec()));
                }
            }
            // else: duplicate of an already delivered message, or too far ahead.
        }
        Ok(())
    }

    pub fn pop(&mut self) -> Option<(u8, Vec<u8>)> {
        self.ready.pop_front()
    }
}

impl Default for ReliableReceiver {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn delivers_in_order_despite_loss_and_reordering() {
        let mut tx = ReliableSender::new();
        let mut rx = ReliableReceiver::new();
        for i in 0..100u8 {
            tx.queue(1, vec![i; (i % 10) as usize + 1]).unwrap();
        }
        let start = Instant::now();
        let mut got = Vec::new();
        let mut held_back: Option<(u16, Vec<u8>)> = None;
        for round in 0..400u32 {
            let now = start + Duration::from_millis(round as u64 * 16);
            let mut buf = Vec::new();
            let mut w = Writer::new(&mut buf);
            let pseq = round as u16;
            tx.write(&mut w, pseq, now, Duration::from_millis(100), 200);
            let lost = round % 4 == 0;
            if !lost {
                // Reorder: hold every 5th packet back by one round.
                if round % 5 == 0 {
                    if let Some((s, b)) = held_back.replace((pseq, buf)) {
                        rx.read(&mut Reader::new(&b)).unwrap();
                        tx.on_packet_acked(s);
                    }
                } else {
                    rx.read(&mut Reader::new(&buf)).unwrap();
                    tx.on_packet_acked(pseq);
                }
            }
            while let Some((_, p)) = rx.pop() {
                got.push(p[0]);
            }
        }
        assert_eq!(got, (0..100u8).collect::<Vec<_>>());
        assert_eq!(tx.pending_len(), 0);
    }

    #[test]
    fn overflow_is_reported() {
        let mut tx = ReliableSender::new();
        for _ in 0..MAX_PENDING {
            tx.queue(1, vec![0]).unwrap();
        }
        assert_eq!(tx.queue(1, vec![0]), Err(SendError::Overflow));
        assert_eq!(tx.queue(1, vec![0; 600]), Err(SendError::TooLarge));
    }
}
