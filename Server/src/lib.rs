//! TacticalFPS authoritative dedicated server.
//!
//! Threading model:
//! * a Tokio runtime runs the UDP receive task (and signal handling);
//! * a dedicated OS thread runs the fixed-timestep simulation
//!   ([`server::ServerCore`]). Received datagrams reach it through a bounded
//!   channel; it sends with a non-blocking socket, so it never blocks on I/O.

pub mod config;
pub mod connection;
pub mod game;
pub mod server;
pub mod stats;

use anyhow::Context;
use config::ServerConfig;
use stats::Stats;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, RecvTimeoutError, SyncSender, TrySendError};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};
use tfps_sim::MapDef;
use tracing::{info, warn};

/// Capacity of the network -> simulation inbox. When full, packets are
/// dropped (load shedding) instead of growing memory unboundedly.
const INBOX_CAPACITY: usize = 16 * 1024;
const RECV_BUF: usize = 2048;

struct Datagram {
    addr: SocketAddr,
    len: usize,
    data: Vec<u8>,
}

pub struct ServerHandle {
    pub local_addr: SocketAddr,
    pub stats: Arc<Stats>,
    shutdown: Arc<AtomicBool>,
    sim_thread: Option<JoinHandle<()>>,
    runtime: Option<tokio::runtime::Runtime>,
}

impl ServerHandle {
    /// Stops the server, notifying connected clients.
    pub fn shutdown(mut self) {
        self.stop();
    }

    pub fn is_running(&self) -> bool {
        self.sim_thread.as_ref().is_some_and(|t| !t.is_finished())
    }

    /// Blocks until the simulation thread exits (e.g. after Ctrl-C).
    pub fn wait(mut self) {
        if let Some(t) = self.sim_thread.take() {
            let _ = t.join();
        }
        if let Some(rt) = self.runtime.take() {
            rt.shutdown_timeout(Duration::from_millis(200));
        }
    }

    pub fn shutdown_flag(&self) -> Arc<AtomicBool> {
        self.shutdown.clone()
    }

    fn stop(&mut self) {
        self.shutdown.store(true, Ordering::SeqCst);
        if let Some(t) = self.sim_thread.take() {
            let _ = t.join();
        }
        if let Some(rt) = self.runtime.take() {
            rt.shutdown_timeout(Duration::from_millis(200));
        }
    }
}

impl Drop for ServerHandle {
    fn drop(&mut self) {
        self.stop();
    }
}

/// Starts a server and returns immediately.
pub fn start(cfg: ServerConfig) -> anyhow::Result<ServerHandle> {
    cfg.validate().map_err(anyhow::Error::msg)?;
    let map = MapDef::load(&cfg.map_path).with_context(|| format!("loading map {}", cfg.map_path.display()))?;
    start_with_map(cfg, map)
}

pub fn start_with_map(cfg: ServerConfig, map: MapDef) -> anyhow::Result<ServerHandle> {
    cfg.validate().map_err(anyhow::Error::msg)?;
    let std_socket = std::net::UdpSocket::bind(cfg.bind).with_context(|| format!("binding {}", cfg.bind))?;
    std_socket.set_nonblocking(true)?;
    let local_addr = std_socket.local_addr()?;
    let send_socket = std_socket.try_clone()?;

    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(1)
        .thread_name("tfps-net")
        .enable_io()
        .enable_time()
        .build()?;

    let stats = Arc::new(Stats::default());
    let shutdown = Arc::new(AtomicBool::new(false));
    let (inbox_tx, inbox_rx) = sync_channel::<Datagram>(INBOX_CAPACITY);
    // Buffers flow back from the simulation thread for reuse (no per-packet
    // allocation in steady state).
    let (pool_tx, pool_rx) = sync_channel::<Vec<u8>>(INBOX_CAPACITY);

    {
        let stats = stats.clone();
        let shutdown = shutdown.clone();
        let _guard = runtime.enter();
        let socket = tokio::net::UdpSocket::from_std(std_socket)?;
        runtime.spawn(recv_loop(socket, inbox_tx, pool_rx, stats, shutdown));
    }

    info!(
        %local_addr,
        tick_rate = cfg.tick_rate,
        snapshot_interval = cfg.snapshot_interval,
        map = %map.name,
        solids = map.solids.len(),
        map_hash = format!("{:016x}", map.geometry_hash()),
        "server started"
    );

    let sim_thread = {
        let stats = stats.clone();
        let shutdown = shutdown.clone();
        std::thread::Builder::new().name("tfps-sim".into()).spawn(move || {
            let core = server::ServerCore::new(cfg, map, send_socket, stats.clone());
            sim_loop(core, inbox_rx, pool_tx, stats, shutdown);
        })?
    };

    Ok(ServerHandle { local_addr, stats, shutdown, sim_thread: Some(sim_thread), runtime: Some(runtime) })
}

