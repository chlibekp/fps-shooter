//! Server core: owns all connections and the game world. Runs entirely on the
//! simulation thread; receives datagrams from the network task through a
//! channel and sends with a non-blocking socket.

use crate::config::ServerConfig;
use crate::connection::Connection;
use crate::game::GameWorld;
use crate::stats::Stats;
use rand::RngCore;
use std::collections::HashMap;
use std::hash::{BuildHasher, Hash, Hasher};
use std::net::{SocketAddr, UdpSocket};
use std::sync::atomic::Ordering::Relaxed;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tfps_protocol::input::InputBundle;
use tfps_protocol::messages::{ConnState, Message};
use tfps_protocol::packet::{
    self, BodyKind, Challenge, ChallengeResponse, ConnectAccepted, ConnectRejected, ConnectRequest,
    DataHeader, Disconnect, DisconnectReason, PacketType, RejectReason,
};
use tfps_protocol::snapshot::{self, EntityState, SnapshotHeader};
use tfps_protocol::{Writer, MAX_PACKET_SIZE, PROTOCOL_VERSION};
use tfps_sim::{MapDef, MoveState};
use tracing::{debug, info, warn};

/// Bytes of each packet reserved for reliable messages.
const RELIABLE_BUDGET: usize = 400;
const RELIABLE_RESEND: Duration = Duration::from_millis(100);
/// Cookie time bucket length.
const COOKIE_BUCKET_SECS: u64 = 10;
/// Max ChallengeResponses processed per tick (CPU flood protection).
const MAX_HANDSHAKES_PER_TICK: u32 = 32;

struct SavedPlayer {
    id: u16,
    team: u8,
    name: String,
    state: MoveState,
    expires: Instant,
}

pub struct ServerCore {
    cfg: ServerConfig,
    socket: UdpSocket,
    stats: Arc<Stats>,
    pub game: GameWorld,
    map_hash: u64,
    start: Instant,
    cookie_key: std::collections::hash_map::RandomState,
    conns: HashMap<u64, Connection>,
    addr_index: HashMap<SocketAddr, u64>,
    saved: HashMap<u64, SavedPlayer>,
    handshakes_this_tick: u32,
    send_buf: Vec<u8>,
    entities: Vec<EntityState>,
    scratch: Vec<EntityState>,
    last_stats_log: Instant,
    last_stats: crate::stats::StatsSnapshot,
}

impl ServerCore {
    pub fn new(cfg: ServerConfig, map: MapDef, socket: UdpSocket, stats: Arc<Stats>) -> Self {
        let map_hash = map.geometry_hash();
        let now = Instant::now();
        Self {
            game: GameWorld::new(map, cfg.tick_rate),
            cfg,
            socket,
            stats,
            map_hash,
            start: now,
            cookie_key: Default::default(),
            conns: HashMap::new(),
            addr_index: HashMap::new(),
            saved: HashMap::new(),
            handshakes_this_tick: 0,
            send_buf: Vec::with_capacity(MAX_PACKET_SIZE),
            entities: Vec::with_capacity(64),
            scratch: Vec::with_capacity(64),
            last_stats_log: now,
            last_stats: Default::default(),
        }
    }

    pub fn tick_rate(&self) -> u16 {
        self.cfg.tick_rate
    }

    fn send_raw(socket: &UdpSocket, stats: &Stats, addr: SocketAddr, data: &[u8]) {
        match socket.send_to(data, addr) {
            Ok(n) => {
                stats.packets_out.fetch_add(1, Relaxed);
                stats.bytes_out.fetch_add(n as u64, Relaxed);
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                stats.send_dropped.fetch_add(1, Relaxed);
            }
            Err(e) => debug!("send to {addr} failed: {e}"),
        }
    }

    fn send(&self, addr: SocketAddr, data: &[u8]) {
        Self::send_raw(&self.socket, &self.stats, addr, data);
    }

    fn cookie(&self, addr: SocketAddr, nonce: u64, bucket: u64) -> u64 {
        let mut h = self.cookie_key.build_hasher();
        addr.hash(&mut h);
        nonce.hash(&mut h);
        bucket.hash(&mut h);
        h.finish()
    }

    fn cookie_bucket(&self, now: Instant) -> u64 {
        now.duration_since(self.start).as_secs() / COOKIE_BUCKET_SECS + 1
    }

    // ---------------------------------------------------------------------
    // Incoming packets
    // ---------------------------------------------------------------------

