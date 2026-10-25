/*
 * Copyright (c) 2026, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibHTTP/HSTS/ParsedHSTSPolicy.h>
#include <LibHTTP/HTTP.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <RustFFI.h>

namespace HTTP::HSTS {

// https://www.rfc-editor.org/rfc/rfc6797#section-6.1
// The parser lives in LibHTTP/Rust, which implements the grammar and the directive rules.
Optional<ParsedHSTSPolicy> parse_header(StringView header_value)
{
    u64 max_age_seconds = 0;
    bool include_sub_domains = false;
    if (!ladybird_http_parse_hsts_header(header_value.bytes().data(), header_value.length(), &max_age_seconds, &include_sub_domains))
        return {};

    return ParsedHSTSPolicy {
        .max_age = AK::Duration::from_seconds(static_cast<i64>(max_age_seconds)),
        .include_sub_domains = include_sub_domains,
    };
}

}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, HTTP::HSTS::ParsedHSTSPolicy const& policy)
{
    TRY(encoder.encode(policy.max_age));
    TRY(encoder.encode(policy.include_sub_domains));
    return {};
}

template<>
ErrorOr<HTTP::HSTS::ParsedHSTSPolicy> IPC::decode(Decoder& decoder)
{
    auto max_age = TRY(decoder.decode<AK::Duration>());
    auto include_sub_domains = TRY(decoder.decode<bool>());
    return HTTP::HSTS::ParsedHSTSPolicy { max_age, include_sub_domains };
}
