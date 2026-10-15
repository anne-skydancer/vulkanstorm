# -*- cmake -*-
include(Prebuilt)
include_guard()

add_library(ll::websocketpp INTERFACE IMPORTED)
use_prebuilt_binary(websocketpp)
