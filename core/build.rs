use std::{env, process::Command};

fn main() {
    let hash = env::var("GITHUB_SHA")
        .ok()
        .and_then(|value| value.get(..7).map(str::to_owned))
        .or_else(|| {
            Command::new("git")
                .args(["rev-parse", "--short=7", "HEAD"])
                .output()
                .ok()
                .filter(|output| output.status.success())
                .and_then(|output| String::from_utf8(output.stdout).ok())
                .map(|value| value.trim().to_owned())
        })
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "source".to_owned());
    println!("cargo:rustc-env=GIT_HASH={hash}");
}
