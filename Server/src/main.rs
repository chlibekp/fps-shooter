use clap::Parser;
use std::path::PathBuf;
use std::time::Duration;
use tfps_server::config::ServerConfig;
use tracing_subscriber::EnvFilter;

/// TacticalFPS authoritative dedicated server.
#[derive(Parser, Debug)]
#[command(version, about)]
struct Args {
    /// Address to bind (UDP).
    #[arg(long, default_value = "0.0.0.0:7777")]
    bind: std::net::SocketAddr,
    /// Simulation ticks per second (60-128 recommended).
    #[arg(long, default_value_t = 64)]
    tick_rate: u16,
    /// Send a snapshot every N ticks.
    #[arg(long, default_value_t = 1)]
    snapshot_interval: u8,
    #[arg(long, default_value_t = 10)]
    max_players: u16,
    /// Map gameplay definition (JSON).
    #[arg(long, default_value = "Shared/Definitions/maps/test_arena.json")]
    map: PathBuf,
    /// Weapon definitions (JSON, must match the clients' file).
    #[arg(long, default_value = "Shared/Definitions/weapons.json")]
    weapons: PathBuf,
    /// Gadget definitions (JSON).
    #[arg(long, default_value = "Shared/Definitions/gadgets.json")]
    gadgets: PathBuf,
    /// Require this join token (hex u64) from clients.
    #[arg(long, value_parser = parse_hex_u64)]
    join_token: Option<u64>,
    /// Seconds between statistics log lines.
    #[arg(long, default_value_t = 10)]
    stats_interval: u64,
}

fn parse_hex_u64(s: &str) -> Result<u64, String> {
    u64::from_str_radix(s.trim_start_matches("0x"), 16).map_err(|e| e.to_string())
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .init();
    let args = Args::parse();
    let cfg = ServerConfig {
        bind: args.bind,
        tick_rate: args.tick_rate,
        snapshot_interval: args.snapshot_interval,
        max_players: args.max_players,
        map_path: args.map,
        weapons_path: args.weapons,
        gadgets_path: args.gadgets,
        join_token: args.join_token,
        stats_interval: Duration::from_secs(args.stats_interval.max(1)),
        ..Default::default()
    };
    let handle = tfps_server::start(cfg)?;
    let flag = handle.shutdown_flag();
    // Ctrl-C / SIGTERM -> graceful shutdown (clients get a Disconnect packet).
    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()?;
    rt.block_on(async {
        #[cfg(unix)]
        {
            let mut term =
                tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
                    .expect("sigterm");
            tokio::select! {
                _ = tokio::signal::ctrl_c() => {},
                _ = term.recv() => {},
            }
        }
        #[cfg(not(unix))]
        {
            let _ = tokio::signal::ctrl_c().await;
        }
    });
    tracing::info!("shutting down");
    flag.store(true, std::sync::atomic::Ordering::SeqCst);
    handle.wait();
    Ok(())
}
