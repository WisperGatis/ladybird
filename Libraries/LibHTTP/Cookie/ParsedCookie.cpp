/*
 * Copyright (c) 2021-2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/DateConstants.h>
#include <AK/Function.h>
#include <AK/StdLibExtras.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibHTTP/Cookie/ParsedCookie.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibURL/URL.h>
#include <RustFFI.h>

namespace HTTP::Cookie {

// NB: The string parsing lives in Libraries/LibHTTP/Rust/src/cookie.rs, which reports these attribute kinds.
enum class AttributeKind : u8 {
    Expires,
    MaxAge,
    Domain,
    Path,
};

struct AttributeSink {
    URL::URL const& url;
    ParsedCookie& parsed_cookie;
};

static void on_attribute(void* context, u8 kind, u8 const* value, size_t length);
static void on_expires_attribute(ParsedCookie& parsed_cookie, StringView attribute_value);
static void on_max_age_attribute(ParsedCookie& parsed_cookie, StringView attribute_value);
static ErrorOr<void> on_domain_attribute(ParsedCookie& parsed_cookie, StringView attribute_value);
static void on_path_attribute(URL::URL const&, ParsedCookie& parsed_cookie, StringView attribute_value);

bool cookie_contains_invalid_control_character(StringView cookie_string)
{
    return ladybird_http_contains_invalid_cookie_control_character(cookie_string.bytes().data(), cookie_string.length());
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6-6
Optional<ParsedCookie> parse_cookie(URL::URL const& url, StringView cookie_string)
{
    // NB: Rust performs the control character, size and tokenizing checks, and reports each attribute in order. The
    //     attributes that need the clock or the URL are applied by on_attribute.
    ParsedCookie parsed_cookie;
    AttributeSink sink { url, parsed_cookie };
    LadybirdHttpCookieParts parts {};
    if (!ladybird_http_parse_cookie_string(cookie_string.bytes().data(), cookie_string.length(), &sink, on_attribute, &parts))
        return {};

    parsed_cookie.name = MUST(String::from_utf8(cookie_string.substring_view(parts.name_offset, parts.name_length)));
    parsed_cookie.value = MUST(String::from_utf8(cookie_string.substring_view(parts.value_offset, parts.value_length)));
    parsed_cookie.same_site_attribute = static_cast<SameSite>(parts.same_site);
    parsed_cookie.secure_attribute_present = parts.secure;
    parsed_cookie.http_only_attribute_present = parts.http_only;

    return parsed_cookie;
}

void on_attribute(void* context, u8 kind, u8 const* value, size_t length)
{
    auto& sink = *static_cast<AttributeSink*>(context);
    auto attribute_value = StringView { reinterpret_cast<char const*>(value), length };

    switch (static_cast<AttributeKind>(kind)) {
    case AttributeKind::Expires:
        on_expires_attribute(sink.parsed_cookie, attribute_value);
        break;
    case AttributeKind::MaxAge:
        on_max_age_attribute(sink.parsed_cookie, attribute_value);
        break;
    case AttributeKind::Domain:
        // NB: Rust has already checked that the domain is ASCII and lowercased it.
        sink.parsed_cookie.domain = MUST(String::from_utf8(attribute_value));
        break;
    case AttributeKind::Path:
        on_path_attribute(sink.url, sink.parsed_cookie, attribute_value);
        break;
    }
}


ErrorOr<ParsedCookie> parse_cookie(Cookie const& cookie)
{
    if (cookie.name.is_empty() && cookie.value.is_empty())
        return Error::from_string_literal("Cookie name and value cannot both be empty");

    if (cookie_contains_invalid_control_character(cookie.name))
        return Error::from_string_literal("Cookie name contains an invalid control character");
    if (cookie_contains_invalid_control_character(cookie.value))
        return Error::from_string_literal("Cookie value contains an invalid control character");

    if (cookie.name.byte_count() + cookie.value.byte_count() > 4096)
        return Error::from_string_literal("Cookie name and value exceed the maximum size");

    if (!cookie.path.bytes_as_string_view().starts_with("/"sv))
        return Error::from_string_literal("Cookie path must start with /");
    if (cookie.path.byte_count() > 1024)
        return Error::from_string_literal("Cookie path exceeds the maximum size");

    if (cookie.same_site == SameSite::None && !cookie.secure)
        return Error::from_string_literal("SameSite=None cookies must be secure");

    if (cookie.name.starts_with_bytes("__Secure-"sv, CaseSensitivity::CaseInsensitive) && !cookie.secure)
        return Error::from_string_literal("__Secure- cookies must be secure");

    if (cookie.name.starts_with_bytes("__Http-"sv, CaseSensitivity::CaseInsensitive)) {
        if (!cookie.secure)
            return Error::from_string_literal("__Http- cookies must be secure");
        if (!cookie.http_only)
            return Error::from_string_literal("__Http- cookies must be HTTP-only");
    }

    if (cookie.name.starts_with_bytes("__Host-Http-"sv, CaseSensitivity::CaseInsensitive) && !cookie.http_only)
        return Error::from_string_literal("__Host-Http- cookies must be HTTP-only");

    if (cookie.name.starts_with_bytes("__Host-"sv, CaseSensitivity::CaseInsensitive)) {
        if (!cookie.secure)
            return Error::from_string_literal("__Host- cookies must be secure");
        if (!cookie.host_only)
            return Error::from_string_literal("__Host- cookies must be host-only");
        if (cookie.path != "/"sv)
            return Error::from_string_literal("__Host- cookies must use path /");
    }

    if (cookie.name.is_empty()) {
        if (cookie.value.starts_with_bytes("__Secure-"sv, CaseSensitivity::CaseInsensitive))
            return Error::from_string_literal("__Secure- cookies must have a name");
        if (cookie.value.starts_with_bytes("__Http-"sv, CaseSensitivity::CaseInsensitive))
            return Error::from_string_literal("__Http- cookies must have a name");
        if (cookie.value.starts_with_bytes("__Host-"sv, CaseSensitivity::CaseInsensitive))
            return Error::from_string_literal("__Host- cookies must have a name");
    }

    ParsedCookie parsed_cookie;
    parsed_cookie.name = cookie.name;
    parsed_cookie.value = cookie.value;
    parsed_cookie.same_site_attribute = cookie.same_site;
    parsed_cookie.path = cookie.path;
    parsed_cookie.secure_attribute_present = cookie.secure;
    parsed_cookie.http_only_attribute_present = cookie.http_only;

    if (!cookie.host_only && cookie.domain.byte_count() > 1024)
        return Error::from_string_literal("Cookie host exceeds the maximum size");

    if (!cookie.host_only)
        TRY(on_domain_attribute(parsed_cookie, cookie.domain.bytes_as_string_view()));

    if (cookie.persistent)
        parsed_cookie.expiry_time_from_expires_attribute = cookie.expiry_time;

    return parsed_cookie;
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.1
void on_expires_attribute(ParsedCookie& parsed_cookie, StringView attribute_value)
{
    // 1. Let the expiry-time be the result of parsing the attribute-value as cookie-date (see Section 5.1.1).
    auto expiry_time = parse_cookie_date(attribute_value);

    // 2. If the attribute-value failed to parse as a cookie date, ignore the cookie-av.
    if (!expiry_time.has_value())
        return;

    // 3. Let cookie-age-limit be the maximum age of the cookie (which SHOULD be 400 days in the future or sooner, see
    //    Section 5.5).
    auto cookie_age_limit = UnixDateTime::now() + MAXIMUM_COOKIE_AGE;

    // 4. If the expiry-time is more than cookie-age-limit, the user agent MUST set the expiry time to cookie-age-limit
    //    in seconds.
    if (expiry_time->seconds_since_epoch() > cookie_age_limit.seconds_since_epoch())
        expiry_time = cookie_age_limit;

    // 5. If the expiry-time is earlier than the earliest date the user agent can represent, the user agent MAY replace
    //    the expiry-time with the earliest representable date.
    if (auto earliest = UnixDateTime::earliest(); *expiry_time < earliest)
        expiry_time = earliest;

    // 6. Append an attribute to the cookie-attribute-list with an attribute-name of Expires and an attribute-value of
    //    expiry-time.
    parsed_cookie.expiry_time_from_expires_attribute = expiry_time.release_value();
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.2
void on_max_age_attribute(ParsedCookie& parsed_cookie, StringView attribute_value)
{
    // 1. If the attribute-value is empty, ignore the cookie-av.
    if (attribute_value.is_empty())
        return;

    // 2. If the first character of the attribute-value is neither a DIGIT, nor a "-" character followed by a DIGIT,
    //    ignore the cookie-av.
    // 3. If the remainder of attribute-value contains a non-DIGIT character, ignore the cookie-av.
    auto digits = attribute_value[0] == '-' ? attribute_value.substring_view(1) : attribute_value;

    if (digits.is_empty() || !all_of(digits, is_ascii_digit))
        return;

    // 4. Let delta-seconds be the attribute-value converted to a base 10 integer.
    auto delta_seconds = attribute_value.to_number<i64>();
    if (!delta_seconds.has_value()) {
        // We know the attribute value only contains digits, so if we failed to parse, it is because the result did not
        // fit in an i64. Set the value to the i64 limits in that case. The positive limit will be further capped below,
        // and the negative limit will be immediately expired in the cookie jar.
        delta_seconds = attribute_value[0] == '-' ? NumericLimits<i64>::min() : NumericLimits<i64>::max();
    }

    // 5. Let cookie-age-limit be the maximum age of the cookie (which SHOULD be 400 days or less, see Section 5.5).
    auto cookie_age_limit = MAXIMUM_COOKIE_AGE;

    // 6. Set delta-seconds to the smaller of its present value and cookie-age-limit.
    if (*delta_seconds > cookie_age_limit.to_seconds())
        delta_seconds = cookie_age_limit.to_seconds();

    // 7. If delta-seconds is less than or equal to zero (0), let expiry-time be the earliest representable date and
    //    time. Otherwise, let the expiry-time be the current date and time plus delta-seconds seconds.
    auto expiry_time = *delta_seconds <= 0
        ? UnixDateTime::earliest()
        : UnixDateTime::now() + AK::Duration::from_seconds(*delta_seconds);

    // 8. Append an attribute to the cookie-attribute-list with an attribute-name of Max-Age and an attribute-value of
    //    expiry-time.
    parsed_cookie.expiry_time_from_max_age_attribute = expiry_time;
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.3
ErrorOr<void> on_domain_attribute(ParsedCookie& parsed_cookie, StringView attribute_value)
{
    // https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-4.1.1-9
    // The domain-value is a subdomain as defined by Section 3.5 of [RFC1034], and as enhanced by Section 2.1 of [RFC1123].
    // Thus, domain-value is a string of [USASCII] characters, such as an "A-label" as defined in Section 2.3.2.1 of [RFC5890].
    // NB: The ASCII check, the leading "." removal (steps 1 and 2) and the lowercasing (step 3) are done in
    //     Libraries/LibHTTP/Rust/src/cookie.rs. A lone "." is kept there rather than reduced to "". By the letter of the
    //     spec this would become an empty domain and be stored as a host cookie, but Chromium, Firefox, WPT (and the
    //     spec intent) treat `Domain=.` as an invalid domain. See: https://github.com/httpwg/http-extensions/issues/1939
    // The normalized domain is never longer than the attribute value.
    auto normalized_domain = TRY(ByteBuffer::create_uninitialized(attribute_value.length()));
    size_t normalized_domain_length = 0;
    if (!ladybird_http_normalize_cookie_domain(attribute_value.bytes().data(), attribute_value.length(), normalized_domain.data(), &normalized_domain_length))
        return Error::from_string_literal("Domain must be a string of ASCII characters");

    // 4. Append an attribute to the cookie-attribute-list with an attribute-name of Domain and an attribute-value of
    //    cookie-domain.
    parsed_cookie.domain = MUST(String::from_utf8(StringView { reinterpret_cast<char const*>(normalized_domain.data()), normalized_domain_length }));

    return {};
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.6.4
void on_path_attribute(URL::URL const& url, ParsedCookie& parsed_cookie, StringView attribute_value)
{
    String cookie_path;

    // 1. If the attribute-value is empty or if the first character of the attribute-value is not %x2F ("/"):
    if (attribute_value.is_empty() || attribute_value[0] != '/') {
        // 1. Let cookie-path be the default-path.
        cookie_path = default_path(url);
    }
    // Otherwise:
    else {
        // 1. Let cookie-path be the attribute-value.
        cookie_path = MUST(String::from_utf8(attribute_value));
    }

    // 2. Append an attribute to the cookie-attribute-list with an attribute-name of Path and an attribute-value of
    //    cookie-path.
    parsed_cookie.path = move(cookie_path);
}

// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-rfc6265bis-22#section-5.1.1
Optional<UnixDateTime> parse_cookie_date(StringView date_string)
{
    // The cookie-date is parsed by the Rust decoder in LibHTTP/Rust, which applies the same token rules and range
    // checks. The result is UTC seconds since the epoch.
    // FIXME: This currently uses UNIX time, which is not equivalent to UTC due to leap seconds.
    i64 seconds = 0;
    if (!ladybird_http_parse_cookie_date(date_string.bytes().data(), date_string.length(), &seconds))
        return {};
    return UnixDateTime::from_seconds_since_epoch(seconds);
}

}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, HTTP::Cookie::ParsedCookie const& cookie)
{
    TRY(encoder.encode(cookie.name));
    TRY(encoder.encode(cookie.value));
    TRY(encoder.encode(cookie.expiry_time_from_expires_attribute));
    TRY(encoder.encode(cookie.expiry_time_from_max_age_attribute));
    TRY(encoder.encode(cookie.domain));
    TRY(encoder.encode(cookie.path));
    TRY(encoder.encode(cookie.secure_attribute_present));
    TRY(encoder.encode(cookie.http_only_attribute_present));
    TRY(encoder.encode(cookie.same_site_attribute));

    return {};
}

template<>
ErrorOr<HTTP::Cookie::ParsedCookie> IPC::decode(Decoder& decoder)
{
    auto name = TRY(decoder.decode<String>());
    auto value = TRY(decoder.decode<String>());
    auto expiry_time_from_expires_attribute = TRY(decoder.decode<Optional<UnixDateTime>>());
    auto expiry_time_from_max_age_attribute = TRY(decoder.decode<Optional<UnixDateTime>>());
    auto domain = TRY(decoder.decode<Optional<String>>());
    auto path = TRY(decoder.decode<Optional<String>>());
    auto secure_attribute_present = TRY(decoder.decode<bool>());
    auto http_only_attribute_present = TRY(decoder.decode<bool>());
    auto same_site_attribute = TRY(decoder.decode<HTTP::Cookie::SameSite>());

    return HTTP::Cookie::ParsedCookie { move(name), move(value), same_site_attribute, move(expiry_time_from_expires_attribute), move(expiry_time_from_max_age_attribute), move(domain), move(path), secure_attribute_present, http_only_attribute_present };
}
