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
    /// Map gameplay definition: a JSON path, or a map name from
    /// Shared/Definitions/maps (test_arena, harbor, villa, consulate).
    #[arg(long, default_value = "test_arena")]
    map: String,
    /// Weapon definitions (JSON, must match the clients' file).
    #[arg(long, default_value = "Shared/Definitions/weapons.json")]
    weapons: PathBuf,
    /// Gadget definitions (JSON).
    #[arg(long, default_value = "Shared/Definitions/gadgets.json")]
    gadgets: PathBuf,
    /// Game mode: deathmatch, elimination or defuse.
    #[arg(long, default_value = "deathmatch")]
    mode: String,
    /// Round modes: rounds a squad must win.
    #[arg(long, default_value_t = 4)]
    rounds_to_win: u8,
    /// Round modes: action phase length (s).
    #[arg(long, default_value_t = 180.0)]
    round_time: f32,
    /// Round modes: preparation phase length (s).
    #[arg(long, default_value_t = 30.0)]
    prep_time: f32,
    /// Deathmatch: kills to win.
    #[arg(long, default_value_t = 50)]
    score_limit: u8,
    /// Deathmatch: time limit in seconds (0 = none).
    #[arg(long, default_value_t = 0.0)]
    time_limit: f32,
    #[arg(long)]
    friendly_fire: bool,
    /// Require this join token (hex u64) from clients.
    #[arg(long, value_parser = parse_hex_u64)]
    join_token: Option<u64>,
    /// Seconds between statistics log lines.
    #[arg(long, default_value_t = 10)]
    stats_interval: u64,
}

/// A bare name ("harbor") means Shared/Definitions/maps/<name>.json.
fn resolve_map(arg: &str) -> PathBuf {
    let p = PathBuf::from(arg);
    if p.extension().is_some() || arg.contains('/') || arg.contains('\\') {
        p
    } else {
        PathBuf::from(format!("Shared/Definitions/maps/{arg}.json"))
    }
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
    let mode = tfps_server::game::GameMode::parse(&args.mode).ok_or_else(|| {
        anyhow::anyhow!(
            "unknown mode '{}' (deathmatch, elimination, defuse)",
            args.mode
        )
    })?;
    let rules = tfps_server::game::MatchRules {
        mode,
        rounds_to_win: args.rounds_to_win.max(1),
        round_time: args.round_time.max(10.0),
        prep_time: args.prep_time.max(0.0),
        score_limit: args.score_limit.max(1),
        time_limit: args.time_limit.max(0.0),
        friendly_fire: args.friendly_fire,
        ..Default::default()
    };
    let cfg = ServerConfig {
        rules,
        bind: args.bind,
        tick_rate: args.tick_rate,
        snapshot_interval: args.snapshot_interval,
        max_players: args.max_players,
        map_path: resolve_map(&args.map),
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
