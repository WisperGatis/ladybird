/*
 * Copyright (c) 2023, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "UserAgent.h"

namespace WebView {

OrderedHashMap<StringView, StringView> const user_agents = {
    { "Chrome Linux Desktop"sv, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"sv },
    { "Chrome macOS Desktop"sv, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"sv },
    { "Chrome Windows Desktop"sv, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"sv },
    { "Chrome Web Store Compatible"sv, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"sv },
    { "Firefox Linux Desktop"sv, "Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0"sv },
    { "Firefox macOS Desktop"sv, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0"sv },
    { "Safari macOS Desktop"sv, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15"sv },
    { "Chrome Android Mobile"sv, "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36"sv },
    { "Firefox Android Mobile"sv, "Mozilla/5.0 (Android 13; Mobile; rv:133.0) Gecko/133.0 Firefox/133.0"sv },
    { "Safari iOS Mobile"sv, "Mozilla/5.0 (iPhone; CPU iPhone OS 17_6_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Mobile/15E148 Safari/604.1"sv },
};

Optional<StringView> normalize_user_agent_name(StringView name)
{
    for (auto const& user_agent : user_agents) {
        if (user_agent.key.equals_ignoring_ascii_case(name))
            return user_agent.key;
    }

    return {};
}

}
