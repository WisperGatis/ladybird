set(SOURCES
    Message.cpp
    Validator.cpp
)

ladybird_lib(LibDNS dns EXPLICIT_SYMBOL_EXPORT)
import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libdns_rust FFI_HEADER RustFFI.h)
target_link_libraries(LibDNS PRIVATE LibCore libdns_rust PUBLIC LibCrypto LibThreading)
ladybird_hide_rust_crate_symbols(LibDNS libdns_rust)
test_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libdns_rust)
