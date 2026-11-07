use std::net::SocketAddr;
use std::path::PathBuf;
use std::time::Duration;

#[derive(Clone, Debug)]
pub struct ServerConfig {
    pub bind: SocketAddr,
    /// Simulation ticks per second (60..=128 recommended; 20..=240 allowed).
    pub tick_rate: u16,
    /// Send a snapshot every N ticks.
    pub snapshot_interval: u8,
    pub max_players: u16,
    pub map_path: PathBuf,
    /// If set, clients must present this join token (lobby system).
    pub join_token: Option<u64>,
    /// No packets for this long -> player marked as interrupted.
    pub interrupt_after: Duration,
    /// No packets for this long -> connection dropped (reconnect possible).
    pub timeout_after: Duration,
    /// How long a dropped player's slot is kept for reconnection.
    pub reconnect_window: Duration,
    /// How often tick/network statistics are logged.
    pub stats_interval: Duration,
}

impl Default for ServerConfig {
    fn default() -> Self {
        Self {
            bind: "0.0.0.0:7777".parse().unwrap(),
            tick_rate: 64,
            snapshot_interval: 1,
            max_players: 10,
            map_path: PathBuf::from("Shared/Definitions/maps/test_arena.json"),
            join_token: None,
            interrupt_after: Duration::from_secs(2),
            timeout_after: Duration::from_secs(10),
            reconnect_window: Duration::from_secs(60),
            stats_interval: Duration::from_secs(10),
        }
    }
}

impl ServerConfig {
    pub fn validate(&self) -> Result<(), String> {
        if !(20..=240).contains(&self.tick_rate) {
            return Err(format!(
                "tick rate {} out of range 20..=240",
                self.tick_rate
            ));
        }
        if self.snapshot_interval == 0 {
            return Err("snapshot interval must be >= 1".into());
        }
        if self.max_players == 0 || self.max_players > 64 {
            return Err("max players must be 1..=64".into());
        }
        Ok(())
    }
}