    pub fn handle_packet(&mut self, addr: SocketAddr, data: &[u8], now: Instant) {
        self.stats.packets_in.fetch_add(1, Relaxed);
        self.stats.bytes_in.fetch_add(data.len() as u64, Relaxed);
        if self.process(addr, data, now).is_err() {
            self.stats.packets_invalid.fetch_add(1, Relaxed);
        }
    }

    fn process(
        &mut self,
        addr: SocketAddr,
        data: &[u8],
        now: Instant,
    ) -> Result<(), tfps_protocol::DecodeError> {
        let (hdr, mut r) = packet::read_header(data)?;
        match hdr.ty {
            PacketType::ConnectRequest => {
                let req = ConnectRequest::decode(&mut r, data.len())?;
                if hdr.version != PROTOCOL_VERSION {
                    self.reject(addr, req.client_nonce, RejectReason::VersionMismatch);
                    return Ok(());
                }
                let cookie = self.cookie(addr, req.client_nonce, self.cookie_bucket(now));
                self.send_buf.clear();
                Challenge {
                    client_nonce: req.client_nonce,
                    cookie,
                }
                .encode(&mut self.send_buf);
                self.send(addr, &self.send_buf);
            }
            PacketType::ChallengeResponse => {
                let resp = ChallengeResponse::decode(&mut r, data.len())?;
                if hdr.version != PROTOCOL_VERSION {
                    self.reject(addr, resp.client_nonce, RejectReason::VersionMismatch);
                    return Ok(());
                }
                self.handle_challenge_response(addr, resp, now);
            }
            PacketType::Data => {
                if hdr.version != PROTOCOL_VERSION {
                    return Err(tfps_protocol::DecodeError::BadVersion(hdr.version));
                }
                self.handle_data(addr, &mut r, now)?;
            }
            PacketType::Disconnect => {
                let d = Disconnect::decode(&mut r)?;
                if let Some(c) = self.conns.get_mut(&d.session_key) {
                    if c.addr == addr {
                        c.doomed = Some(DisconnectReason::ClientQuit);
                    }
                }
            }
            // Server-to-client packet types are never valid here.
            PacketType::Challenge | PacketType::ConnectAccepted | PacketType::ConnectRejected => {
                return Err(tfps_protocol::DecodeError::Invalid(
                    "unexpected packet type",
                ));
            }
        }
        Ok(())
    }

    fn reject(&mut self, addr: SocketAddr, client_nonce: u64, reason: RejectReason) {
        self.send_buf.clear();
        ConnectRejected {
            client_nonce,
            reason,
        }
        .encode(&mut self.send_buf);
        self.send(addr, &self.send_buf);
    }

