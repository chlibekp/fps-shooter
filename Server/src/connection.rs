//! Per-client connection state.

use std::net::SocketAddr;
use std::time::Instant;
use tfps_protocol::ack::AckState;
use tfps_protocol::reliable::{ReliableReceiver, ReliableSender};
use tfps_protocol::snapshot::EntityState;

/// Number of sent snapshots remembered per client for delta compression.
pub const SNAPSHOT_HISTORY: usize = 32;

pub struct Connection {
    pub addr: SocketAddr,
    pub session_key: u64,
    pub client_nonce: u64,
    pub reconnect_token: u64,
    pub player_id: u16,
    pub ack: AckState,
    pub rel_tx: ReliableSender,
    pub rel_rx: ReliableReceiver,
    pub last_recv: Instant,
    pub connected_at: Instant,
    pub interrupted: bool,
    /// Newest snapshot tick the client reported as decoded.
    pub client_ack_snapshot: u32,
    /// Ring of (tick, entities) snapshots sent to this client.
    pub history: Vec<(u32, Vec<EntityState>)>,
    pub history_cursor: usize,
    /// Cached `ConnectAccepted` packet, re-sent if the client repeats its
    /// `ChallengeResponse` (the first accept may have been lost).
    pub accepted_packet: Vec<u8>,
    /// Set when the connection must be dropped at the end of the tick.
    pub doomed: Option<tfps_protocol::packet::DisconnectReason>,
}

impl Connection {
    pub fn new(
        addr: SocketAddr,
        session_key: u64,
        client_nonce: u64,
        reconnect_token: u64,
        player_id: u16,
        now: Instant,
    ) -> Self {
        Self {
            addr,
            session_key,
            client_nonce,
            reconnect_token,
            player_id,
            ack: AckState::new(),
            rel_tx: ReliableSender::new(),
            rel_rx: ReliableReceiver::new(),
            last_recv: now,
            connected_at: now,
            interrupted: false,
            client_ack_snapshot: 0,
            history: (0..SNAPSHOT_HISTORY)
                .map(|_| (0u32, Vec::with_capacity(16)))
                .collect(),
            history_cursor: 0,
            accepted_packet: Vec::new(),
            doomed: None,
        }
    }

    /// History index of a previously sent snapshot, if still remembered.
    pub fn baseline_index(&self, tick: u32) -> Option<usize> {
        if tick == 0 {
            return None;
        }
        self.history.iter().position(|(t, _)| *t == tick)
    }

    pub fn remember(&mut self, tick: u32, entities: &[EntityState]) {
        let slot = &mut self.history[self.history_cursor];
        slot.0 = tick;
        slot.1.clear();
        slot.1.extend_from_slice(entities);
        self.history_cursor = (self.history_cursor + 1) % SNAPSHOT_HISTORY;
    }
}
