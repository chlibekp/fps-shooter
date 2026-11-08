//! TacticalFPS lobby service.
//!
//! Players create a lobby (and get a 6-character invite code), friends join
//! with the code, the host picks the map, mode, team size (1v1..5v5), rounds
//! and timers, everyone readies up and the host starts the match. The
//! service then launches a game server with a fresh join token and gives
//! the members its address and token. See `http.rs` for the API.

pub mod http;
pub mod service;
pub mod state;

use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};

pub use service::{LobbyConfig, Service};

pub struct LobbyHandle {
    pub local_addr: SocketAddr,
    pub service: Arc<Service>,
    runtime: Option<tokio::runtime::Runtime>,
}

impl LobbyHandle {
    pub fn shutdown(mut self) {
        if let Some(rt) = self.runtime.take() {
            rt.shutdown_timeout(Duration::from_millis(500));
        }
    }

    /// Blocks until the process is interrupted (Ctrl-C).
    pub fn wait(mut self) {
        if let Some(rt) = self.runtime.take() {
            rt.block_on(async {
                let _ = tokio::signal::ctrl_c().await;
            });
            rt.shutdown_timeout(Duration::from_millis(500));
        }
    }
}

impl Drop for LobbyHandle {
    fn drop(&mut self) {
        if let Some(rt) = self.runtime.take() {
            rt.shutdown_timeout(Duration::from_millis(500));
        }
    }
}

/// Starts the lobby service (HTTP API + housekeeping) and returns immediately.
pub fn start(cfg: LobbyConfig) -> anyhow::Result<LobbyHandle> {
    let service = Service::new(cfg.clone())?;
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .thread_name("tfps-lobby")
        .enable_all()
        .build()?;
    let listener = runtime.block_on(tokio::net::TcpListener::bind(cfg.bind))?;
    let local_addr = listener.local_addr()?;
    runtime.spawn(http::serve(listener, service.clone()));
    let svc = service.clone();
    runtime.spawn(async move {
        let mut iv = tokio::time::interval(Duration::from_millis(500));
        loop {
            iv.tick().await;
            let s = svc.clone();
            let _ = tokio::task::spawn_blocking(move || s.tick(Instant::now())).await;
        }
    });
    tracing::info!(%local_addr, "lobby service started");
    Ok(LobbyHandle {
        local_addr,
        service,
        runtime: Some(runtime),
    })
}
