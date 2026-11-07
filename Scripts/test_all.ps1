# Runs the Rust tests and the C++ NetCore tests (offline + live) on Windows.
. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
cargo test --workspace --release
cmake -S Client\NetCoreTests -B build\netcore
cmake --build build\netcore --config Release
& "build\netcore\Release\tfps_netcore_tests.exe"
$server = Start-Process "target\release\tfps-server.exe" -ArgumentList "--bind", "127.0.0.1:17777" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 700
& "build\netcore\Release\tfps_netcore_tests.exe" --server 127.0.0.1:17777 Live
Stop-Process $server
Pop-Location
