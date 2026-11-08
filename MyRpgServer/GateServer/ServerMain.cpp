#include "../ServerCore/EnvironmentConfig.h"
#include "GateServer.h"
#include <iostream>

int main()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
    {
        return 1;
    }

    int result = 0;
    {
        GateServer gate(static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_GATE_PORT", 9000, 1, 65535)),
                        static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_LOGIN_PORT", 9001, 1, 65535)),
                        static_cast<unsigned short>(RpgConfig::GetUnsigned("RPG_GAME_PORT", 9200, 1, 65535)),
                        RpgConfig::GetUnsigned("RPG_GAME_ENABLE_DEVELOPMENT_PACKETS", 0, 0, 1) != 0);
        if (!gate.Start())
        {
            result = 1;
        }
        else
        {
            RPG_LOG_INFO("Gate", "GateServer ready (loopback only; public TLS deployment not configured).");
            std::string command;
            while (std::getline(std::cin, command))
            {
                if (command == "shutdown" || command == "-shutdown" || command == "exit")
                {
                    break;
                }

                if (command == "status")
                {
                    RPG_LOG_INFO("Gate", "users=" << gate.Users());
                }

            }

            gate.Stop();
        }

    }

    RpgLogging::Logger::Shutdown();
    WSACleanup();
    return result;
}