async fn recv_loop(
    socket: tokio::net::UdpSocket,
    inbox: SyncSender<Datagram>,
    pool: Receiver<Vec<u8>>,
    stats: Arc<Stats>,
    shutdown: Arc<AtomicBool>,
) {
    let mut buf = vec![0u8; RECV_BUF];
    while !shutdown.load(Ordering::Relaxed) {
        let res = tokio::time::timeout(Duration::from_millis(100), socket.recv_from(&mut buf)).await;
        let (n, addr) = match res {
            Err(_) => continue, // timeout: re-check shutdown flag
            Ok(Ok(v)) => v,
            Ok(Err(e)) => {
                // ICMP port unreachable etc. surface as errors on some OSes.
                tracing::debug!("recv error: {e}");
                continue;
            }
        };
        if n > tfps_protocol::MAX_PACKET_SIZE {
            stats.packets_invalid.fetch_add(1, Ordering::Relaxed);
            continue;
        }
        let mut data = pool.try_recv().unwrap_or_else(|_| Vec::with_capacity(RECV_BUF));
        data.clear();
        data.extend_from_slice(&buf[..n]);
        match inbox.try_send(Datagram { addr, len: n, data }) {
            Ok(()) => {}
            Err(TrySendError::Full(_)) => {
                stats.inbox_dropped.fetch_add(1, Ordering::Relaxed);
            }
            Err(TrySendError::Disconnected(_)) => break,
        }
    }
}

fn sim_loop(
    mut core: server::ServerCore,
    inbox: Receiver<Datagram>,
    pool: SyncSender<Vec<u8>>,
    stats: Arc<Stats>,
    shutdown: Arc<AtomicBool>,
) {
    let period = Duration::from_secs_f64(1.0 / core.tick_rate() as f64);
    let mut next_tick = Instant::now() + period;
    loop {
        if shutdown.load(Ordering::Relaxed) {
            core.shutdown();
            info!("server stopped");
            return;
        }
        // Handle packets as they arrive until the next tick is due.
        loop {
            let now = Instant::now();
            if now >= next_tick {
                break;
            }
            match inbox.recv_timeout(next_tick - now) {
                Ok(d) => {
                    core.handle_packet(d.addr, &d.data[..d.len], Instant::now());
                    let _ = pool.try_send(d.data);
                }
                Err(RecvTimeoutError::Timeout) => break,
                Err(RecvTimeoutError::Disconnected) => {
                    std::thread::sleep(next_tick.saturating_duration_since(Instant::now()));
                    break;
                }
            }
        }
        let t0 = Instant::now();
        core.tick(t0);
        let work = t0.elapsed();
        stats.record_tick(work.as_micros() as u64);
        core.maybe_log_stats(t0);

        next_tick += period;
        let now = Instant::now();
        if now > next_tick + period * 4 {
            // Fell far behind (debugger pause, overloaded host): resync
            // instead of running a burst of catch-up ticks.
            stats.late_ticks.fetch_add(1, Ordering::Relaxed);
            warn!(behind_ms = (now - next_tick).as_millis() as u64, "simulation fell behind, resyncing");
            next_tick = now + period;
        }
    }
}
