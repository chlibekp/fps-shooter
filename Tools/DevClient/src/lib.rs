//! Reference game client (headless).
//!
//! This is the Rust reference implementation of the client-side netcode that
//! the Unreal client mirrors in `UTfpsNetClient` / `FTfpsClientCore`:
//! handshake, fixed-tick input sampling, client-side prediction, server
//! reconciliation, snapshot delta decoding, remote-entity interpolation and
//! client time dilation.
//!
//! It is driven by calling [`Client::update`] regularly (e.g. every 1-4 ms in
//! a bot thread, or every frame in a game).

use rand::{Rng, RngCore, SeedableRng};
use std::collections::{BTreeMap, VecDeque};
use std::net::{SocketAddr, UdpSocket};
use std::time::{Duration, Instant};
use tfps_protocol::ack::AckState;
use tfps_protocol::input::{InputBundle, MAX_INPUT_REDUNDANCY};
use tfps_protocol::messages::{ConnState, Message};
use tfps_protocol::packet::{
    self, BodyKind, Challenge, ChallengeResponse, ConnectAccepted, ConnectRejected, ConnectRequest,
    DataHeader, Disconnect, DisconnectReason, PacketType, RejectReason,
};
use tfps_protocol::reliable::{ReliableReceiver, ReliableSender};
use tfps_protocol::snapshot::{self, EntityState, ShotEvent};
use tfps_protocol::{Reader, Writer, MAX_PACKET_SIZE};
use tfps_sim::weapon::{ShotBuffer, WeaponDef, WeaponSet, WeaponState};
use tfps_sim::{simulate_player, CollisionWorld, MapDef, MoveInput, MoveState, Vec3};

const HANDSHAKE_RESEND: Duration = Duration::from_millis(250);
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);
const SNAPSHOT_RING: usize = 32;
const CMD_HISTORY: usize = 256;
const REMOTE_SAMPLE_KEEP: usize = 128;

#[derive(Clone, Debug)]
pub struct ClientConfig {
    pub server: SocketAddr,
    pub name: String,
    pub reconnect_token: u64,
    pub join_token: u64,
    /// Commands repeated in every input packet.
    pub input_redundancy: u8,
    /// Interpolation delay for remote entities, in seconds (min 2 snapshots).
    pub interp_delay: f32,
    /// Simulated packet loss (0..1) in each direction, for testing.
    pub simulated_loss: f32,
    pub connection_timeout: Duration,
}

