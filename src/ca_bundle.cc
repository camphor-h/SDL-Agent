// SPDX-License-Identifier: GPL-3.0-only

#include "ca_bundle.hpp"

#include "ca_data.inc"

namespace sdlagent::ca
{

void apply(CURL* curl)
{
    if (!curl)
        return;

    static curl_blob blob{
        const_cast<unsigned char*>(sdlagent::ca::kCa),
        sdlagent::ca::kCaSize,
        CURL_BLOB_NOCOPY
    };
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
}

} //namespace sdlagent::ca
