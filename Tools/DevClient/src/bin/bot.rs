//! Headless bot clients for testing and load generation.
//!
//! `tfps-bot --server 127.0.0.1:7777 --count 2 --seconds 30`
//! Each bot connects, moves with a scripted pattern and prints what it sees.

use clap::{Parser, ValueEnum};
use std::time::{Duration, Instant};
use tfps_devclient::{Client, ClientConfig, ClientEvent};
use tfps_sim::{buttons, math, MapDef, MoveInput, WeaponSet};

#[derive(Clone, Copy, Debug, ValueEnum)]
enum Pattern {
    /// Run in a circle.
    Circle,
    /// Strafe left/right while facing forward.
    Strafe,
    /// Random direction changes, sprint, jump, crouch.
    Random,
    /// Stand still.
    Idle,
}

#[derive(Parser, Debug)]
struct Args {
    #[arg(long, default_value = "127.0.0.1:7777")]
    server: std::net::SocketAddr,
    #[arg(long, default_value_t = 1)]
    count: usize,
    #[arg(long, default_value_t = 30)]
    seconds: u64,
    #[arg(long, value_enum, default_value_t = Pattern::Circle)]
    pattern: Pattern,
    #[arg(long, default_value = "Shared/Definitions/maps/test_arena.json")]
    map: std::path::PathBuf,
    #[arg(long, default_value = "Shared/Definitions/weapons.json")]
    weapons: std::path::PathBuf,
    /// Simulated packet loss in each direction (0..1).
    #[arg(long, default_value_t = 0.0)]
    loss: f32,
    /// Join token (hex), if the server requires one.
    #[arg(long, default_value = "0")]
    join_token: String,
    /// Print a status line every N seconds (0 = only summary).
    #[arg(long, default_value_t = 2)]
    report: u64,
}

fn input_for(pattern: Pattern, t: f32, idx: usize, rng: &mut u64) -> MoveInput {
    match pattern {
        Pattern::Idle => MoveInput::default(),
        Pattern::Circle => {
            let yaw = (t * 45.0 + idx as f32 * 90.0) % 360.0;
            MoveInput {
                buttons: buttons::FORWARD,
                yaw_q: math::quantize_yaw_deg(yaw),
                pitch_q: 0,
                slot: 0,
            }
        }
        Pattern::Strafe => {
            let b = if (t as u32 / 2).is_multiple_of(2) {
                buttons::LEFT
            } else {
                buttons::RIGHT
            };
            MoveInput {
                buttons: b,
                yaw_q: math::quantize_yaw_deg(idx as f32 * 45.0),
                pitch_q: 0,
                slot: 0,
            }
        }
        Pattern::Random => {
            // Re-roll roughly twice a second (caller keeps the result between).
            *rng ^= *rng << 13;
            *rng ^= *rng >> 7;
            *rng ^= *rng << 17;
            let r = *rng;
            let mut b = buttons::FORWARD;
            if r & 3 == 0 {
                b |= buttons::SPRINT;
            }
            if (r >> 2) & 7 == 0 {
                b |= buttons::JUMP;
            }
            if (r >> 5) & 7 == 0 {
                b |= buttons::CROUCH;
            }
            if (r >> 8) & 1 == 0 {
                b |= buttons::FIRE | buttons::AIM;
            }
            if (r >> 9) & 7 == 0 {
                b |= buttons::RELOAD;
            }
            MoveInput {
                buttons: b,
                yaw_q: (r >> 16) as u16,
                pitch_q: 0,
                slot: 0,
            }
        }
    }
}

fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    let map = MapDef::load(&args.map)?;
    let weapons = WeaponSet::load(&args.weapons)?;
    let join_token = u64::from_str_radix(args.join_token.trim_start_matches("0x"), 16)?;
    let mut bots = Vec::new();
    for i in 0..args.count {
        let mut cfg = ClientConfig::new(args.server, &format!("Bot{}", i + 1));
        cfg.simulated_loss = args.loss;
        cfg.join_token = join_token;
        bots.push(Client::connect(cfg, &map, &weapons)?);
    }
    let start = Instant::now();
    let end = start + Duration::from_secs(args.seconds);
    let mut next_report = start + Duration::from_secs(args.report.max(1));
    let mut rngs: Vec<u64> = (0..args.count)
        .map(|i| 0x9E37_79B9 ^ ((i as u64 + 1) * 0x1000_0001))
        .collect();
    let mut held: Vec<(MoveInput, Instant)> = vec![(MoveInput::default(), start); args.count];
    while Instant::now() < end {
        let now = Instant::now();
        let t = now.duration_since(start).as_secs_f32();
        for (i, bot) in bots.iter_mut().enumerate() {
            if matches!(args.pattern, Pattern::Random) {
                if now >= held[i].1 {
                    held[i] = (
                        input_for(args.pattern, t, i, &mut rngs[i]),
                        now + Duration::from_millis(500),
                    );
                }
                bot.set_input(held[i].0);
            } else {
                bot.set_input(input_for(args.pattern, t, i, &mut rngs[i]));
            }
            bot.update(now);
            while let Some(ev) = bot.poll_event() {
                match ev {
                    ClientEvent::Connected { player_id } => {
                        println!("[{}] connected as player {player_id}", bot.cfg.name)
                    }
                    ClientEvent::PlayerJoined {
                        player_id,
                        name,
                        team,
                    } if player_id != bot.player_id => {
                        println!(
                            "[{}] sees player {player_id} '{name}' (team {team})",
                            bot.cfg.name
                        )
                    }
                    ClientEvent::PlayerLeft { player_id, reason } => {
                        println!("[{}] player {player_id} left ({reason:?})", bot.cfg.name)
                    }
                    ClientEvent::PlayerConnState { player_id, state } => {
                        println!("[{}] player {player_id} connection {state:?}", bot.cfg.name)
                    }
                    ClientEvent::Disconnected(c) => {
                        println!("[{}] disconnected: {c:?}", bot.cfg.name)
                    }
                    _ => {}
                }
            }
        }
        if args.report > 0 && now >= next_report {
            next_report += Duration::from_secs(args.report);
            for bot in &bots {
                let p = bot.predicted.pos;
                let others: Vec<String> = bot
                    .players
                    .values()
                    .filter(|r| r.present)
                    .map(|r| {
                        format!(
                            "{}@({:.2},{:.2},{:.2})",
                            r.id, r.render_pos.x, r.render_pos.y, r.render_pos.z
                        )
                    })
                    .collect();
                println!(
                    "[{}] pos=({:.2},{:.2},{:.2}) rtt={:.1}ms depth={} scale={:.2} corrections={} max_corr={:.3}m sees=[{}]",
                    bot.cfg.name,
                    p.x,
                    p.y,
                    p.z,
                    bot.stats.rtt_ms,
                    bot.stats.server_input_depth,
                    bot.stats.time_scale,
                    bot.stats.corrections,
                    bot.stats.max_correction_m,
                    others.join(" ")
                );
            }
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    let secs = start.elapsed().as_secs_f64();
    for bot in &mut bots {
        let s = bot.stats;
        println!(
            "[{}] summary: shots={} remote_shot_events={} cmds={} snapshots={} (delta {}) up={:.1} kbit/s down={:.1} kbit/s corrections={} max_corr={:.3}m",
            bot.cfg.name,
            bot.local_shots,
            bot.remote_shot_events.len(),
            bot.command_seq(),
            s.snapshots_received,
            s.delta_snapshots,
            s.bytes_sent as f64 * 8.0 / 1000.0 / secs,
            s.bytes_received as f64 * 8.0 / 1000.0 / secs,
            s.corrections,
            s.max_correction_m
        );
        bot.disconnect();
    }
    Ok(())
}
