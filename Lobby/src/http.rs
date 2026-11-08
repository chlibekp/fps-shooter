//! Minimal HTTP/1.1 JSON API (one request per connection).
//!
//! | Method | Path | Body | Auth |
//! |--------|------|------|------|
//! | GET  | /health | | |
//! | POST | /lobbies | {"name"} | |
//! | POST | /lobbies/join | {"code","name"} | |
//! | GET  | /lobbies/{code} | | member |
//! | POST | /lobbies/{code}/settings | MatchSettings | host |
//! | POST | /lobbies/{code}/squad | {"squad"} | member |
//! | POST | /lobbies/{code}/ready | {"ready"} | member |
//! | POST | /lobbies/{code}/start | | host |
//! | POST | /lobbies/{code}/leave | | member |
//!
//! Auth: `Authorization: Bearer <token>` (hex token from create/join).
//! Every response that carries `"lobby"` also lists the playable `"maps"`.
//! Responses: JSON; errors are `{"error": "..."}` with 4xx status.

use crate::service::Service;
use crate::state::{LobbyError, MatchSettings};
use serde_json::{json, Value};
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Instant;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

const MAX_REQUEST: usize = 64 * 1024;

pub async fn serve(listener: TcpListener, svc: Arc<Service>) {
    loop {
        let Ok((stream, peer)) = listener.accept().await else {
            continue;
        };
        let svc = svc.clone();
        tokio::spawn(async move {
            let _ = tokio::time::timeout(
                std::time::Duration::from_secs(10),
                handle(stream, peer, svc),
            )
            .await;
        });
    }
}

struct Request {
    method: String,
    path: String,
    token: Option<u64>,
    body: Vec<u8>,
}

async fn read_request(stream: &mut TcpStream) -> Option<Request> {
    let mut buf = Vec::with_capacity(2048);
    let mut chunk = [0u8; 4096];
    loop {
        let n = stream.read(&mut chunk).await.ok()?;
        if n == 0 {
            return None;
        }
        buf.extend_from_slice(&chunk[..n]);
        if buf.len() > MAX_REQUEST {
            return None;
        }
        let mut headers = [httparse::EMPTY_HEADER; 32];
        let mut req = httparse::Request::new(&mut headers);
        match req.parse(&buf) {
            Ok(httparse::Status::Complete(head_len)) => {
                let mut len = 0usize;
                let mut token = None;
                for h in req.headers.iter() {
                    if h.name.eq_ignore_ascii_case("content-length") {
                        len = std::str::from_utf8(h.value).ok()?.trim().parse().ok()?;
                    } else if h.name.eq_ignore_ascii_case("authorization") {
                        let v = std::str::from_utf8(h.value).ok()?.trim();
                        token = v
                            .strip_prefix("Bearer ")
                            .and_then(|t| u64::from_str_radix(t.trim(), 16).ok());
                    }
                }
                if len > MAX_REQUEST {
                    return None;
                }
                let method = req.method?.to_string();
                let path = req.path?.to_string();
                while buf.len() < head_len + len {
                    let n = stream.read(&mut chunk).await.ok()?;
                    if n == 0 {
                        return None;
                    }
                    buf.extend_from_slice(&chunk[..n]);
                }
                return Some(Request {
                    method,
                    path,
                    token,
                    body: buf[head_len..head_len + len].to_vec(),
                });
            }
            Ok(httparse::Status::Partial) => continue,
            Err(_) => return None,
        }
    }
}

async fn handle(mut stream: TcpStream, peer: SocketAddr, svc: Arc<Service>) {
    let Some(req) = read_request(&mut stream).await else {
        return;
    };
    // Starting a match spawns threads and binds sockets: keep it off the async workers.
    let (status, body) = tokio::task::spawn_blocking(move || route(&svc, &req, peer))
        .await
        .unwrap_or((500, json!({"error": "internal error"})));
    let text = body.to_string();
    let reason = match status {
        200 => "OK",
        201 => "Created",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        409 => "Conflict",
        429 => "Too Many Requests",
        _ => "Error",
    };
    let resp = format!(
        "HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{text}",
        text.len()
    );
    let _ = stream.write_all(resp.as_bytes()).await;
    let _ = stream.shutdown().await;
}

fn err(e: LobbyError) -> (u16, Value) {
    (e.status(), json!({ "error": e.message() }))
}

fn body_json(req: &Request) -> Result<Value, (u16, Value)> {
    if req.body.is_empty() {
        return Ok(json!({}));
    }
    serde_json::from_slice(&req.body).map_err(|_| (400, json!({"error": "invalid JSON"})))
}

fn route(svc: &Service, req: &Request, peer: SocketAddr) -> (u16, Value) {
    let now = Instant::now();
    let parts: Vec<&str> = req.path.trim_matches('/').split('/').collect();
    let body = match body_json(req) {
        Ok(b) => b,
        Err(e) => return e,
    };
    let str_field = |k: &str| {
        body.get(k)
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string()
    };
    let token = req.token.unwrap_or(0);
    let maps = svc.map_names();
    let mut st = svc.state.lock().unwrap();
    let result = match (req.method.as_str(), parts.as_slice()) {
        ("GET", ["health"]) => {
            return (
                200,
                json!({"ok": true, "lobbies": st.lobbies.len(), "games": svc.running_games()}),
            )
        }
        ("POST", ["lobbies"]) => {
            return match st.create(&str_field("name"), now) {
                Ok((v, t)) => (
                    201,
                    json!({"token": format!("{t:016x}"), "lobby": v, "maps": maps}),
                ),
                Err(e) => err(e),
            }
        }
        ("POST", ["lobbies", "join"]) => {
            return match st.join(&str_field("code"), &str_field("name"), peer.ip(), now) {
                Ok((v, t)) => (
                    200,
                    json!({"token": format!("{t:016x}"), "lobby": v, "maps": maps}),
                ),
                Err(e) => err(e),
            }
        }
        ("GET", ["lobbies", code]) => st.view(code, token, now),
        ("POST", ["lobbies", code, "settings"]) => {
            match serde_json::from_value::<MatchSettings>(body.clone()) {
                Ok(s) => st.update_settings(code, token, s),
                Err(_) => Err(LobbyError::BadRequest("invalid settings".into())),
            }
        }
        ("POST", ["lobbies", code, "squad"]) => {
            let squad = body
                .get("squad")
                .and_then(|v| v.as_u64())
                .unwrap_or(9)
                .min(255) as u8;
            st.set_squad(code, token, squad)
        }
        ("POST", ["lobbies", code, "ready"]) => {
            let ready = body.get("ready").and_then(|v| v.as_bool()).unwrap_or(true);
            st.set_ready(code, token, ready)
        }
        ("POST", ["lobbies", code, "leave"]) => {
            return match st.leave(code, token) {
                Ok(()) => (200, json!({"ok": true})),
                Err(e) => err(e),
            }
        }
        ("POST", ["lobbies", code, "start"]) => {
            let code = code.to_string();
            drop(st);
            return match svc.start_match(&code, token) {
                Ok(v) => (200, json!({ "lobby": v, "maps": maps })),
                Err(e) => err(e),
            };
        }
        _ => return (404, json!({"error": "no such endpoint"})),
    };
    match result {
        Ok(v) => (200, json!({ "lobby": v, "maps": maps })),
        Err(e) => err(e),
    }
}