    fn handle_challenge_response(
        &mut self,
        addr: SocketAddr,
        resp: ChallengeResponse,
        now: Instant,
    ) {
        // Repeated response from an already accepted client: resend accept.
        if let Some(key) = self.addr_index.get(&addr) {
            if let Some(c) = self.conns.get(key) {
                if c.client_nonce == resp.client_nonce {
                    self.send(addr, &c.accepted_packet);
                    return;
                }
            }
        }
        if self.handshakes_this_tick >= MAX_HANDSHAKES_PER_TICK {
            return; // client will retry
        }
        self.handshakes_this_tick += 1;

        let bucket = self.cookie_bucket(now);
        let valid = resp.cookie == self.cookie(addr, resp.client_nonce, bucket)
            || resp.cookie == self.cookie(addr, resp.client_nonce, bucket - 1);
        if !valid {
            self.reject(addr, resp.client_nonce, RejectReason::InvalidChallenge);
            return;
        }
        if let Some(required) = self.cfg.join_token {
            if resp.join_token != required {
                self.reject(addr, resp.client_nonce, RejectReason::Unauthorized);
                return;
            }
        }
        let Some(name) = sanitize_name(&resp.name) else {
            self.reject(addr, resp.client_nonce, RejectReason::InvalidName);
            return;
        };

        // A new session from an address that already has one (client restart):
        // the old session is replaced.
        if let Some(old_key) = self.addr_index.get(&addr).copied() {
            if let Some(c) = self.conns.get_mut(&old_key) {
                if c.reconnect_token != resp.reconnect_token || resp.reconnect_token == 0 {
                    c.doomed = Some(DisconnectReason::ClientQuit);
                    self.drop_doomed(now);
                }
            }
        }

        // Session takeover: the token belongs to a live connection (client
        // lost its connection but the server has not timed it out yet).
        let takeover = if resp.reconnect_token != 0 {
            self.conns
                .iter()
                .find(|(_, c)| c.reconnect_token == resp.reconnect_token)
                .map(|(k, _)| *k)
        } else {
            None
        };

        let (player_id, restored, rejoin_existing) = if let Some(old_key) = takeover {
            let old = self.conns.remove(&old_key).expect("conn exists");
            self.addr_index.remove(&old.addr);
            (old.player_id, None, true)
        } else if let Some(saved) = (resp.reconnect_token != 0)
            .then(|| self.saved.remove(&resp.reconnect_token))
            .flatten()
        {
            (saved.id, Some(saved), false)
        } else {
            if self.game.players().len() + self.saved.len() >= self.cfg.max_players as usize {
                self.reject(addr, resp.client_nonce, RejectReason::ServerFull);
                return;
            }
            (self.allocate_player_id(), None, false)
        };

        let mut rng = rand::rng();
        let session_key = loop {
            let k = rng.next_u64();
            if k != 0 && !self.conns.contains_key(&k) {
                break k;
            }
        };
        let reconnect_token = loop {
            let t = rng.next_u64();
            if t != 0 && t != session_key {
                break t;
            }
        };

        let (team, pname) = if rejoin_existing {
            let p = self.game.player(player_id).expect("player of live conn");
            (p.team, p.name.clone())
        } else if let Some(s) = &restored {
            (s.team, s.name.clone())
        } else {
            let team = self.pick_team();
            (team, name)
        };
        if !rejoin_existing {
            let state = restored.as_ref().map(|s| s.state);
            self.game.add_player(player_id, team, pname.clone(), state);
        } else if let Some(p) = self.game.player_mut(player_id) {
            // New client process: its command sequence restarts.
            p.cmds = Default::default();
        }

        let mut conn = Connection::new(
            addr,
            session_key,
            resp.client_nonce,
            reconnect_token,
            player_id,
            now,
        );
        ConnectAccepted {
            client_nonce: resp.client_nonce,
            session_key,
            player_id,
            tick_rate: self.cfg.tick_rate,
            snapshot_interval: self.cfg.snapshot_interval,
            server_tick: self.game.tick,
            reconnect_token,
            map_hash: self.map_hash,
            map_name: self.game.map.name.clone(),
        }
        .encode(&mut conn.accepted_packet);
        self.send(addr, &conn.accepted_packet);

        // Tell the new client about everyone (including itself).
        for p in self.game.players() {
            let m = Message::PlayerJoined {
                player_id: p.id,
                team: p.team,
                name: p.name.clone(),
            };
            let _ = conn.rel_tx.queue(m.msg_type(), m.encode_payload());
        }
        self.conns.insert(session_key, conn);
        self.addr_index.insert(addr, session_key);

        if rejoin_existing {
            self.broadcast_except(
                player_id,
                &Message::PlayerConnState {
                    player_id,
                    state: ConnState::Connected,
                },
            );
        } else {
            self.broadcast_except(
                player_id,
                &Message::PlayerJoined {
                    player_id,
                    team,
                    name: pname.clone(),
                },
            );
        }
        info!(
            player_id,
            name = %pname,
            %addr,
            reconnect = restored.is_some() || rejoin_existing,
            "player connected"
        );
        self.update_counts();
    }

    fn allocate_player_id(&self) -> u16 {
        (1..=u16::MAX)
            .find(|id| self.game.player(*id).is_none() && !self.saved.values().any(|s| s.id == *id))
            .expect("player ids exhausted")
    }

    fn pick_team(&self) -> u8 {
        let t0 = self.game.players().iter().filter(|p| p.team == 0).count();
        let t1 = self.game.players().iter().filter(|p| p.team == 1).count();
        if t1 < t0 {
            1
        } else {
            0
        }
    }

