// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <curl/curl.h>

namespace sdlagent::ca
{

//Applies the embedded Mozilla CA bundle to a curl handle. Keeping the bundle
//in the binary avoids depending on the build machine's OpenSSL paths.
void apply(CURL* curl);

} //namespace sdlagent::ca
