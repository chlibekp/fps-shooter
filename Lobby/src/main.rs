use clap::Parser;
use std::path::PathBuf;
use tfps_lobby::LobbyConfig;
use tracing_subscriber::EnvFilter;

/// TacticalFPS lobby service: invite codes, custom matches, game servers.
#[derive(Parser, Debug)]
#[command(version, about)]
struct Args {
    /// HTTP API address.
    #[arg(long, default_value = "0.0.0.0:7700")]
    bind: std::net::SocketAddr,
    /// IP the launched game servers bind to.
    #[arg(long, default_value = "0.0.0.0")]
    game_bind_ip: std::net::IpAddr,
    /// Host name or IP clients use to reach the game servers.
    #[arg(long, default_value = "127.0.0.1")]
    public_host: String,
    /// Shared definitions directory (maps, weapons, gadgets).
    #[arg(long, default_value = "Shared/Definitions")]
    definitions: PathBuf,
    /// Most concurrent matches.
    #[arg(long, default_value_t = 16)]
    max_games: usize,
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .init();
    let a = Args::parse();
    let handle = tfps_lobby::start(LobbyConfig {
        bind: a.bind,
        game_bind_ip: a.game_bind_ip,
        public_host: a.public_host,
        definitions: a.definitions,
        max_games: a.max_games,
        ..Default::default()
    })?;
    handle.wait();
    Ok(())
}
