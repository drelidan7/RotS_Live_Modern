use std::{
    net::{SocketAddr, SocketAddrV4},
    str::FromStr,
    sync::Arc,
};

use clap::Parser;
use color_eyre::eyre::{bail, ContextCompat, Report};
use futures_util::{stream::StreamExt, SinkExt};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
};

use tokio_tungstenite::tungstenite::{handshake::server::Request, Message};

const READ_BUFFER_SIZE: usize = 8 * 1024; // 8 kb

#[derive(Clone, Debug)]
struct GameAddr {
    hostname: Arc<String>,
    port: u16,
}

impl GameAddr {
    async fn connect(&self) -> Result<TcpStream, Report> {
        let stream = TcpStream::connect((self.hostname.as_str(), self.port)).await?;
        if let Err(error) = stream.set_nodelay(true) {
            log::warn!("Failed to set TCP_NODELAY on game connection: {error}");
        }
        Ok(stream)
    }
}

impl FromStr for GameAddr {
    type Err = Report;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (hostname, port) = s.split_once(':').context("Missing port")?;

        Ok(Self {
            hostname: Arc::new(String::from(hostname)),
            port: port.parse()?,
        })
    }
}

/// Simple program to greet a person
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    #[arg(short, long, default_value = "127.0.0.1:1024")]
    /// Address of the game
    game: GameAddr,

    #[arg(short, long, default_value = "0.0.0.0:3791")]
    /// TCP address to listen
    listen: SocketAddrV4,

    #[arg(short, long, default_value = "0.0.0.0:8080")]
    /// WebSocket address to listen
    websocket: SocketAddrV4,

    #[arg(short, long)]
    /// Get the connecting IP from the Cloudflare header
    cloudflare: bool,
}

