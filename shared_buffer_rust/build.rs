use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rustc-link-lib=dylib=shared_buffer");

    if let Some(prefixes) = env::var_os("AMENT_PREFIX_PATH") {
        for prefix in env::split_paths(&prefixes) {
            let lib_dir = prefix.join("lib");
            if lib_dir.is_dir() {
                println!("cargo:rustc-link-search=native={}", lib_dir.display());
            }
        }
    }

    // Keep direct cargo builds useful when a ROS overlay is supplied through a
    // conventional CMake-style variable instead of AMENT_PREFIX_PATH.
    if let Some(prefix) = env::var_os("SHARED_BUFFER_PREFIX") {
        let lib_dir = PathBuf::from(prefix).join("lib");
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
    }

    println!("cargo:rerun-if-env-changed=AMENT_PREFIX_PATH");
    println!("cargo:rerun-if-env-changed=SHARED_BUFFER_PREFIX");
}
