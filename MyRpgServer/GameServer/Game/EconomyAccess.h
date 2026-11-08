#pragma once

#include "../../Protocol/EconomyPackets.h"

#include <functional>

using SubmitEconomy = std::function<bool(std::uint64_t, const shared_world::economy::RpcRequest&,

                                         std::function<void(const shared_world::economy::RpcResponse&)>)>;