async fn handle_tcp(game: GameAddr, mut stream: TcpStream, addr: SocketAddr) -> Result<(), Report> {
    if let Err(error) = stream.set_nodelay(true) {
        log::warn!("Failed to set TCP_NODELAY on client connection: {error}");
    }
    let addr = match addr {
        SocketAddr::V4(addr) => addr,
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    // Connect to the game
    let mut game = game.connect().await?;

    // Write the proxy header
    game.write_u32(u32::from(*addr.ip())).await?;

    // Start proxying
    tokio::io::copy_bidirectional(&mut stream, &mut game).await?;

    Ok(())
}

async fn tcp_server(game: GameAddr, listener: TcpListener) -> Result<(), Report> {
    loop {
        let (stream, addr) = listener.accept().await?;
        let game = game.clone();

        log::debug!("Received TCP connection on {addr}");

        tokio::spawn(async move {
            if let Err(err) = handle_tcp(game, stream, addr).await {
                log::error!("{err}");
            }
        });
    }
}

async fn handle_ws(
    game: GameAddr,
    stream: TcpStream,
    addr: SocketAddr,
    cloudflare: bool,
) -> Result<(), Report> {
    if let Err(error) = stream.set_nodelay(true) {
        log::warn!("Failed to set TCP_NODELAY on websocket connection: {error}");
    }

    let mut addr = match addr {
        SocketAddr::V4(addr) => *addr.ip(),
        SocketAddr::V6(addr) => bail!("Unexpected IPv6: {addr}"),
    };

    let mut ws = tokio_tungstenite::accept_hdr_async(stream, |req: &Request, res| {
        if let Some(header) = cloudflare
            .then_some(req.headers())
            .and_then(|h| h.get("CF-Connecting-IP"))
            .and_then(|h| h.to_str().ok())
            .and_then(|h| h.parse().ok())
        {
            addr = header;
        }

        Ok(res)
    })
    .await?;

    let mut game = game.connect().await?;
    let mut buf = [0; READ_BUFFER_SIZE];

    // Write the proxy header
    game.write_u32(addr.into()).await?;

    // Start proxying
    loop {
        tokio::select! {
            msg = ws.next() => {
                let msg = if let Some(msg) = msg {
                    msg?
                } else {
                    break;
                };

                match msg {
                    Message::Text(msg) => game.write_all(msg.as_bytes()).await?,
                    Message::Binary(msg) => game.write_all(&msg).await?,
                    Message::Close(_) => break,
                    _ => continue,
                }
            },
            read = game.read(&mut buf) => {
                let read = read?;

                if read > 0 {
                    ws.send(Message::Binary(Vec::from(&buf[..read]))).await?;
                }
            }
        }
    }

    Ok(())
}

async fn ws_server(game: GameAddr, listener: TcpListener, cloudflare: bool) -> Result<(), Report> {
    loop {
        let (stream, addr) = listener.accept().await?;
        let game = game.clone();

        log::debug!("Received websocket connection on {addr}");

        tokio::spawn(async move {
            if let Err(err) = handle_ws(game, stream, addr, cloudflare).await {
                log::error!("{err}");
            }
        });
    }
}

#[tokio::main]
async fn main() -> Result<(), Report> {
    env_logger::init();
    color_eyre::install()?;

    let args = Args::parse();
    let tcp = TcpListener::bind(args.listen).await?;
    let ws = TcpListener::bind(args.websocket).await?;

    if let Ok(addr) = tcp.local_addr() {
        log::info!("Listening for TCP connections on {}", addr);
    }

    if let Ok(addr) = ws.local_addr() {
        log::info!("Listening for WebSocket connections on {}", addr);
    }

    let tcp = tokio::spawn(tcp_server(args.game.clone(), tcp));
    let ws = tokio::spawn(ws_server(args.game, ws, args.cloudflare));

    tokio::select! {
        res = tcp => res??,
        res = ws => res??,
    };

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn game_connection_disables_nagle() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let endpoint = listener.local_addr().unwrap();
        let game = GameAddr {
            hostname: Arc::new("127.0.0.1".to_owned()),
            port: endpoint.port(),
        };
        let connection = game.connect().await.unwrap();
        assert!(connection.nodelay().unwrap());
        let (_accepted, _) = listener.accept().await.unwrap();
    }

    #[tokio::test]
    async fn tcp_proxy_preserves_header_and_bidirectional_bytes() {
        let game_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let game_endpoint = game_listener.local_addr().unwrap();
        let game = GameAddr {
            hostname: Arc::new("127.0.0.1".to_owned()),
            port: game_endpoint.port(),
        };
        let proxy_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let mut client = TcpStream::connect(proxy_listener.local_addr().unwrap()).await.unwrap();
        let (proxy_connection, client_address) = proxy_listener.accept().await.unwrap();
        let proxy_task = tokio::spawn(handle_tcp(game, proxy_connection, client_address));
        let (mut server, _) = game_listener.accept().await.unwrap();
        assert_eq!(server.read_u32().await.unwrap(), u32::from(std::net::Ipv4Addr::LOCALHOST));
        client.write_all(b"look\r\n").await.unwrap();
        let mut command = [0u8; 6];
        server.read_exact(&mut command).await.unwrap();
        assert_eq!(&command, b"look\r\n");
        server.write_all(b"room\r\n").await.unwrap();
        let mut response = [0u8; 6];
        client.read_exact(&mut response).await.unwrap();
        assert_eq!(&response, b"room\r\n");
        drop(client);
        drop(server);
        proxy_task.await.unwrap().unwrap();
    }

    #[tokio::test]
    async fn websocket_proxy_preserves_header_and_bidirectional_bytes() {
        // A blocking timer avoids adding Tokio's optional time feature just for this test.
        // Runtime teardown joins it, so even the successful path remains bounded to five seconds.
        let deadline = tokio::task::spawn_blocking(|| {
            std::thread::sleep(std::time::Duration::from_secs(5));
        });
        let scenario = async {
            let game_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
            let game_endpoint = game_listener.local_addr().unwrap();
            let game = GameAddr {
                hostname: Arc::new("127.0.0.1".to_owned()),
                port: game_endpoint.port(),
            };
            let proxy_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
            let client = TcpStream::connect(proxy_listener.local_addr().unwrap()).await.unwrap();
            let (proxy_connection, client_address) = proxy_listener.accept().await.unwrap();
            let proxy_task = tokio::spawn(handle_ws(game, proxy_connection, client_address, false));
            let (mut websocket, handshake) =
                tokio_tungstenite::client_async("ws://127.0.0.1/", client).await.unwrap();
            assert_eq!(handshake.status().as_u16(), 101);
            let (mut server, _) = game_listener.accept().await.unwrap();
            assert_eq!(server.read_u32().await.unwrap(), u32::from(std::net::Ipv4Addr::LOCALHOST));
            websocket.send(Message::Text("look\r\n".to_owned())).await.unwrap();
            let mut command = [0u8; 6];
            server.read_exact(&mut command).await.unwrap();
            assert_eq!(&command, b"look\r\n");
            server.write_all(b"room\r\n").await.unwrap();
            let mut response = Vec::new();
            while response.len() < 6 {
                match websocket.next().await.unwrap().unwrap() {
                    Message::Binary(bytes) => response.extend_from_slice(&bytes),
                    unexpected => panic!("Unexpected response frame: {unexpected:?}"),
                }
            }
            assert_eq!(&response, b"room\r\n");
            websocket.close(None).await.unwrap();
            proxy_task.await.unwrap().unwrap();
        };
        tokio::select! {
            _ = deadline => panic!("WebSocket proxy round trip exceeded five seconds"),
            _ = scenario => {},
        }
    }
}