    fn handle_data(
        &mut self,
        addr: SocketAddr,
        r: &mut tfps_protocol::Reader,
        now: Instant,
    ) -> Result<(), tfps_protocol::DecodeError> {
        let dh = DataHeader::decode(r)?;
        let Some(conn) = self.conns.get_mut(&dh.session_key) else {
            return Err(tfps_protocol::DecodeError::Invalid("unknown session"));
        };
        let Connection { ack, rel_tx, .. } = conn;
        let fresh = ack.on_incoming(dh.seq, dh.ack, dh.ack_bits, now, |s| {
            rel_tx.on_packet_acked(s)
        });
        if !fresh {
            return Ok(());
        }
        // Address migration (NAT rebinding): the session key authenticates.
        if conn.addr != addr {
            info!(player_id = conn.player_id, old = %conn.addr, new = %addr, "client address changed");
            self.addr_index.remove(&conn.addr);
            self.addr_index.insert(addr, dh.session_key);
            conn.addr = addr;
        }
        conn.last_recv = now;
        let resumed = conn.interrupted;
        conn.interrupted = false;
        conn.rel_rx.read(r)?;
        // No client->server reliable messages are defined in v1; drain them.
        while conn.rel_rx.pop().is_some() {}
        let player_id = conn.player_id;
        match BodyKind::from_u8(r.u8()?)? {
            BodyKind::None => {}
            BodyKind::Input => {
                let bundle = InputBundle::decode(r)?;
                if bundle.ack_snapshot_tick > conn.client_ack_snapshot
                    && bundle.ack_snapshot_tick <= self.game.tick
                {
                    conn.client_ack_snapshot = bundle.ack_snapshot_tick;
                }
                if let Some(p) = self.game.player_mut(player_id) {
                    for (seq, cmd) in bundle.iter_oldest_first() {
                        p.cmds.push(seq, cmd);
                    }
                }
            }
            BodyKind::Snapshot => {
                return Err(tfps_protocol::DecodeError::Invalid("snapshot from client"))
            }
        }
        if resumed {
            self.broadcast_except(
                player_id,
                &Message::PlayerConnState {
                    player_id,
                    state: ConnState::Connected,
                },
            );
        }
        Ok(())
    }

    // ---------------------------------------------------------------------
    // Tick
    // ---------------------------------------------------------------------

    /// One fixed simulation step plus networking housekeeping.
    pub fn tick(&mut self, now: Instant) {
        self.handshakes_this_tick = 0;
        self.game.tick();
        self.check_timeouts(now);
        self.drop_doomed(now);
        if self
            .game
            .tick
            .is_multiple_of(self.cfg.snapshot_interval as u32)
        {
            self.send_snapshots(now);
        }
        self.saved.retain(|_, s| s.expires > now);
    }

    fn check_timeouts(&mut self, now: Instant) {
        let mut interrupted = Vec::new();
        for c in self.conns.values_mut() {
            let silent = now.saturating_duration_since(c.last_recv);
            if silent > self.cfg.timeout_after {
                c.doomed.get_or_insert(DisconnectReason::Timeout);
            } else if silent > self.cfg.interrupt_after && !c.interrupted {
                c.interrupted = true;
                interrupted.push(c.player_id);
            }
        }
        for id in interrupted {
            info!(player_id = id, "player connection interrupted");
            self.broadcast_except(
                id,
                &Message::PlayerConnState {
                    player_id: id,
                    state: ConnState::Interrupted,
                },
            );
        }
    }

    fn drop_doomed(&mut self, now: Instant) {
        let doomed: Vec<u64> = self
            .conns
            .iter()
            .filter(|(_, c)| c.doomed.is_some())
            .map(|(k, _)| *k)
            .collect();
        for key in doomed {
            let c = self.conns.remove(&key).expect("present");
            let reason = c.doomed.unwrap_or(DisconnectReason::Unknown);
            self.addr_index.remove(&c.addr);
            if reason != DisconnectReason::Timeout {
                // Tell the client explicitly (unreliable, sent 3x).
                self.send_buf.clear();
                Disconnect {
                    session_key: c.session_key,
                    reason,
                }
                .encode(&mut self.send_buf);
                for _ in 0..3 {
                    self.send(c.addr, &self.send_buf);
                }
            }
            if let Some(p) = self.game.remove_player(c.player_id) {
                if reason != DisconnectReason::Kicked {
                    self.saved.insert(
                        c.reconnect_token,
                        SavedPlayer {
                            id: p.id,
                            team: p.team,
                            name: p.name,
                            state: p.state,
                            expires: now + self.cfg.reconnect_window,
                        },
                    );
                }
            }
            info!(player_id = c.player_id, ?reason, "player disconnected");
            self.broadcast_except(
                c.player_id,
                &Message::PlayerLeft {
                    player_id: c.player_id,
                    reason: reason as u8,
                },
            );
        }
        self.update_counts();
    }

    fn broadcast_except(&mut self, except_player: u16, msg: &Message) {
        let payload = msg.encode_payload();
        let ty = msg.msg_type();
        for c in self.conns.values_mut() {
            if c.player_id == except_player {
                continue;
            }
            if c.rel_tx.queue(ty, payload.clone()).is_err() {
                warn!(
                    player_id = c.player_id,
                    "reliable queue overflow, dropping client"
                );
                c.doomed = Some(DisconnectReason::ProtocolError);
            }
        }
    }