impl ClientConfig {
    pub fn new(server: SocketAddr, name: &str) -> Self {
        Self {
            server,
            name: name.into(),
            reconnect_token: 0,
            join_token: 0,
            input_redundancy: 8,
            interp_delay: 0.05,
            simulated_loss: 0.0,
            connection_timeout: Duration::from_secs(10),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DisconnectCause {
    Rejected(RejectReason),
    HandshakeTimeout,
    Timeout,
    MapMismatch { server: u64, local: u64 },
    WeaponsMismatch,
    Server(DisconnectReason),
    Local,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClientEvent {
    Connected {
        player_id: u16,
    },
    PlayerJoined {
        player_id: u16,
        team: u8,
        name: String,
    },
    PlayerLeft {
        player_id: u16,
        reason: DisconnectReason,
    },
    PlayerConnState {
        player_id: u16,
        state: ConnState,
    },
    Disconnected(DisconnectCause),
    /// One of our shots hit someone (hit marker).
    HitConfirm {
        victim: u16,
        damage: u8,
        headshot: bool,
        killed: bool,
    },
    /// We were hit.
    Damaged {
        attacker: u16,
        amount: u8,
        health: u8,
    },
    Kill {
        killer: u16,
        victim: u16,
        weapon: u8,
        headshot: bool,
    },
    /// An explosion (effects only), position in millimetres.
    Explosion {
        pos_mm: [i32; 3],
        radius_cm: u16,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Phase {
    Requesting {
        nonce: u64,
        started: Instant,
        last_send: Option<Instant>,
    },
    Responding {
        nonce: u64,
        cookie: u64,
        started: Instant,
        last_send: Option<Instant>,
    },
    Connected,
    Disconnected(DisconnectCause),
}

#[derive(Clone, Copy, Debug)]
struct CmdRecord {
    seq: u32,
    input: MoveInput,
    render_time: u32,
    /// Predicted state after simulating this command.
    after: MoveState,
    weapon_after: WeaponState,
}

/// Interpolated view of a remote player.
#[derive(Clone, Debug)]
pub struct RemotePlayer {
    pub id: u16,
    pub name: String,
    pub team: u8,
    pub conn_state: ConnState,
    /// Whether the entity is present in the latest snapshot.
    pub present: bool,
    samples: VecDeque<(u32, EntityState)>,
    /// Interpolated render state (updated by `Client::update`).
    pub render_pos: Vec3,
    pub render_yaw_deg: f32,
    pub render_pitch_deg: f32,
    pub flags: u8,
    pub health: u8,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct NetStats {
    pub packets_sent: u64,
    pub packets_received: u64,
    pub bytes_sent: u64,
    pub bytes_received: u64,
    pub snapshots_received: u64,
    pub snapshots_dropped_no_baseline: u64,
    pub delta_snapshots: u64,
    /// Reconciliations where the replayed state differed from the prediction.
    pub corrections: u64,
    pub max_correction_m: f32,
    pub last_correction_m: f32,
    pub rtt_ms: f32,
    pub server_input_depth: i8,
    pub time_scale: f32,
}

pub struct Client {
    pub cfg: ClientConfig,
    socket: UdpSocket,
    phase: Phase,
    rng: rand::rngs::StdRng,
    world: CollisionWorld,
    local_map_hash: u64,

    // Session
    pub session_key: u64,
    pub player_id: u16,
    pub tick_rate: u16,
    pub snapshot_interval: u8,
    pub reconnect_token: u64,
    dt: f32,
    ack: AckState,
    rel_tx: ReliableSender,
    rel_rx: ReliableReceiver,
    last_recv: Instant,

    // Prediction
    current_input: MoveInput,
    cmd_seq: u32,
    history: Vec<CmdRecord>,
    pub predicted: MoveState,
    pub predicted_weapon: WeaponState,
    /// Authoritative local state from the newest snapshot.
    pub server_state: MoveState,
    pub server_weapon: WeaponState,
    pub health: u8,
    pub alive: bool,
    pub respawn_in: f32,
    weapons: WeaponSet,
    pub loadout: [u8; 2],
    shot_buf: ShotBuffer,
    /// Shots this client fired (predicted), and shot events of others.
    pub local_shots: u64,
    /// Cells destroyed in this client's collision world (from the server).
    pub destroyed_cells: u64,
    pub remote_shot_events: Vec<ShotEvent>,
    shot_decode: Vec<ShotEvent>,
    have_authoritative: bool,
    last_frame: Option<Instant>,
    tick_accum: f64,
    time_scale: f32,

    // Snapshots
    ring: Vec<(u32, Vec<EntityState>)>,
    ring_cursor: usize,
    latest_snapshot_tick: u32,
    decode_buf: Vec<EntityState>,
    /// Estimated current server tick (fractional), for interpolation.
    server_tick_estimate: f64,

    pub players: BTreeMap<u16, RemotePlayer>,
    events: VecDeque<ClientEvent>,
    pub stats: NetStats,
    send_buf: Vec<u8>,
    recv_buf: Vec<u8>,
}

impl Client {
    /// Creates the client and starts connecting. `map` must be the same map
    /// definition the server runs (verified by hash).
    pub fn connect(cfg: ClientConfig, map: &MapDef, weapons: &WeaponSet) -> std::io::Result<Self> {
        let bind: SocketAddr = if cfg.server.is_ipv4() {
            "0.0.0.0:0".parse().unwrap()
        } else {
            "[::]:0".parse().unwrap()
        };
        let socket = UdpSocket::bind(bind)?;
        // A connected UDP socket: the OS filters other senders, and recv()
        // never parses a source address (macOS can report zero-length source
        // addresses, which makes std's recv_from panic).
        socket.connect(cfg.server)?;
        socket.set_nonblocking(true)?;
        let mut rng = rand::rngs::StdRng::from_os_rng();
        let now = Instant::now();
        let nonce = rng.next_u64();
        Ok(Self {
            reconnect_token: cfg.reconnect_token,
            cfg,
            socket,
            phase: Phase::Requesting {
                nonce,
                started: now,
                last_send: None,
            },
            rng,
            world: CollisionWorld::from_map(map),
            local_map_hash: map.geometry_hash(),
            session_key: 0,
            player_id: 0,
            tick_rate: 64,
            snapshot_interval: 1,
            dt: tfps_sim::tick_dt(64),
            ack: AckState::new(),
            rel_tx: ReliableSender::new(),
            rel_rx: ReliableReceiver::new(),
            last_recv: now,
            current_input: MoveInput::default(),
            cmd_seq: 0,
            history: vec![
                CmdRecord {
                    seq: 0,
                    input: MoveInput::default(),
                    render_time: 0,
                    after: MoveState::default(),
                    weapon_after: WeaponState::default(),
                };
                CMD_HISTORY
            ],
            predicted: MoveState::default(),
            predicted_weapon: WeaponState::default(),
            server_state: MoveState::default(),
            server_weapon: WeaponState::default(),
            health: 100,
            alive: true,
            respawn_in: 0.0,
            weapons: weapons.clone(),
            loadout: weapons.default_loadout,
            shot_buf: ShotBuffer::default(),
            local_shots: 0,
            destroyed_cells: 0,
            remote_shot_events: Vec::new(),
            shot_decode: Vec::new(),
            have_authoritative: false,
            last_frame: None,
            tick_accum: 0.0,
            time_scale: 1.0,
            ring: (0..SNAPSHOT_RING).map(|_| (0u32, Vec::new())).collect(),
            ring_cursor: 0,
            latest_snapshot_tick: 0,
            decode_buf: Vec::with_capacity(64),
            server_tick_estimate: 0.0,
            players: BTreeMap::new(),
            events: VecDeque::new(),
            stats: NetStats {
                time_scale: 1.0,
                ..Default::default()
            },
            send_buf: Vec::with_capacity(MAX_PACKET_SIZE),
            recv_buf: vec![0u8; 2048],
        })
    }

    pub fn is_connected(&self) -> bool {
        self.phase == Phase::Connected
    }

    pub fn disconnect_cause(&self) -> Option<&DisconnectCause> {
        match &self.phase {
            Phase::Disconnected(c) => Some(c),
            _ => None,
        }
    }

    pub fn local_addr(&self) -> SocketAddr {
        self.socket.local_addr().expect("bound")
    }

    pub fn poll_event(&mut self) -> Option<ClientEvent> {
        self.events.pop_front()
    }

    /// Sets the input that will be sampled at the next simulation ticks.
    pub fn set_input(&mut self, input: MoveInput) {
        self.current_input = input;
    }

    /// This client's collision world (with the destruction it was told about).
    pub fn world(&self) -> &CollisionWorld {
        &self.world
    }

    /// The input currently being sampled.
    pub fn current_input(&self) -> MoveInput {
        self.current_input
    }

    pub fn command_seq(&self) -> u32 {
        self.cmd_seq
    }

    pub fn latest_snapshot_tick(&self) -> u32 {
        self.latest_snapshot_tick
    }

    /// Graceful disconnect.
    pub fn disconnect(&mut self) {
        if self.phase == Phase::Connected {
            self.send_buf.clear();
            Disconnect {
                session_key: self.session_key,
                reason: DisconnectReason::ClientQuit,
            }
            .encode(&mut self.send_buf);
            for _ in 0..3 {
                let _ = self.socket.send(&self.send_buf);
            }
        }
        self.set_disconnected(DisconnectCause::Local);
    }

    fn set_disconnected(&mut self, cause: DisconnectCause) {
        if !matches!(self.phase, Phase::Disconnected(_)) {
            self.phase = Phase::Disconnected(cause.clone());
            self.events.push_back(ClientEvent::Disconnected(cause));
        }
    }

    fn lose(&mut self) -> bool {
        self.cfg.simulated_loss > 0.0 && self.rng.random::<f32>() < self.cfg.simulated_loss
    }

    fn send_current(&mut self) {
        if self.lose() {
            return;
        }
        if let Ok(n) = self.socket.send(&self.send_buf) {
            self.stats.packets_sent += 1;
            self.stats.bytes_sent += n as u64;
        }
    }

    /// Pumps the network and advances the simulation. Call frequently.
    pub fn update(&mut self, now: Instant) {
        self.receive(now);
        match self.phase.clone() {
            Phase::Requesting {
                nonce,
                started,
                last_send,
            } => {
                if now.duration_since(started) > HANDSHAKE_TIMEOUT {
                    self.set_disconnected(DisconnectCause::HandshakeTimeout);
                } else if last_send.is_none_or(|t| now.duration_since(t) >= HANDSHAKE_RESEND) {
                    self.send_buf.clear();
                    ConnectRequest {
                        client_nonce: nonce,
                    }
                    .encode(&mut self.send_buf);
                    self.send_current();
                    self.phase = Phase::Requesting {
                        nonce,
                        started,
                        last_send: Some(now),
                    };
                }
            }
            Phase::Responding {
                nonce,
                cookie,
                started,
                last_send,
            } => {
                if now.duration_since(started) > HANDSHAKE_TIMEOUT {
                    self.set_disconnected(DisconnectCause::HandshakeTimeout);
                } else if last_send.is_none_or(|t| now.duration_since(t) >= HANDSHAKE_RESEND) {
                    self.send_buf.clear();
                    ChallengeResponse {
                        client_nonce: nonce,
                        cookie,
                        reconnect_token: self.cfg.reconnect_token,
                        join_token: self.cfg.join_token,
                        name: self.cfg.name.clone(),
                    }
                    .encode(&mut self.send_buf);
                    self.send_current();
                    self.phase = Phase::Responding {
                        nonce,
                        cookie,
                        started,
                        last_send: Some(now),
                    };
                }
            }
            Phase::Connected => {
                if now.duration_since(self.last_recv) > self.cfg.connection_timeout {
                    self.set_disconnected(DisconnectCause::Timeout);
                    return;
                }
                self.advance_ticks(now);
                self.update_interpolation(now);
            }
            Phase::Disconnected(_) => {}
        }
    }

    fn advance_ticks(&mut self, now: Instant) {
        let elapsed = match self.last_frame {
            Some(t) => now.duration_since(t).as_secs_f64(),
            None => self.dt as f64, // first frame: run one tick immediately
        };
        self.last_frame = Some(now);
        self.server_tick_estimate += elapsed * self.tick_rate as f64;
        self.tick_accum += elapsed * self.time_scale as f64;
        let dt = self.dt as f64;
        let mut ran = 0;
        while self.tick_accum >= dt && ran < 8 {
            self.tick_accum -= dt;
            self.run_tick(now);
            ran += 1;
        }
        if ran == 8 {
            self.tick_accum = 0.0; // hitch: don't spiral
        }
    }

    /// One client simulation tick: sample input, predict, send.
    /// Loadout weapon definitions (valid once connected).
    fn loadout_defs(&self) -> Option<[&WeaponDef; 2]> {
        Some([
            self.weapons.get(self.loadout[0])?,
            self.weapons.get(self.loadout[1])?,
        ])
    }

    /// One predicted step (no-op while dead). Returns the number of bullets fired.
    fn step(&mut self, ms: &mut MoveState, ws: &mut WeaponState, input: &MoveInput) -> usize {
        if !self.alive {
            return 0;
        }
        // Field-level borrows: weapon definitions (shared) and shot buffer (mutable).
        let (Some(p), Some(s)) = (
            self.weapons.get(self.loadout[0]),
            self.weapons.get(self.loadout[1]),
        ) else {
            return 0;
        };
        self.shot_buf.clear();
        simulate_player(
            ms,
            ws,
            input,
            self.dt,
            &self.world,
            &[p, s],
            &mut self.shot_buf,
        );
        self.shot_buf.len
    }

    fn run_tick(&mut self, now: Instant) {
        self.cmd_seq += 1;
        let input = self.current_input;
        let (mut ms, mut ws) = (self.predicted, self.predicted_weapon);
        self.local_shots += self.step(&mut ms, &mut ws, &input) as u64;
        self.predicted = ms;
        self.predicted_weapon = ws;
        let rt = self.render_tick();
        self.history[self.cmd_seq as usize % CMD_HISTORY] = CmdRecord {
            seq: self.cmd_seq,
            input,
            render_time: if rt > 0.0 { (rt * 256.0) as u32 } else { 0 },
            after: self.predicted,
            weapon_after: self.predicted_weapon,
        };
        self.send_input(now);
    }

    fn send_input(&mut self, now: Instant) {
        let count = (self.cfg.input_redundancy as u32)
            .clamp(1, MAX_INPUT_REDUNDANCY as u32)
            .min(self.cmd_seq);
        let mut bundle = InputBundle {
            ack_snapshot_tick: self.latest_snapshot_tick,
            newest_seq: self.cmd_seq,
            count: count as u8,
            ..Default::default()
        };
        for i in 0..count {
            let rec = &self.history[(self.cmd_seq - i) as usize % CMD_HISTORY];
            bundle.cmds[i as usize] = rec.input;
            bundle.render_times[i as usize] = rec.render_time;
        }
        self.send_buf.clear();
        let mut w = Writer::new(&mut self.send_buf);
        let (seq, ack, ack_bits) = self.ack.next_outgoing(now);
        DataHeader {
            session_key: self.session_key,
            seq,
            ack,
            ack_bits,
        }
        .encode(&mut w);
        self.rel_tx
            .write(&mut w, seq, now, Duration::from_millis(100), 200);
        w.u8(BodyKind::Input as u8);
        bundle.encode(&mut w);
        self.send_current();
    }

    fn receive(&mut self, now: Instant) {
        loop {
            let n = match self.socket.recv(&mut self.recv_buf) {
                Ok(v) => v,
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(_) => break, // e.g. ICMP unreachable on Windows; keep going next frame
            };
            if self.lose() {
                continue;
            }
            self.stats.packets_received += 1;
            self.stats.bytes_received += n as u64;
            let data = std::mem::take(&mut self.recv_buf);
            let _ = self.process(&data[..n], now);
            self.recv_buf = data;
        }
    }

    fn process(&mut self, data: &[u8], now: Instant) -> Result<(), tfps_protocol::DecodeError> {
        let (hdr, mut r) = packet::read_header(data)?;
        match (hdr.ty, self.phase.clone()) {
            (PacketType::Challenge, Phase::Requesting { nonce, started, .. }) => {
                let c = Challenge::decode(&mut r)?;
                if c.client_nonce == nonce {
                    self.phase = Phase::Responding {
                        nonce,
                        cookie: c.cookie,
                        started,
                        last_send: None,
                    };
                    self.update(now); // respond immediately
                }
            }
            (PacketType::ConnectAccepted, Phase::Responding { nonce, .. }) => {
                let a = ConnectAccepted::decode(&mut r)?;
                if a.client_nonce != nonce {
                    return Ok(());
                }
                let weapons_ok = a.weapons_hash == self.weapons.hash()
                    && self.weapons.get(a.loadout[0]).is_some()
                    && self.weapons.get(a.loadout[1]).is_some();
                if a.map_hash != self.local_map_hash || !weapons_ok {
                    self.send_buf.clear();
                    Disconnect {
                        session_key: a.session_key,
                        reason: DisconnectReason::ProtocolError,
                    }
                    .encode(&mut self.send_buf);
                    let _ = self.socket.send(&self.send_buf);
                    self.set_disconnected(if weapons_ok {
                        DisconnectCause::MapMismatch {
                            server: a.map_hash,
                            local: self.local_map_hash,
                        }
                    } else {
                        DisconnectCause::WeaponsMismatch
                    });
                    return Ok(());
                }
                self.loadout = a.loadout;
                if let Some(defs) = self.loadout_defs() {
                    self.predicted_weapon = WeaponState::new(&defs);
                }
                self.session_key = a.session_key;
                self.player_id = a.player_id;
                self.tick_rate = a.tick_rate;
                self.snapshot_interval = a.snapshot_interval.max(1);
                self.dt = tfps_sim::tick_dt(a.tick_rate);
                self.reconnect_token = a.reconnect_token;
                self.server_tick_estimate = a.server_tick as f64;
                self.last_recv = now;
                self.phase = Phase::Connected;
                self.events.push_back(ClientEvent::Connected {
                    player_id: a.player_id,
                });
            }
            (
                PacketType::ConnectRejected,
                Phase::Requesting { nonce, .. } | Phase::Responding { nonce, .. },
            ) => {
                let rj = ConnectRejected::decode(&mut r)?;
                if rj.client_nonce == nonce {
                    self.set_disconnected(DisconnectCause::Rejected(rj.reason));
                }
            }
            (PacketType::Data, Phase::Connected) => self.process_data(&mut r, now)?,
            (PacketType::Disconnect, Phase::Connected) => {
                let d = Disconnect::decode(&mut r)?;
                if d.session_key == self.session_key {
                    self.set_disconnected(DisconnectCause::Server(d.reason));
                }
            }
            _ => {}
        }
        Ok(())
    }

    fn process_data(
        &mut self,
        r: &mut Reader,
        now: Instant,
    ) -> Result<(), tfps_protocol::DecodeError> {
        let dh = DataHeader::decode(r)?;
        if dh.session_key != self.session_key {
            return Ok(());
        }
        let rel_tx = &mut self.rel_tx;
        if !self.ack.on_incoming(dh.seq, dh.ack, dh.ack_bits, now, |s| {
            rel_tx.on_packet_acked(s)
        }) {
            return Ok(());
        }
        self.last_recv = now;
        self.stats.rtt_ms = self.ack.rtt.as_secs_f32() * 1000.0;
        self.rel_rx.read(r)?;
        while let Some((ty, payload)) = self.rel_rx.pop() {
            if let Ok(m) = Message::decode(ty, &payload) {
                self.on_message(m);
            }
        }
        if BodyKind::from_u8(r.u8()?)? == BodyKind::Snapshot {
            self.on_snapshot(r)?;
        }
        Ok(())
    }

    fn on_message(&mut self, m: Message) {
        match m {
            Message::PlayerJoined {
                player_id,
                team,
                name,
            } => {
                if player_id != self.player_id {
                    let rp = self
                        .players
                        .entry(player_id)
                        .or_insert_with(|| RemotePlayer {
                            id: player_id,
                            name: String::new(),
                            team,
                            conn_state: ConnState::Connected,
                            present: false,
                            samples: VecDeque::new(),
                            render_pos: Vec3::ZERO,
                            render_yaw_deg: 0.0,
                            render_pitch_deg: 0.0,
                            flags: 0,
                            health: 100,
                        });
                    rp.name = name.clone();
                    rp.team = team;
                    rp.conn_state = ConnState::Connected;
                }
                self.events.push_back(ClientEvent::PlayerJoined {
                    player_id,
                    team,
                    name,
                });
            }
            Message::PlayerLeft { player_id, reason } => {
                self.players.remove(&player_id);
                self.events.push_back(ClientEvent::PlayerLeft {
                    player_id,
                    reason: DisconnectReason::from_u8(reason),
                });
            }
            Message::PlayerConnState { player_id, state } => {
                if let Some(p) = self.players.get_mut(&player_id) {
                    p.conn_state = state;
                }
                self.events
                    .push_back(ClientEvent::PlayerConnState { player_id, state });
            }
            Message::HitConfirm {
                victim,
                damage,
                headshot,
                killed,
            } => {
                self.events.push_back(ClientEvent::HitConfirm {
                    victim,
                    damage,
                    headshot,
                    killed,
                });
            }
            Message::Damaged {
                attacker,
                amount,
                health,
                ..
            } => {
                self.events.push_back(ClientEvent::Damaged {
                    attacker,
                    amount,
                    health,
                });
            }
            Message::CellsDestroyed { cells } => {
                for c in cells {
                    if self.world.destroy_cell(c) {
                        self.destroyed_cells += 1;
                    }
                }
            }
            Message::Explosion { pos_mm, radius_cm } => {
                self.events
                    .push_back(ClientEvent::Explosion { pos_mm, radius_cm });
            }
            Message::Kill {
                killer,
                victim,
                weapon,
                headshot,
            } => {
                self.events.push_back(ClientEvent::Kill {
                    killer,
                    victim,
                    weapon,
                    headshot,
                });
            }
        }
    }

    fn on_snapshot(&mut self, r: &mut Reader) -> Result<(), tfps_protocol::DecodeError> {
        // Look up the baseline before decoding.
        let rest = r.clone().take(r.remaining())?;
        let base_tick = snapshot::peek_baseline_tick(rest)?;
        let base_idx = if base_tick != 0 {
            self.ring.iter().position(|(t, _)| *t == base_tick)
        } else {
            None
        };
        if base_tick != 0 && base_idx.is_none() {
            self.stats.snapshots_dropped_no_baseline += 1;
            return Ok(());
        }
        let mut out = std::mem::take(&mut self.decode_buf);
        let mut shots = std::mem::take(&mut self.shot_decode);
        let h = snapshot::decode(
            r,
            base_idx.map(|i| self.ring[i].1.as_slice()),
            &mut out,
            &mut shots,
        )?;
        let own = self.player_id;
        self.remote_shot_events
            .extend(shots.iter().filter(|e| e.shooter != own));
        if self.remote_shot_events.len() > 1024 {
            let excess = self.remote_shot_events.len() - 1024;
            self.remote_shot_events.drain(..excess);
        }
        self.shot_decode = shots;
        self.stats.snapshots_received += 1;
        if h.baseline_tick != 0 {
            self.stats.delta_snapshots += 1;
        }
        if h.server_tick <= self.latest_snapshot_tick {
            // Out of order: too old to be useful.
            self.decode_buf = out;
            return Ok(());
        }
        // Store for future delta decoding.
        let slot = &mut self.ring[self.ring_cursor];
        slot.0 = h.server_tick;
        slot.1.clear();
        slot.1.extend_from_slice(&out);
        self.ring_cursor = (self.ring_cursor + 1) % SNAPSHOT_RING;
        self.latest_snapshot_tick = h.server_tick;

        // Server time estimate: never behind the newest snapshot.
        if (h.server_tick as f64) > self.server_tick_estimate {
            self.server_tick_estimate = h.server_tick as f64;
        }

        // Remote entities.
        for p in self.players.values_mut() {
            p.present = false;
        }
        for e in &out {
            if let Some(p) = self.players.get_mut(&e.id) {
                p.present = true;
                p.samples.push_back((h.server_tick, *e));
                while p.samples.len() > REMOTE_SAMPLE_KEEP {
                    p.samples.pop_front();
                }
            }
        }
        self.decode_buf = out;

        // Local player reconciliation.
        self.server_state = h.local;
        self.server_weapon = h.weapon;
        self.health = h.local_health;
        self.alive = h.alive;
        self.respawn_in = h.respawn_in;
        self.stats.server_input_depth = h.input_buffer_depth;
        self.reconcile(h.last_cmd_seq, h.local, h.weapon);
        self.adjust_time_scale(h.input_buffer_depth);
        Ok(())
    }

    fn reconcile(&mut self, acked_seq: u32, server: MoveState, server_weapon: WeaponState) {
        if acked_seq > self.cmd_seq {
            return;
        }
        if acked_seq == 0 || !self.alive {
            // No command of ours simulated yet (`server` is the spawn state),
            // or dead: adopt the server state and re-predict pending commands
            // so stored predictions are not based on stale state.
            let (mut ms, mut ws) = (server, server_weapon);
            for seq in acked_seq + 1..=self.cmd_seq {
                let rec = self.history[seq as usize % CMD_HISTORY];
                if rec.seq != seq {
                    continue;
                }
                self.step(&mut ms, &mut ws, &rec.input);
                let r = &mut self.history[seq as usize % CMD_HISTORY];
                r.after = ms;
                r.weapon_after = ws;
            }
            self.predicted = ms;
            self.predicted_weapon = ws;
            self.have_authoritative = true;
            return;
        }
        let rec = self.history[acked_seq as usize % CMD_HISTORY];
        // Before the first authoritative state the client cannot know its
        // spawn, so that first difference is not a misprediction.
        let had_prediction = rec.seq == acked_seq && self.have_authoritative;
        self.have_authoritative = true;
        let (mut ms, mut ws) = (server, server_weapon);
        let mut seq = acked_seq + 1;
        while seq <= self.cmd_seq {
            let r = self.history[seq as usize % CMD_HISTORY];
            if r.seq != seq {
                break;
            }
            self.step(&mut ms, &mut ws, &r.input);
            let r = &mut self.history[seq as usize % CMD_HISTORY];
            r.after = ms;
            r.weapon_after = ws;
            seq += 1;
        }
        if had_prediction {
            let err = rec.after.pos.distance(&server.pos);
            self.stats.last_correction_m = err;
            if err > 1e-4 || rec.after.vel != server.vel || rec.weapon_after != server_weapon {
                self.stats.corrections += 1;
                self.stats.max_correction_m = self.stats.max_correction_m.max(err);
            }
        }
        self.predicted = ms;
        self.predicted_weapon = ws;
    }

    /// Client time dilation: keep 1-3 commands buffered at the server.
    fn adjust_time_scale(&mut self, depth: i8) {
        self.time_scale = match depth {
            i8::MIN..=-1 => 1.06,
            0 => 1.03,
            1..=3 => 1.0,
            4..=6 => 0.97,
            _ => 0.92,
        };
        self.stats.time_scale = self.time_scale;
    }

    /// Server tick that remote entities are rendered at.
    pub fn render_tick(&self) -> f64 {
        let delay_ticks = (self.cfg.interp_delay as f64 * self.tick_rate as f64)
            .max(2.0 * self.snapshot_interval as f64);
        self.server_tick_estimate - delay_ticks
    }

    fn update_interpolation(&mut self, _now: Instant) {
        let rt = self.render_tick();
        for p in self.players.values_mut() {
            if p.samples.is_empty() {
                continue;
            }
            // Find samples a <= rt < b.
            let mut a = p.samples[0];
            let mut b = a;
            for s in p.samples.iter() {
                if (s.0 as f64) <= rt {
                    a = *s;
                    b = *s;
                } else {
                    b = *s;
                    break;
                }
            }
            let t = if b.0 > a.0 {
                ((rt - a.0 as f64) / (b.0 - a.0) as f64).clamp(0.0, 1.0) as f32
            } else {
                0.0
            };
            let pa = a.1.position();
            let pb = b.1.position();
            p.render_pos = Vec3::new(
                pa.x + (pb.x - pa.x) * t,
                pa.y + (pb.y - pa.y) * t,
                pa.z + (pb.z - pa.z) * t,
            );
            let ya = tfps_sim::math::dequantize_yaw_deg(a.1.yaw_q);
            let yb = tfps_sim::math::dequantize_yaw_deg(b.1.yaw_q);
            let mut dy = yb - ya;
            if dy > 180.0 {
                dy -= 360.0;
            } else if dy < -180.0 {
                dy += 360.0;
            }
            p.render_yaw_deg = (ya + dy * t).rem_euclid(360.0);
            let pa_ = tfps_sim::math::dequantize_pitch_deg(a.1.pitch_q);
            let pb_ = tfps_sim::math::dequantize_pitch_deg(b.1.pitch_q);
            p.render_pitch_deg = pa_ + (pb_ - pa_) * t;
            p.flags = b.1.flags;
            p.health = b.1.health;
        }
    }
}

/// Convenience: drive a client until `pred` is true or `timeout` passes.
pub fn pump_until(
    client: &mut Client,
    timeout: Duration,
    mut pred: impl FnMut(&mut Client) -> bool,
) -> bool {
    let end = Instant::now() + timeout;
    while Instant::now() < end {
        client.update(Instant::now());
        if pred(client) {
            return true;
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    false
}