    fn send_snapshots(&mut self, now: Instant) {
        self.game.collect_entities(&mut self.entities);
        let tick = self.game.tick;
        let mut buf = std::mem::take(&mut self.send_buf);
        for c in self.conns.values_mut() {
            let Some(player) = self.game.player(c.player_id) else {
                continue;
            };
            self.scratch.clear();
            self.scratch.extend(
                self.entities
                    .iter()
                    .filter(|e| e.id != c.player_id)
                    .copied(),
            );

            let base_idx = c.baseline_index(c.client_ack_snapshot);
            let baseline = base_idx.map(|i| c.history[i].1.as_slice());
            let header = SnapshotHeader {
                server_tick: tick,
                baseline_tick: if baseline.is_some() {
                    c.client_ack_snapshot
                } else {
                    0
                },
                last_cmd_seq: player.cmds.last_processed_seq,
                input_buffer_depth: player.reported_depth(),
                local: player.state,
                local_health: player.health,
            };
            buf.clear();
            let mut w = Writer::new(&mut buf);
            let Connection {
                ack: c_ack,
                rel_tx,
                history,
                session_key,
                ..
            } = c;
            let baseline = base_idx.map(|i| history[i].1.as_slice());
            let (seq, ack, ack_bits) = c_ack.next_outgoing(now);
            DataHeader {
                session_key: *session_key,
                seq,
                ack,
                ack_bits,
            }
            .encode(&mut w);
            rel_tx.write(&mut w, seq, now, RELIABLE_RESEND, RELIABLE_BUDGET);
            w.u8(BodyKind::Snapshot as u8);
            snapshot::encode(&mut w, &header, &self.scratch, baseline);
            debug_assert!(buf.len() <= MAX_PACKET_SIZE);
            c.remember(tick, &self.scratch);
            Self::send_raw(&self.socket, &self.stats, c.addr, &buf);
        }
        self.send_buf = buf;
    }

    fn update_counts(&self) {
        self.stats
            .players
            .store(self.game.players().len() as u64, Relaxed);
        self.stats
            .connections
            .store(self.conns.len() as u64, Relaxed);
    }

    /// Logs periodic statistics.
    pub fn maybe_log_stats(&mut self, now: Instant) {
        let elapsed = now.duration_since(self.last_stats_log);
        if elapsed < self.cfg.stats_interval {
            return;
        }
        let s = self.stats.snapshot();
        let (avg, max) = self.stats.take_tick_window();
        let secs = elapsed.as_secs_f64();
        let prev = self.last_stats;
        info!(
            tick = self.game.tick,
            players = s.players,
            tick_avg_us = format!("{avg:.1}"),
            tick_max_us = max,
            ticks_per_s = format!("{:.1}", (s.ticks - prev.ticks) as f64 / secs),
            pkts_in_s = format!("{:.0}", (s.packets_in - prev.packets_in) as f64 / secs),
            pkts_out_s = format!("{:.0}", (s.packets_out - prev.packets_out) as f64 / secs),
            kbit_in_s = format!(
                "{:.1}",
                (s.bytes_in - prev.bytes_in) as f64 * 8.0 / 1000.0 / secs
            ),
            kbit_out_s = format!(
                "{:.1}",
                (s.bytes_out - prev.bytes_out) as f64 * 8.0 / 1000.0 / secs
            ),
            invalid = s.packets_invalid - prev.packets_invalid,
            late_ticks = s.late_ticks - prev.late_ticks,
            "server stats"
        );
        self.last_stats = s;
        self.last_stats_log = now;
    }

    /// Notifies all clients that the server is shutting down.
    pub fn shutdown(&mut self) {
        for c in self.conns.values_mut() {
            c.doomed = Some(DisconnectReason::ServerShutdown);
        }
        self.drop_doomed(Instant::now());
    }
}

/// Trims the name and rejects control characters. Empty names are allowed
/// and replaced by a default.
fn sanitize_name(raw: &str) -> Option<String> {
    let name = raw.trim();
    if name.chars().any(|c| c.is_control()) {
        return None;
    }
    if name.is_empty() {
        return Some("Operator".to_string());
    }
    Some(name.to_string())
}

#[cfg(test)]
mod tests {
    use super::sanitize_name;
    #[test]
    fn names() {
        assert_eq!(sanitize_name("  Ghost "), Some("Ghost".into()));
        assert_eq!(sanitize_name(""), Some("Operator".into()));
        assert_eq!(sanitize_name("a\nb"), None);
    }
}
